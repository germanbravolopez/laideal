#include "contabilidad.h"
#include "sql_lite.h"
#include "qprinter.h"
#include "appsettings.h"
#include "reporthtml.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Result panel messages, coloured like the other Verifactu / accounting dialogs.
QString okHtml(const QString &text)      { return "<b style='color:green'>" + text + "</b>"; }
QString warnHtml(const QString &text)    { return "<b style='color:#b26a00'>" + text + "</b>"; }
QString errorHtml(const QString &text)   { return "<b style='color:red'>" + text + "</b>"; }

QString fileLinkHtml(const QString &file)
{
    return "PDF: <a href='" + QUrl::fromLocalFile(file).toString() + "'>"
           + QFileInfo(file).fileName().toHtmlEscaped() + "</a>";
}

} // namespace

Contabilidad::Contabilidad(const QSqlDatabase &database, QWidget *parent) :
    QDialog(parent),
    db(database)
{
    setAttribute(Qt::WA_DeleteOnClose); // self-delete on close: callers show() it and drop the pointer
    // Window-modal over MainWindow and kept open after each report: it can only be
    // closed (Cerrar / the title bar), not minimised or sent behind the main window.
    setWindowModality(Qt::WindowModal);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowFlag(Qt::WindowMinimizeButtonHint, false);
    setMinimumWidth(520);
    buildUi();
    initialSettings();
}

Contabilidad::~Contabilidad() = default;

void Contabilidad::buildUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    m_lblIntro = new QLabel();
    m_lblIntro->setWordWrap(true);
    m_lblIntro->setFrameShape(QFrame::StyledPanel);
    m_lblIntro->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(m_lblIntro);

    // Period -------------------------------------------------------------
    QGroupBox *grpPeriod = new QGroupBox("Periodo");
    QFormLayout *periodForm = new QFormLayout(grpPeriod);
    periodForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    m_cbConfig = new QComboBox();
    m_cbConfig->setMinimumWidth(180);
    m_cbConfig->setObjectName("cbConfig");   // stable names for the e2e test bench
    m_cbConfig->addItems({ "Mensual", "Trimestral", "Anual" });   // ConfigMode order
    periodForm->addRow("Tipo de informe:", m_cbConfig);
    m_sbYear = new QSpinBox();
    m_sbYear->setObjectName("sbYear");
    m_sbYear->setMinimumWidth(110);
    periodForm->addRow("Año:", m_sbYear);
    m_lblPeriod = new QLabel("Trimestre:");
    m_sbPeriod = new QSpinBox();
    m_sbPeriod->setObjectName("sbPeriod");
    m_sbPeriod->setMinimumWidth(110);
    periodForm->addRow(m_lblPeriod, m_sbPeriod);
    layout->addWidget(grpPeriod);

    // Options ------------------------------------------------------------
    QGroupBox *grpOptions = new QGroupBox("Opciones");
    QVBoxLayout *optionsLayout = new QVBoxLayout(grpOptions);
    m_chkLock = new QCheckBox("Bloquear el trimestre al generar la contabilidad");
    m_chkLock->setObjectName("chkLock");
    m_chkLock->setToolTip("Una vez bloqueado, los ingresos y gastos del trimestre no se pueden modificar "
                          "hasta revertir la contabilidad. Solo en el informe trimestral.");
    optionsLayout->addWidget(m_chkLock);
    m_chkDetail = new QCheckBox("Incluir el detalle de tickets y facturas");
    m_chkDetail->setObjectName("chkDetail");
    m_chkDetail->setToolTip("Añade la lista de tickets y facturas de cada importe. Se guarda en un "
                            "PDF aparte, terminado en \"_detalle\".");
    optionsLayout->addWidget(m_chkDetail);
    layout->addWidget(grpOptions);

    // Actions + result ---------------------------------------------------
    QHBoxLayout *actions = new QHBoxLayout();
    m_btnCheckLock = new QPushButton("Comprobar bloqueo");
    m_btnCheckLock->setObjectName("btnCheckLock");
    m_btnCheckLock->setAutoDefault(false);
    actions->addWidget(m_btnCheckLock);
    actions->addStretch();
    m_btnGenerate = new QPushButton();
    m_btnGenerate->setObjectName("btnGenerate");
    m_btnGenerate->setDefault(true);
    m_btnGenerate->setMinimumWidth(220);
    QFont primary = m_btnGenerate->font();
    primary.setBold(true);
    m_btnGenerate->setFont(primary);
    actions->addWidget(m_btnGenerate);
    layout->addLayout(actions);

    m_lblResult = new QLabel();
    m_lblResult->setObjectName("lblResult");
    m_lblResult->setWordWrap(true);
    m_lblResult->setFrameShape(QFrame::StyledPanel);
    m_lblResult->setContentsMargins(8, 8, 8, 8);
    m_lblResult->setMinimumHeight(64);
    m_lblResult->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_lblResult->setTextFormat(Qt::RichText);
    m_lblResult->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_lblResult->setOpenExternalLinks(true);
    layout->addWidget(m_lblResult);

    QPushButton *btnClose = new QPushButton("Cerrar");
    btnClose->setObjectName("btnClose");
    btnClose->setAutoDefault(false);
    layout->addWidget(btnClose, 0, Qt::AlignRight);

    connect(m_cbConfig, qOverload<int>(&QComboBox::currentIndexChanged), this, &Contabilidad::onConfigChanged);
    connect(m_btnCheckLock, &QPushButton::clicked, this, &Contabilidad::onCheckLockClicked);
    connect(m_btnGenerate,  &QPushButton::clicked, this, &Contabilidad::onGenerateClicked);
    connect(btnClose,       &QPushButton::clicked, this, &QDialog::close);
}

Contabilidad::ConfigMode Contabilidad::currentMode() const
{
    return static_cast<ConfigMode>(m_cbConfig->currentIndex());
}

void Contabilidad::initialSettings()
{
    // Lower bound of the year selector taken from the oldest income record
    // rather than a hardcoded year, so the range tracks the actual data.
    // Use fecha_recepcion, not fecha_pago: every ingresos row has a reception
    // date, whereas fecha_pago is empty on unpaid rows and SQLite MIN() of a
    // column containing '' returns '' (-> 0), which would collapse the range
    // to the current year and block selecting/reverting prior-year accounting.
    const int firstYear = readMaxNMinYearInColumnFromTable(db, false, "fecha_recepcion", "ingresos");
    const int currentYear = QDate::currentDate().year();
    m_sbYear->setRange(firstYear > 0 ? firstYear : currentYear, currentYear);
    m_sbPeriod->setRange(1, 4);
    resetAllContents();
}

void Contabilidad::resetAllContents()
{
    setWindowTitle(revertirOn ? "Revertir contabilidad" : "Generar contabilidad");
    m_lblIntro->setText(revertirOn
        ? "Desbloquea un trimestre ya contabilizado para que sus ingresos y gastos se puedan "
          "volver a modificar. No se genera ningún informe."
        : "Genera el informe de contabilidad del periodo en PDF. El informe trimestral puede "
          "además bloquear el trimestre, como cierre tras la declaración de impuestos.");
    m_cbConfig->setCurrentIndex(Trimestral);
    m_cbConfig->setDisabled(revertirOn);   // reverting is always quarterly
    m_sbYear->setValue(QDate::currentDate().year());
    m_chkLock->setChecked(false);
    m_chkLock->setEnabled(lockOptionAvailable(currentMode(), revertirOn));
    m_chkDetail->setChecked(false);
    m_chkDetail->setEnabled(!revertirOn);  // reverting writes no report
    m_btnGenerate->setText(revertirOn ? "Revertir contabilidad" : "Generar contabilidad");
    m_lblResult->setText("Seleccione el periodo y pulse \"" + m_btnGenerate->text() + "\".");
}

static bool s_openGeneratedReports = true;

void Contabilidad::setOpenGeneratedReports(bool open)
{
    s_openGeneratedReports = open;
}

static QString s_lastReportHtml;

QString Contabilidad::lastReportHtml()
{
    return s_lastReportHtml;
}

bool Contabilidad::lockOptionAvailable(ConfigMode mode, bool reverting)
{
    return mode == Trimestral && !reverting;
}

int Contabilidad::combinedLockState(int ingresosLock, int gastosLock)
{
    if (ingresosLock == 2 && gastosLock == 2)
        return 2;
    return (ingresosLock == 1 || gastosLock == 1) ? 1 : 0;
}

QString Contabilidad::reportRelativePath(ConfigMode mode, int unit, int year, bool withDetail)
{
    const QString suffix = withDetail ? QStringLiteral("_detalle.pdf") : QStringLiteral(".pdf");
    const QString y = QString::number(year), u = QString::number(unit);
    switch (mode) {
    case Mensual:    return "/Mensual/reporte_mensual_" + y + "_" + u + suffix;
    case Trimestral: return "/contabilidad_trimestral_" + y + "_" + u + suffix;
    case Anual:      return "/Anual/reporte_anual_" + y + suffix;
    }
    return QString();
}

QString Contabilidad::lockStatusMessage(QSqlDatabase &db, ConfigMode mode, int unit, int year)
{
    const auto quarterLine = [&db, year](int q) {
        const QString name = "El trimestre " + QString::number(q) + " de " + QString::number(year);
        switch (combinedLockState(readLockForQuarter(db, "ingresos", q, year),
                                  readLockForQuarter(db, "gastos", q, year))) {
        case 1:  return name + " está bloqueado (contabilidad realizada).";
        case 0:  return name + " no está bloqueado.";
        default: return name + " no tiene registros de ingresos ni de gastos.";
        }
    };
    if (mode == Trimestral)
        return quarterLine(unit);
    if (mode == Mensual) {
        const int q = (unit - 1) / 3 + 1;
        return "El mes " + QString::number(unit) + " de " + QString::number(year)
               + " pertenece al trimestre " + QString::number(q) + ", que "
               + (quarterIsClosed(db, QDate(year, unit, 1)) ? "está bloqueado." : "no está bloqueado.");
    }
    QStringList lines;
    for (int q = 1; q <= 4; ++q)
        lines << quarterLine(q);
    return lines.join('\n');
}

void Contabilidad::onCheckLockClicked()
{
    const int unit = currentMode() == Anual ? 0 : m_sbPeriod->value();
    const QString message = lockStatusMessage(db, currentMode(), unit, m_sbYear->value());
    qDebug() << "Contabilidad::onCheckLockClicked:" << message;
    m_lblResult->setText(message.toHtmlEscaped().replace('\n', "<br>"));
}

void Contabilidad::onGenerateClicked()
{
    const int trim = m_sbPeriod->value();
    const int year = m_sbYear->value();
    const QString quarterName = "del trimestre " + QString::number(trim) + " de " + QString::number(year);
    m_lblResult->setText(revertirOn ? "Revirtiendo..." : "Generando el informe...");
    QString message;
    int invalidAmounts = 0;
    QString file;

    if (currentMode() != Trimestral) {
        file = generateContabilidad(invalidAmounts);
        message = okHtml("Informe generado.");
    } else {
        // Read the lock across the whole quarter, not just its last month: a quarter
        // with income only in its first months would otherwise read as "no data".
        const int editLock = combinedLockState(readLockForQuarter(db, "ingresos", trim, year),
                                               readLockForQuarter(db, "gastos", trim, year));
        switch (editLock) {
        case 0:     // not done yet
            if (revertirOn) {
                qDebug() << "(Revertir) Contabilidad: trim" << trim << "year" << year << "was not done (editLock = 0)";
                message = warnHtml("La contabilidad " + quarterName + " no estaba realizada: no hay nada que revertir.");
            } else {
                file = generateContabilidad(invalidAmounts);
                const bool lock = m_chkLock->isChecked();
                if (lock)
                    updateLock();
                qDebug() << "Contabilidad: trim" << trim << "year" << year << "generated, lock =" << lock;
                message = okHtml("Contabilidad " + quarterName + " realizada.") + "<br>"
                          + (lock ? "El trimestre se ha bloqueado: sus ingresos y gastos ya no se pueden modificar."
                                  : "El trimestre no se ha bloqueado: se pueden realizar modificaciones posteriores.");
            }
            break;
        case 1:     // done (locked)
            if (revertirOn) {
                updateLock();
                qDebug() << "(Revertir) Contabilidad: trim" << trim << "year" << year << "reverted (editLock = 0)";
                message = okHtml("Contabilidad " + quarterName + " revertida.") + "<br>"
                          + "El trimestre vuelve a estar abierto.";
            } else {
                file = generateContabilidad(invalidAmounts);
                qDebug() << "Contabilidad: trim" << trim << "year" << year << "already locked, report regenerated";
                message = okHtml("La contabilidad " + quarterName + " ya estaba realizada (trimestre bloqueado).")
                          + "<br>Documentación generada de nuevo.";
            }
            break;
        default:
            qWarning() << "Contabilidad: no ingresos nor gastos for trim" << trim << "year" << year;
            message = warnHtml("No hay registros de ingresos ni de gastos " + quarterName + ".");
            break;
        }
    }
    if (!file.isEmpty())
        message += "<br>" + fileLinkHtml(file);
    if (invalidAmounts > 0)
        message += "<br>" + errorHtml(QString("Hay %1 importe(s) guardados con ',' como separador decimal: "
                                              "no se han sumado y aparecen marcados en el detalle. Utilice la "
                                              "herramienta de limpiado de importes decimales.").arg(invalidAmounts));
    m_lblResult->setText(message);
}

void Contabilidad::onConfigChanged()
{
    // Only a quarterly report can close the books; untick so a hidden choice never applies.
    const bool lockAvailable = lockOptionAvailable(currentMode(), revertirOn);
    if (!lockAvailable)
        m_chkLock->setChecked(false);
    m_chkLock->setEnabled(lockAvailable);
    const bool monthly = currentMode() == Mensual;
    m_lblPeriod->setText(monthly ? "Mes:" : "Trimestre:");
    m_sbPeriod->setRange(1, monthly ? 12 : 4);
    m_lblPeriod->setVisible(currentMode() != Anual);
    m_sbPeriod->setVisible(currentMode() != Anual);
}

QString Contabilidad::generateContabilidad(int &invalidAmounts)
{
    const int year = m_sbYear->value();
    const double ivaRate = AppSettings::ivaRate();
    const bool withDetail = m_chkDetail->isChecked();
    const QString detailTitle = withDetail ? QStringLiteral(" · Detalle") : QString();
    QString contabilidadHtml;
    invalidAmounts = 0;

    if (currentMode() == Trimestral || currentMode() == Mensual) {
        QDate start, endExclusive;
        periodRange(0, start, endExclusive);
        // One fetch per table: the summary and the detail tables use the same rows.
        const QVector<IncomeTicketDetail> income = incomeTicketsBetweenDates(db, start, endExclusive);
        const QVector<ExpenseDetail> expenses = expensesBetweenDates(db, start, endExclusive);
        const QVector<RegularizationDetail> regs = regularizationsBetweenDates(db, start, endExclusive);
        invalidAmounts = invalidAmountCount(income, regs, expenses);

        QString title, subtitle = periodSubtitle(0);
        if (currentMode() == Trimestral) {
            title = "Contabilidad - Trimestre " + QString::number(m_sbPeriod->value()) + " · " + QString::number(year);
        }
        else {
            // Closing is quarterly: the month reads closed when its quarter is (even a
            // month without rows, which readLockForMonthAndYear would report as open).
            const bool cerrada = quarterIsClosed(db, QDate(year, m_sbPeriod->value(), 1));
            title = "Reporte Mensual - Mes " + QString::number(m_sbPeriod->value()) + " · " + QString::number(year);
            subtitle += cerrada ? " · Contabilidad cerrada" : " · Contabilidad no cerrada";
        }
        contabilidadHtml = ReportHtml::documentOpen(title + detailTitle, subtitle)
                + renderSection(figuresFromDetails(income, regs, expenses, ivaRate, start, endExclusive),
                                "Resumen del periodo")
                + (withDetail ? renderDetailTables("Detalle del periodo", income, regs, expenses, ivaRate) : QString())
                + ReportHtml::documentClose();
    }
    else {
        contabilidadHtml = ReportHtml::documentOpen("Reporte Anual - " + QString::number(year) + detailTitle);
        // One scan per table for the whole year, bucketed by quarter; each quarter's
        // figures and detail tables come from the same rows.
        const QuarterlyDetails details = annualDetailsByQuarter(db, year);
        PeriodFigures annual;
        for (int trim = 1; trim < 5; trim++) {
            const int i = trim - 1;
            const bool cerrada = combinedLockState(readLockForQuarter(db, "ingresos", trim, year),
                                                   readLockForQuarter(db, "gastos", trim, year)) == 1;
            QDate quarterStart, quarterEnd;
            periodRangeFor(Anual, trim, year, quarterStart, quarterEnd);
            const PeriodFigures f = figuresFromDetails(details.income[i], details.regularizations[i],
                                                       details.expenses[i], ivaRate, quarterStart, quarterEnd);
            annual.accumulate(f);
            invalidAmounts += invalidAmountCount(details.income[i], details.regularizations[i], details.expenses[i]);
            contabilidadHtml += "<h2>Trimestre " + QString::number(trim)
                    + (cerrada ? " · Contabilidad cerrada" : " · Contabilidad no cerrada") + "</h2>"
                    + renderSection(f, "Resumen del trimestre");
        }
        annual.ingTickets = yearTicketCount(details, year);   // distinct over the year, not the quarterly sum
        contabilidadHtml += "<h2>Resumen anual consolidado</h2>"
                + createHtmlSummary(annual, "Total a&ntilde;o " + QString::number(year));
        for (int trim = 1; withDetail && trim < 5; trim++)
            contabilidadHtml += renderDetailTables("Detalle del trimestre " + QString::number(trim),
                                                   details.income[trim - 1], details.regularizations[trim - 1],
                                                   details.expenses[trim - 1], ivaRate);
        contabilidadHtml += ReportHtml::documentClose();
    }
    if (invalidAmounts > 0)
        qCritical() << "Contabilidad::generateContabilidad:" << invalidAmounts << "comma-decimal importe(s) not summed";
    const QString file = AppSettings::instance()->contabilidadPath()
            + reportRelativePath(currentMode(), currentMode() == Anual ? 0 : m_sbPeriod->value(), year, withDetail);
    QDir().mkpath(QFileInfo(file).absolutePath());
    writeHtml(file, contabilidadHtml);
    return file;
}

void Contabilidad::periodRangeFor(ConfigMode mode, int unit, int year, QDate &start, QDate &endExclusive)
{
    if (mode == Mensual) {
        const int month = unit;
        start.setDate(year, month, 1);
        if (month == 12)
            endExclusive.setDate(year + 1, 1, 1);
        else
            endExclusive.setDate(year, month + 1, 1);
        return;
    }

    switch (unit) { // quarter 1-4
    case 1: start.setDate(year, 1, 1);  endExclusive.setDate(year, 4, 1);      break;
    case 2: start.setDate(year, 4, 1);  endExclusive.setDate(year, 7, 1);      break;
    case 3: start.setDate(year, 7, 1);  endExclusive.setDate(year, 10, 1);     break;
    case 4: start.setDate(year, 10, 1); endExclusive.setDate(year + 1, 1, 1);  break;
    default: break;
    }
}

void Contabilidad::periodRange(int trimForYearConfig, QDate &start, QDate &endExclusive)
{
    const ConfigMode mode = currentMode();
    // Mensual reads the month from sb_trim; Trimestral the quarter from sb_trim;
    // Anual iterates quarters, so the caller passes the quarter in trimForYearConfig.
    const int unit = (mode == Anual) ? trimForYearConfig : m_sbPeriod->value();
    periodRangeFor(mode, unit, m_sbYear->value(), start, endExclusive);
}

QString Contabilidad::periodSubtitle(int trimForYearConfig)
{
    QDate start, endExclusive;
    periodRange(trimForYearConfig, start, endExclusive);
    return "Periodo: " + start.toString("dd-MM-yyyy") + " a " + endExclusive.addDays(-1).toString("dd-MM-yyyy");
}

Contabilidad::PeriodFigures Contabilidad::figuresFromTotals(
    double ingImporte, int ingTickets, double gas10Importe, double gas21Importe,
    double gasNiImporte, int gasFacturas, double ivaRate)
{
    PeriodFigures f;
    f.ingImporte = ingImporte;
    f.ingBase    = f.ingImporte / (1.0 + ivaRate / 100.0);
    f.ingIva     = f.ingImporte - f.ingBase;

    f.gas10Importe = gas10Importe;
    f.gas10Base    = f.gas10Importe / 1.10;
    f.gas10Iva     = f.gas10Importe - f.gas10Base;
    f.gas21Importe = gas21Importe;
    f.gas21Base    = f.gas21Importe / 1.21;
    f.gas21Iva     = f.gas21Importe - f.gas21Base;
    f.gasNiImporte = gasNiImporte;     // sin IVA: base == importe

    f.ingTickets  = ingTickets;
    f.gasFacturas = gasFacturas;
    return f;
}

Contabilidad::PeriodFigures Contabilidad::figuresFromDetails(const QVector<IncomeTicketDetail> &income,
                                                             const QVector<RegularizationDetail> &regularizations,
                                                             const QVector<ExpenseDetail> &expenses,
                                                             double ivaRate,
                                                             const QDate &periodStart, const QDate &periodEnd)
{
    double ingImporte = 0.0, regularizacion = 0.0, gas10 = 0.0, gas21 = 0.0, gasNi = 0.0;
    for (const IncomeTicketDetail &t : income)
        ingImporte += t.importe;
    for (const RegularizationDetail &r : regularizations)
        regularizacion += r.importe;
    for (const ExpenseDetail &e : expenses) {
        if (e.iva == 10)      gas10 += e.importe;
        else if (e.iva == 21) gas21 += e.importe;
        else if (e.iva == 0)  gasNi += e.importe;
    }
    PeriodFigures f = figuresFromTotals(ingImporte - regularizacion,
                                        netTicketCount(income, regularizations, periodStart, periodEnd),
                                        gas10, gas21, gasNi, expenses.size(), ivaRate);
    f.ingRegularizacion = regularizacion;
    return f;
}

int Contabilidad::netTicketCount(const QVector<IncomeTicketDetail> &income,
                                 const QVector<RegularizationDetail> &regularizations,
                                 const QDate &periodStart, const QDate &periodEnd)
{
    // Per ticket: income minus its regularisations. Count only a positive net (1 cent
    // tolerance): paid-and-cancelled nets to 0, and a by-differences credit note is
    // negative. A ticket whose amounts are all comma-flagged (net 0 but a real sale)
    // still counts unless it was regularised.
    QHash<QString, double> net;
    QSet<QString> flagged;
    for (const IncomeTicketDetail &t : income) {
        net[t.nRecibo] += t.importe;
        if (t.invalidAmounts > 0)
            flagged.insert(t.nRecibo);
    }
    QSet<QString> regularized;
    for (const RegularizationDetail &r : regularizations) {
        const QDate paid = QDate::fromString(r.fechaPago, QStringLiteral("dd-MM-yyyy"));
        if (!paid.isValid() || paid < periodStart || paid >= periodEnd)
            continue;                       // an earlier period's payment: does not offset this period's sales
        if (net.contains(r.nRecibo))
            net[r.nRecibo] -= r.importe;
        regularized.insert(r.nRecibo);
    }
    int n = 0;
    for (auto it = net.cbegin(); it != net.cend(); ++it)
        if (it.value() > 0.005 || (flagged.contains(it.key()) && !regularized.contains(it.key())))
            n++;
    return n;
}

int Contabilidad::yearTicketCount(const QuarterlyDetails &details, int year)
{
    QVector<IncomeTicketDetail> income;
    QVector<RegularizationDetail> regularizations;
    for (const QVector<IncomeTicketDetail> &quarter : details.income)
        income += quarter;
    for (const QVector<RegularizationDetail> &quarter : details.regularizations)
        regularizations += quarter;
    return netTicketCount(income, regularizations, QDate(year, 1, 1), QDate(year + 1, 1, 1));
}

int Contabilidad::invalidAmountCount(const QVector<IncomeTicketDetail> &income,
                                     const QVector<RegularizationDetail> &regularizations,
                                     const QVector<ExpenseDetail> &expenses)
{
    int n = 0;
    for (const IncomeTicketDetail &t : income)
        n += t.invalidAmounts;
    for (const RegularizationDetail &r : regularizations)
        n += r.invalidAmounts;
    for (const ExpenseDetail &e : expenses)
        n += e.invalidAmount ? 1 : 0;
    return n;
}

void Contabilidad::updateLock()
{
    // Doing the contabilidad locks the period (edit_lock = 1); reverting unlocks it (0).
    const int lockValue = revertirOn ? 0 : 1;
    const int year = m_sbYear->value();
    switch (m_sbPeriod->value()) {
    case 1:
        updateLockForMonth(db, lockValue, 1, year);
        updateLockForMonth(db, lockValue, 2, year);
        updateLockForMonth(db, lockValue, 3, year);
        break;
    case 2:
        updateLockForMonth(db, lockValue, 4, year);
        updateLockForMonth(db, lockValue, 5, year);
        updateLockForMonth(db, lockValue, 6, year);
        break;
    case 3:
        updateLockForMonth(db, lockValue, 7, year);
        updateLockForMonth(db, lockValue, 8, year);
        updateLockForMonth(db, lockValue, 9, year);
        break;
    case 4:
        updateLockForMonth(db, lockValue, 10, year);
        updateLockForMonth(db, lockValue, 11, year);
        updateLockForMonth(db, lockValue, 12, year);
        break;
    default:
        break;
    }
    qDebug() << "Contabilidad::updateLock: edit_lock set to" << lockValue
             << "for trim" << m_sbPeriod->value() << "year" << year;
}

void Contabilidad::writeHtml(QString filename,
                             QString html)
{
    s_lastReportHtml = html;
    QTextDocument document;
    document.setHtml(html);

    QPrinter printer(QPrinter::PrinterResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setPageSize(QPageSize::A4);
    printer.setOutputFileName(filename);
    printer.setPageMargins(QMarginsF(15, 15, 15, 15));

    document.print(&printer);

    if (s_openGeneratedReports)
        QDesktopServices::openUrl(QUrl::fromLocalFile(filename));
    //QDesktopServices::openUrl(QUrl::fromLocalFile(qApp->applicationDirPath() + "/docs/" + "nameof.pdf"));
}

QString Contabilidad::renderSection(const PeriodFigures &f, const QString &summaryHeading)
{
    return createHtmlTableIngresos(f) + createHtmlTableGastos(f) + createHtmlSummary(f, summaryHeading);
}

// Right-aligned currency cell.
static QString euroCell(double value)
{
    return "<td style='text-align:right;'>" + ReportHtml::formatEuro(value) + "</td>";
}

QString Contabilidad::createHtmlTableIngresos(const PeriodFigures &f)
{
    return
    "<h3>Ingresos</h3>"
    + ReportHtml::tableOpen(true) +
        "<tr><th>Concepto</th><th style='text-align:right;'>Importe</th></tr>"
        + (f.ingRegularizacion > 0.0
           ? "<tr><td>Ingresos del periodo (IVA incluido)</td>" + euroCell(f.ingImporte + f.ingRegularizacion) + "</tr>"
             "<tr><td>Anulaciones / rectificaciones del periodo</td>" + euroCell(-f.ingRegularizacion) + "</tr>"
           : QString()) +
        "<tr><td>Importe total (IVA incluido)</td>" + euroCell(f.ingImporte) + "</tr>"
        "<tr style='background-color:#f6f7f9;'><td>Base imponible</td>" + euroCell(f.ingBase) + "</tr>"
        "<tr><td>IVA repercutido</td>" + euroCell(f.ingIva) + "</tr>"
    "</table>";
}

QString Contabilidad::createHtmlTableGastos(const PeriodFigures &f)
{
    return
    "<h3>Gastos</h3>"
    + ReportHtml::tableOpen(true) +
        "<tr>"
            "<th>Concepto</th>"
            "<th style='text-align:right;'>IVA 21%</th>"
            "<th style='text-align:right;'>IVA 10%</th>"
            "<th style='text-align:right;'>Subtotal con IVA</th>"
            "<th style='text-align:right;'>Sin IVA</th>"
            "<th style='text-align:right;'>Total</th>"
        "</tr>"
        "<tr>"
            "<td>Importe</td>"
            + euroCell(f.gas21Importe) + euroCell(f.gas10Importe) + euroCell(f.gastosConIvaImporte())
            + euroCell(f.gasNiImporte) + euroCell(f.gastosImporteTotal()) +
        "</tr>"
        "<tr style='background-color:#f6f7f9;'>"
            "<td>Base imponible</td>"
            + euroCell(f.gas21Base) + euroCell(f.gas10Base) + euroCell(f.gastosConIvaBase())
            + euroCell(f.gasNiImporte) + euroCell(f.gastosBaseTotal()) +
        "</tr>"
        "<tr>"
            "<td>IVA soportado</td>"
            + euroCell(f.gas21Iva) + euroCell(f.gas10Iva) + euroCell(f.gastosIvaTotal())
            + "<td style='text-align:right;'>-</td>" + euroCell(f.gastosIvaTotal()) +
        "</tr>"
    "</table>";
}

QString Contabilidad::createHtmlSummary(const PeriodFigures &f, const QString &heading)
{
    const QString ivaLabel = f.resultadoIva() >= 0.0 ? "Resultado IVA (a ingresar)"
                                                      : "Resultado IVA (a compensar)";
    const QString resLabel = f.resultadoPeriodo() >= 0.0 ? "Resultado del periodo (beneficio)"
                                                         : "Resultado del periodo (p&eacute;rdida)";
    // Emphasised total rows: subtle accent background + bold value.
    const QString totalRow = "background-color:#d9e0e7; font-weight:bold;";

    return
    "<h3>" + heading + "</h3>"
    + ReportHtml::tableOpen(true) +
        "<tr><th>Liquidaci&oacute;n de IVA</th><th style='text-align:right;'>Importe</th></tr>"
        "<tr><td>IVA repercutido (ingresos)</td>" + euroCell(f.ingIva) + "</tr>"
        "<tr style='background-color:#f6f7f9;'><td>IVA soportado (gastos)</td>" + euroCell(f.gastosIvaTotal()) + "</tr>"
        "<tr style='" + totalRow + "'><td>" + ivaLabel + "</td>"
            "<td style='text-align:right; font-weight:bold;'>" + ReportHtml::formatEuro(f.resultadoIva()) + "</td></tr>"
        "<tr><td>Base ingresos</td>" + euroCell(f.ingBase) + "</tr>"
        "<tr style='background-color:#f6f7f9;'><td>Base gastos</td>" + euroCell(f.gastosBaseTotal()) + "</tr>"
        "<tr style='" + totalRow + "'><td>" + resLabel + "</td>"
            "<td style='text-align:right; font-weight:bold;'>" + ReportHtml::formatEuro(f.resultadoPeriodo()) + "</td></tr>"
        "<tr><td>N&uacute;mero de tickets (ingresos)</td><td style='text-align:right;'>" + QString::number(f.ingTickets) + "</td></tr>"
        "<tr style='background-color:#f6f7f9;'><td>N&uacute;mero de facturas (gastos)</td><td style='text-align:right;'>" + QString::number(f.gasFacturas) + "</td></tr>"
    "</table>";
}

QString Contabilidad::renderDetailTables(const QString &heading,
                                        const QVector<IncomeTicketDetail> &income,
                                        const QVector<RegularizationDetail> &regularizations,
                                        const QVector<ExpenseDetail> &expenses,
                                        double ivaRate)
{
    return "<h2>" + heading + "</h2>"
            + createHtmlDetailIngresos(income, ivaRate)
            + createHtmlDetailRegularizaciones(regularizations, ivaRate)
            + createHtmlDetailGastos(expenses);
}

// Footnote under each detail table: the per-row cells are rounded for display,
// while the total row sums the unrounded amounts (and equals the summary).
static const QString kRoundingNote = QStringLiteral(
    "<p><i>Base e IVA de cada l&iacute;nea redondeados al c&eacute;ntimo; los totales se calculan sin redondear.</i></p>");

// Zebra-striped row opener for the long, borderless detail tables.
static QString detailRowOpen(int index)
{
    return index % 2 == 0 ? QStringLiteral("<tr>") : QStringLiteral("<tr style='background-color:#f6f7f9;'>");
}

QString Contabilidad::createHtmlDetailIngresos(const QVector<IncomeTicketDetail> &tickets, double ivaRate)
{
    QString html = "<h3>Detalle de ingresos</h3>";
    if (tickets.isEmpty())
        return html + "<p>Sin tickets cobrados en el periodo.</p>";

    html += ReportHtml::tableOpen() +
            "<thead><tr>"
                "<th>N&ordm; recibo</th><th>Fecha pago</th><th>Cliente</th>"
                "<th style='text-align:right;'>Prendas</th>"
                "<th style='text-align:right;'>Base</th>"
                "<th style='text-align:right;'>IVA</th>"
                "<th style='text-align:right;'>Importe</th>"
            "</tr></thead><tbody>";
    double totalImporte = 0.0, totalBase = 0.0;
    int flaggedTickets = 0;
    for (int i = 0; i < tickets.size(); i++) {
        const IncomeTicketDetail &t = tickets[i];
        const double base = t.importe / (1.0 + ivaRate / 100.0);
        totalImporte += t.importe;
        totalBase += base;
        if (t.invalidAmounts > 0)
            flaggedTickets++;
        html += detailRowOpen(i)
                + "<td>" + t.nRecibo.toHtmlEscaped() + (t.invalidAmounts > 0 ? " *" : "") + "</td>"
                + "<td>" + t.fechaPago.toHtmlEscaped() + "</td>"
                + "<td>" + t.cliente.toHtmlEscaped() + "</td>"
                + "<td style='text-align:right;'>" + QString::number(t.garments) + "</td>"
                + euroCell(base) + euroCell(t.importe - base) + euroCell(t.importe) + "</tr>";
    }
    html += "<tr style='font-weight:bold;'><td colspan='4'>Total (" + QString::number(tickets.size()) + " tickets)</td>"
            + euroCell(totalBase) + euroCell(totalImporte - totalBase) + euroCell(totalImporte) + "</tr>"
            "</tbody></table>";
    if (flaggedTickets > 0)
        html += "<p>* " + QString::number(flaggedTickets) + " ticket(s) con alguna prenda cuyo importe est&aacute; "
                "guardado con ',' decimal: esa prenda no se suma en el importe ni en el total.</p>";
    return html + kRoundingNote;
}

bool Contabilidad::expenseIvaIsSummarised(int iva)
{
    return iva == 0 || iva == 10 || iva == 21;
}

QString Contabilidad::createHtmlDetailRegularizaciones(const QVector<RegularizationDetail> &regularizations,
                                                      double ivaRate)
{
    if (regularizations.isEmpty())
        return QString();

    QString html = "<h3>Anulaciones y rectificaciones del periodo</h3>"
                   "<p>Tickets cobrados en este u otro periodo y anulados o sustituidos en este: "
                   "el periodo de cobro los mantiene como ingreso y aqu&iacute; se restan.</p>"
            + ReportHtml::tableOpen() +
            "<thead><tr>"
                "<th>N&ordm; recibo</th><th>Fecha pago</th><th>Fecha anulaci&oacute;n</th><th>Motivo</th><th>Cliente</th>"
                "<th style='text-align:right;'>Base</th>"
                "<th style='text-align:right;'>IVA</th>"
                "<th style='text-align:right;'>Importe</th>"
            "</tr></thead><tbody>";
    double total = 0.0, totalBase = 0.0;
    int flagged = 0;
    for (int i = 0; i < regularizations.size(); i++) {
        const RegularizationDetail &r = regularizations[i];
        const double base = r.importe / (1.0 + ivaRate / 100.0);
        total += r.importe;
        totalBase += base;
        if (r.invalidAmounts > 0)
            flagged++;
        const QString motivo = r.verifactuEstado == QLatin1String("RECTIFICADA") ? QStringLiteral("Rectificada")
                                                                                  : QStringLiteral("Anulada");
        html += detailRowOpen(i)
                + "<td>" + r.nRecibo.toHtmlEscaped() + (r.invalidAmounts > 0 ? " *" : "") + "</td>"
                + "<td>" + r.fechaPago.toHtmlEscaped() + "</td>"
                + "<td>" + r.fechaAnulacion.toHtmlEscaped() + "</td>"
                + "<td>" + motivo + "</td>"
                + "<td>" + r.cliente.toHtmlEscaped() + "</td>"
                + euroCell(-base) + euroCell(-(r.importe - base)) + euroCell(-r.importe) + "</tr>";
    }
    html += "<tr style='font-weight:bold;'><td colspan='5'>Total (" + QString::number(regularizations.size()) + " tickets)</td>"
            + euroCell(-totalBase) + euroCell(-(total - totalBase)) + euroCell(-total) + "</tr>"
            "</tbody></table>";
    if (flagged > 0)
        html += "<p>* " + QString::number(flagged) + " ticket(s) con alguna prenda cuyo importe est&aacute; "
                "guardado con ',' decimal: esa prenda no se resta.</p>";
    return html + kRoundingNote;
}

QString Contabilidad::createHtmlDetailGastos(const QVector<ExpenseDetail> &expenses)
{
    QString html = "<h3>Detalle de gastos</h3>";
    if (expenses.isEmpty())
        return html + "<p>Sin facturas de gastos en el periodo.</p>";

    html += ReportHtml::tableOpen() +
            "<thead><tr>"
                "<th>Fecha</th><th>N&ordm; factura</th><th>Empresa</th><th>Servicio</th>"
                "<th style='text-align:right;'>IVA %</th>"
                "<th style='text-align:right;'>Base</th>"
                "<th style='text-align:right;'>Cuota IVA</th>"
                "<th style='text-align:right;'>Importe</th>"
            "</tr></thead><tbody>";
    double totalImporte = 0.0, totalBase = 0.0;
    int counted = 0, invalidRows = 0;
    for (int i = 0; i < expenses.size(); i++) {
        const ExpenseDetail &e = expenses[i];
        html += detailRowOpen(i)
                + "<td>" + e.fecha.toHtmlEscaped() + "</td>"
                + "<td>" + e.nFactura.toHtmlEscaped() + "</td>"
                + "<td>" + e.empresa.toHtmlEscaped() + "</td>"
                + "<td>" + e.servicio.toHtmlEscaped() + "</td>";
        // The summary only sums the 10 %, 21 % and sin-IVA columns; any other rate
        // (or a NULL one) is listed but flagged and kept out of the total, so the
        // total row still equals the summary.
        const QString ivaText = e.iva < 0 ? QStringLiteral("?") : QString::number(e.iva);
        if (e.invalidAmount) {
            invalidRows++;
            html += "<td style='text-align:right;'>" + ivaText + "</td>"
                    "<td style='text-align:right;'>-</td><td style='text-align:right;'>-</td>"
                    "<td style='text-align:right;'>importe no v&aacute;lido **</td></tr>";
            continue;
        }
        if (!expenseIvaIsSummarised(e.iva)) {
            html += "<td style='text-align:right;'>" + ivaText + " *</td>"
                    "<td style='text-align:right;'>-</td><td style='text-align:right;'>-</td>"
                    + euroCell(e.importe) + "</tr>";
            continue;
        }
        // Same base derivation as figuresFromTotals; iva 0 means sin IVA (base == importe).
        const double base = e.iva > 0 ? e.importe / (1.0 + e.iva / 100.0) : e.importe;
        totalImporte += e.importe;
        totalBase += base;
        counted++;
        html += "<td style='text-align:right;'>" + QString::number(e.iva) + "</td>"
                + euroCell(base) + euroCell(e.importe - base) + euroCell(e.importe) + "</tr>";
    }
    html += "<tr style='font-weight:bold;'><td colspan='5'>Total (" + QString::number(counted) + " facturas)</td>"
            + euroCell(totalBase) + euroCell(totalImporte - totalBase) + euroCell(totalImporte) + "</tr>"
            "</tbody></table>";
    const int flagged = expenses.size() - counted - invalidRows;
    if (flagged > 0)
        html += "<p>* " + QString::number(flagged) + " factura(s) con un tipo de IVA no reconocido "
                "(solo se suman sin IVA, 10 % y 21 %): aparecen en el listado pero no entran en el "
                "resumen de gastos ni en este total. Revise su IVA en la tabla de gastos.</p>";
    if (invalidRows > 0)
        html += "<p>** " + QString::number(invalidRows) + " factura(s) con el importe guardado con ',' decimal: "
                "no se suman en el resumen ni en este total.</p>";
    return html + kRoundingNote;
}
