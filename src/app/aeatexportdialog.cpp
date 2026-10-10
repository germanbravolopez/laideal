#include "aeatexportdialog.h"
#include "aeatexport.h"
#include "appsettings.h"
#include "sql_lite.h"
#include "uikit.h"

#include <QDateEdit>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

AeatExportDialog::AeatExportDialog(const QSqlDatabase &database, QWidget *parent)
    : QDialog(parent), db(database)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::WindowModal);
    UiKit::setUpDialog(this, "Exportar registros AEAT (XML)", 560);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(UiKit::introPanel(
        "Guarda en un archivo XML las facturas registradas en AEAT en un periodo (emitidas o "
        "anuladas en él), con el registro de AEAT de cada una, para entregarlo a Hacienda si lo pide."));

    QGroupBox *grpPeriod = new QGroupBox("Periodo");
    QFormLayout *periodForm = new QFormLayout(grpPeriod);
    periodForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    m_deFrom = UiKit::dateEdit(QDate::currentDate().addMonths(-3), "deFrom");   // stable names for the e2e bench
    m_deTo   = UiKit::dateEdit(QDate::currentDate(), "deTo");
    periodForm->addRow("Desde:", m_deFrom);
    periodForm->addRow("Hasta:", m_deTo);
    layout->addWidget(grpPeriod);

    QGroupBox *grpFile = new QGroupBox("Archivo");
    QHBoxLayout *fileRow = new QHBoxLayout(grpFile);
    m_leFile = new QLineEdit();
    m_leFile->setObjectName("leFile");
    fileRow->addWidget(m_leFile, 1);
    QPushButton *btnChoose = UiKit::secondaryButton("Elegir…", "btnChoose");
    fileRow->addWidget(btnChoose);
    layout->addWidget(grpFile);

    QHBoxLayout *actions = new QHBoxLayout();
    actions->addStretch();
    QPushButton *btnExport = UiKit::primaryButton("Exportar registros", "btnExport");
    actions->addWidget(btnExport);
    layout->addLayout(actions);
    m_lblResult = new UiKit::ResultPanel("Elija el periodo y pulse \"Exportar registros\".");
    layout->addWidget(m_lblResult);
    layout->addLayout(UiKit::closeRow(this));

    suggestFileName();
    connect(m_deFrom, &QDateEdit::dateChanged, this, &AeatExportDialog::suggestFileName);
    connect(m_deTo,   &QDateEdit::dateChanged, this, &AeatExportDialog::suggestFileName);
    connect(m_leFile, &QLineEdit::textEdited, this, [this]() { m_fileChosen = true; });
    connect(btnChoose, &QPushButton::clicked, this, &AeatExportDialog::onChooseFileClicked);
    connect(btnExport, &QPushButton::clicked, this, &AeatExportDialog::onExportClicked);
}

void AeatExportDialog::suggestFileName()
{
    if (m_fileChosen)
        return;
    m_leFile->setText(QDir(AppSettings::instance()->reportsRoot()).filePath(
        QString("AEAT/aeat_registros_%1_%2.xml")
            .arg(m_deFrom->date().toString("yyyyMMdd"), m_deTo->date().toString("yyyyMMdd"))));
}

void AeatExportDialog::onChooseFileClicked()
{
    const QString file = QFileDialog::getSaveFileName(this, "Guardar archivo de registros AEAT",
                                                      m_leFile->text(), "XML (*.xml)");
    if (file.isEmpty())
        return;
    m_leFile->setText(file);
    m_fileChosen = true;
}

void AeatExportDialog::onExportClicked()
{
    const QDate from = m_deFrom->date();
    const QDate to   = m_deTo->date();
    const QString file = m_leFile->text().trimmed();
    if (from > to) {
        m_lblResult->setText(UiKit::errorHtml("La fecha 'Desde' debe ser anterior o igual a 'Hasta'."));
        return;
    }
    if (file.isEmpty()) {
        m_lblResult->setText(UiKit::errorHtml("Indique el archivo donde guardar los registros."));
        return;
    }

    // One record per invoice submitted to AEAT (payment event), any current estado:
    // Hacienda holds cancelled and rectified invoices too.
    QVector<AeatExportRecord> records;
    int undated = 0;
    if (!aeatExportRecords(db, from, to, records, &undated)) {
        m_lblResult->setText(UiKit::errorHtml("Error al consultar la base de datos.")
                             + "<br>No se ha creado el archivo.");
        return;
    }
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lblResult->setText(UiKit::errorHtml("No se pudo abrir el archivo para escribir.")
                             + "<br>" + file.toHtmlEscaped());
        return;
    }
    const int count = writeAeatExportXml(&out, records, from, to,
                                         AppSettings::instance()->verifactuNif(),
                                         AppSettings::instance()->verifactuName());
    out.close();
    if (out.error() != QFileDevice::NoError) {
        qWarning() << "Exportar registros AEAT: write failed -" << out.errorString();
        m_lblResult->setText(UiKit::errorHtml("Error al escribir el archivo.") + "<br>"
                             + out.errorString().toHtmlEscaped());
        return;
    }

    const QString period = from.toString("dd-MM-yyyy") + " a " + to.toString("dd-MM-yyyy");
    QString message = count == 0
        ? UiKit::warnHtml("No hay registros enviados a AEAT del " + period + ".") + "<br>El archivo se ha creado vacío."
        : UiKit::okHtml(QString("%1 registros exportados (%2).").arg(count).arg(period));
    message += "<br>" + UiKit::fileLinkHtml(file, "Archivo");
    if (undated > 0)
        message += "<br>" + UiKit::errorHtml(QString("Atención: %1 factura(s) enviadas a AEAT no tienen una fecha "
                                                     "de pago válida y no aparecen en ningún periodo. Revise el "
                                                     "log y contacte con soporte.").arg(undated));
    m_lblResult->setText(message);
}
