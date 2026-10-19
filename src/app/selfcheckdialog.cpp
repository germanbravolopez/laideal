#include "selfcheckdialog.h"

#include "aeatqr.h"
#include "appsettings.h"
#include "imprimir.h"
#include "sql_lite.h"
#include "uikit.h"
#include "version.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

using SelfCheck::Check;

namespace {
const char *const kConnection = "selfcheck";
const QString kVersion = QStringLiteral("%1.%2").arg(PROJECT_VERSION_MAJOR).arg(PROJECT_VERSION_MINOR);
}

SelfCheckDialog::SelfCheckDialog(const QString &liveSettingsFile, QWidget *parent)
    : QDialog(parent)
    , m_liveSettingsFile(liveSettingsFile)
    , m_reportFolder(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation))
{
    buildUi();
}

SelfCheckDialog::~SelfCheckDialog()
{
    cleanUp();
}

void SelfCheckDialog::buildUi()
{
    UiKit::setUpDialog(this, tr("Comprobación tras actualizar"), 720);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(UiKit::introPanel(tr(
        "Comprueba esta versión con los datos y la impresora de la tienda. Trabaja sobre una copia de la "
        "última copia de seguridad: la base de datos real no se modifica y no se envía nada a la AEAT. "
        "Responda a las preguntas; al terminar se guarda un informe en el escritorio para enviarlo.")));

    m_table = new QTableWidget(0, 3);
    m_table->setObjectName("tableChecks");
    m_table->setHorizontalHeaderLabels({ tr("Comprobación"), tr("Resultado"), tr("Detalle") });
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table);

    m_grpQuestion = new QGroupBox(tr("Pregunta"));
    QVBoxLayout *q = new QVBoxLayout(m_grpQuestion);
    m_lblQuestion = new QLabel();
    m_lblQuestion->setObjectName("lblQuestion");
    m_lblQuestion->setWordWrap(true);
    m_lblInfo = new QLabel();
    m_lblInfo->setObjectName("lblInfo");
    m_lblInfo->setWordWrap(true);
    m_lblInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_leNote = new QLineEdit();
    m_leNote->setObjectName("leNote");
    m_leNote->setPlaceholderText(tr("Nota (opcional): qué ha visto si algo no está bien"));
    QHBoxLayout *buttons = new QHBoxLayout();
    m_btnOpen = UiKit::secondaryButton(tr("Abrir el informe presentado"), "btnOpenReport");
    m_btnNo = UiKit::secondaryButton(tr("No"), "btnNo");
    m_btnYes = UiKit::primaryButton(tr("Sí"), "btnYes");
    buttons->addWidget(m_btnOpen);
    buttons->addStretch();
    buttons->addWidget(m_btnNo);
    buttons->addWidget(m_btnYes);
    q->addWidget(m_lblQuestion);
    q->addWidget(m_lblInfo);
    q->addWidget(m_leNote);
    q->addLayout(buttons);
    m_grpQuestion->setVisible(false);
    layout->addWidget(m_grpQuestion);

    m_result = new UiKit::ResultPanel(tr("Preparando la copia de los datos..."));
    layout->addWidget(m_result);
    layout->addLayout(UiKit::closeRow(this));

    connect(m_btnYes, &QPushButton::clicked, this, [this] { answer(true); });
    connect(m_btnNo, &QPushButton::clicked, this, [this] { answer(false); });
    connect(m_btnOpen, &QPushButton::clicked, this, [this] {
        if (m_current >= 0 && m_current < m_questions.size())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_questions[m_current].openFile));
    });
}

int SelfCheckDialog::addCheck(const QString &id, const QString &title)
{
    Check c;
    c.id = id;
    c.title = title;
    m_checks.append(c);
    m_table->insertRow(m_table->rowCount());
    showCheck(m_checks.size() - 1);
    return m_checks.size() - 1;
}

void SelfCheckDialog::showCheck(int index)
{
    const Check &c = m_checks[index];
    static const QStringList texts = { tr("..."), tr("Correcto"), tr("FALLO"), tr("Omitido") };
    m_table->setItem(index, 0, new QTableWidgetItem(c.id + " " + c.title));
    m_table->setItem(index, 1, new QTableWidgetItem(texts.value(c.status)));
    m_table->setItem(index, 2, new QTableWidgetItem(c.reason));
}

void SelfCheckDialog::start()
{
    QString error;
    if (!prepareCopy(&error)) {
        const int i = addCheck(QStringLiteral("M0"), tr("Copia de los datos"));
        m_checks[i].status = Check::Fail;
        m_checks[i].reason = error;
        showCheck(i);
        finish();
        return;
    }
    runAutomatic();
    askNext();
}

bool SelfCheckDialog::prepareCopy(QString *error)
{
    AppSettings *settings = AppSettings::instance();
    const QString liveDb = settings->dbPath();
    m_liveDb = liveDb;
    // Copies a crashed earlier run left behind hold shop data: remove them.
    const QDir temp(QDir::tempPath());
    for (const QString &old : temp.entryList({ QStringLiteral("laideal_comprobacion_*") }, QDir::Dirs | QDir::NoDotAndDotDot))
        if (temp.filePath(old) != m_work.path())
            QDir(temp.filePath(old)).removeRecursively();
    const QString backup = SelfCheck::newestBackup(QDir(QFileInfo(liveDb).absolutePath()).filePath("backups"));
    if (liveDb.isEmpty() || backup.isEmpty()) {
        *error = tr("no hay ninguna copia de seguridad junto a la base de datos: abra el programa una vez "
                    "(hace una al empezar el día) y repita la comprobación");
        return false;
    }
    QFile live(m_liveSettingsFile);
    const QString dbCopy = m_work.filePath("laideal_comprobacion.db");
    const QString settingsCopy = m_work.filePath("ajustes_comprobacion.json");
    if (!m_work.isValid() || !QFile::copy(backup, dbCopy) || !live.open(QIODevice::ReadOnly)) {
        *error = tr("no se pudo copiar la copia de seguridad o los ajustes a una carpeta temporal");
        return false;
    }
    const QByteArray json = SelfCheck::testSettings(live.readAll(), dbCopy);
    QFile out(settingsCopy);
    if (json.isEmpty() || !out.open(QIODevice::WriteOnly) || out.write(json) != json.size()) {
        *error = tr("no se pudieron preparar los ajustes de la comprobación");
        return false;
    }
    out.close();
    // From here every read and any save goes to the copies.
    settings->loadFrom(settingsCopy);
    setDbPath(dbCopy);
    m_db = QSqlDatabase::addDatabase("QSQLITE", kConnection);
    m_db.setDatabaseName(dbCopy);
    qDebug() << "SelfCheckDialog: checking a copy of" << QFileInfo(backup).fileName() << "in" << m_work.path();
    return true;
}

void SelfCheckDialog::runAutomatic()
{
    const int m1 = addCheck(QStringLiteral("M1"), QString());
    m_checks[m1] = SelfCheck::checkSchema(m_db);
    showCheck(m1);

    const int m1c = addCheck(QStringLiteral("M1c"), QString());
    m_checks[m1c] = SelfCheck::checkChain(m_db);
    showCheck(m1c);

    const int m2 = addCheck(QStringLiteral("M2"), QString());
    m_checks[m2] = SelfCheck::checkData(m_db, AppSettings::instance()->contabilidadPath(), m_facts);
    showCheck(m2);

    if (m_facts.closedQuarter > 0) {
        const int i = addCheck(QStringLiteral("M2b"), tr("Cifras del último trimestre cerrado"));
        const QString quarter = tr("trimestre %1 de %2").arg(m_facts.closedQuarter).arg(m_facts.closedYear);
        m_questions.append({ i,
            tr("¿Coinciden estas cifras con las del informe de contabilidad presentado del %1?").arg(quarter),
            tr("Ingresos: %1 € (IVA %2 €) · Gastos: %3 € · Liquidación de IVA: %4 €")
                .arg(displayMoney(m_facts.income), displayMoney(m_facts.incomeIva),
                     displayMoney(m_facts.expenses), displayMoney(m_facts.ivaResult))
                + (m_facts.reportFile.isEmpty() ? tr("\nNo se ha encontrado el PDF del informe en la carpeta de Contabilidad.")
                                                : QString()),
            m_facts.reportFile });
    }

    const int m3 = addCheck(QStringLiteral("M3"), tr("Impresora: recibo y factura con QR"));
    if (!AppSettings::instance()->enablePrinting()) {
        m_checks[m3].status = Check::Skip;
        m_checks[m3].reason = tr("la impresión está desactivada en Configuración");
        showCheck(m3);
    } else if (m_facts.printableTicket.isEmpty()
               || !SelfCheck::invoiceStillSent(m_liveDb, m_facts.printableTicket, m_facts.printableSeq)) {
        m_checks[m3].status = Check::Skip;
        m_checks[m3].reason = tr("no hay ningún ticket con factura enviada a la AEAT para imprimir");
        showCheck(m3);
    } else {
        m_questions.append({ m3,
            tr("Se han impreso el recibo y la factura del ticket %1 como prueba (no los entregue a un cliente; "
               "destrúyalos). ¿Han salido los dos, cortados, con los acentos bien, y al escanear el QR de la "
               "factura con el móvil se abre la página de la AEAT?").arg(m_facts.printableTicket),
            QString(), QString() });
    }

    const int m5 = addCheck(QStringLiteral("M5"), tr("Instalador: novedades en su idioma"));
    m_questions.append({ m5,
        tr("Al instalar esta versión, ¿la página de información mostraba las novedades en su idioma y con los "
           "acentos bien?"),
        QString(), QString() });
}

void SelfCheckDialog::printSample()
{
    Imprimir recibo(m_db, this);
    recibo.isRecibo = true;
    recibo.le_n_ticket->setText(m_facts.printableTicket);
    recibo.getTicketInfo();
    recibo.buildTicket(true, true);
    const bool reciboPrinted = recibo.printTicket();

    Imprimir factura(m_db, this);
    factura.isRecibo = false;
    factura.invoiceSeq = m_facts.printableSeq;
    // The QR from the URL stored with the invoice: drawn here, nothing asks AEAT.
    factura.qrCode = QPixmap::fromImage(AeatQr::image(m_facts.printableQrUrl, 5));   // as the direct client draws it
    factura.le_n_ticket->setText(m_facts.printableTicket);
    factura.getTicketInfo();
    factura.buildTicket(true, false);
    const bool facturaPrinted = factura.printTicket();
    if (!reciboPrinted || !facturaPrinted)
        m_result->setText(UiKit::warnHtml(tr("La impresora no ha respondido a todo: compruebe que está encendida "
                                             "y conteste a la pregunta según lo que haya salido.")));
}

void SelfCheckDialog::askNext()
{
    ++m_current;
    if (m_current >= m_questions.size()) {
        finish();
        return;
    }
    const Question &question = m_questions[m_current];
    if (m_checks[question.check].id == QLatin1String("M3"))
        printSample();
    m_lblQuestion->setText(question.text);
    m_lblInfo->setText(question.info);
    m_lblInfo->setVisible(!question.info.isEmpty());
    m_btnOpen->setVisible(!question.openFile.isEmpty());
    m_leNote->clear();
    m_grpQuestion->setVisible(true);
    m_btnYes->setFocus();
}

void SelfCheckDialog::answer(bool yes)
{
    if (m_current < 0 || m_current >= m_questions.size())
        return;
    Check &c = m_checks[m_questions[m_current].check];
    c.status = yes ? Check::Pass : Check::Fail;
    c.reason = yes ? tr("confirmado por el usuario") : tr("el usuario indica que no es correcto");
    c.note = m_leNote->text().trimmed();
    showCheck(m_questions[m_current].check);
    askNext();
}

void SelfCheckDialog::finish()
{
    m_grpQuestion->setVisible(false);
    cleanUp();
    const QDateTime now = QDateTime::currentDateTime();
    QDir().mkpath(m_reportFolder);
    m_reportFile = QDir(m_reportFolder).filePath(
        QStringLiteral("comprobacion_laideal_%1_%2.txt").arg(kVersion, now.toString("yyyyMMdd_HHmm")));
    QFile file(m_reportFile);
    const QByteArray text = SelfCheck::reportText(m_checks, kVersion, now).toUtf8();
    const bool written = file.open(QIODevice::WriteOnly | QIODevice::Text) && file.write(text) == text.size();
    file.close();
    bool allOk = true;
    for (const Check &c : m_checks)
        allOk = allOk && (c.status == Check::Pass || c.status == Check::Skip);
    qDebug() << "SelfCheckDialog: finished, all ok =" << allOk << "report" << m_reportFile << "written" << written;
    const QString outcome = allOk ? UiKit::okHtml(tr("Comprobación terminada: todo correcto."))
                                  : UiKit::errorHtml(tr("Comprobación terminada con fallos."));
    m_result->setText(outcome + "<br>" + tr("Las copias de los datos se han borrado.") + "<br>"
                      + (written ? tr("Envíe el informe: ") + UiKit::fileLinkHtml(m_reportFile, QFileInfo(m_reportFile).fileName())
                                 : UiKit::errorHtml(tr("No se pudo guardar el informe en el escritorio."))));
}

void SelfCheckDialog::cleanUp()
{
    if (m_db.isValid()) {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(kConnection);
    }
    if (m_work.isValid())
        m_work.remove();
}
