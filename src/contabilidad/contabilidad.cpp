#include "contabilidad.h"
#include "ui_contabilidad.h"
#include "sql_lite.h"
#include "qprinter.h"
#include "appsettings.h"
#include "reporthtml.h"

#include <QHash>

Contabilidad::Contabilidad(const QSqlDatabase &database, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Contabilidad),
    db(database)
{
    setAttribute(Qt::WA_DeleteOnClose); // self-delete on close: callers show() non-modally and drop the pointer
    ui->setupUi(this);
    initialSettings();
}

Contabilidad::~Contabilidad()
{
    delete ui;
}

Contabilidad::ConfigMode Contabilidad::currentMode() const
{
    return static_cast<ConfigMode>(ui->cb_config->currentIndex());
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
    ui->sb_year->setRange(firstYear > 0 ? firstYear : currentYear, currentYear);
    ui->sb_trim->setRange(1, 4);
    resetAllContents();
}

void Contabilidad::resetAllContents()
{
    ui->cb_config->setCurrentIndex(Trimestral);
    ui->cb_config->setDisabled(revertirOn); // disable configuration combobox when reverting contabilidad mode
    ui->sb_year->setValue(QDate::currentDate().year());
    ui->checkBox_lock->setChecked(false);
    ui->checkBox_lock->setEnabled(lockOptionAvailable(currentMode(), revertirOn));
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

void Contabilidad::on_bb_ok_cancel_accepted()
{
    // Stays false except when reverting a quarter that was never done: there we
    // keep the dialog open so the operator can act on the info message.
    bool keepDialogOpen = false;
    if (currentMode() == Trimestral) {
        // Read the lock across the whole quarter, not just its last month: a
        // quarter with income only in its first months would otherwise read as
        // "no data" and never get locked.
        int editLock = combinedLockState(readLockForQuarter(db, "ingresos", ui->sb_trim->value(), ui->sb_year->value()),
                                         readLockForQuarter(db, "gastos", ui->sb_trim->value(), ui->sb_year->value()));
        switch (editLock) {
        case 0:
            // contabilidad not done
            if (revertirOn) {
                // revert contabilidad mode
                qDebug() << "(Revertir) Contabilidad::on_bb_ok_cancel_accepted: contabilidad was not done for trim" << ui->sb_trim->value()
                         << "year" << ui->sb_year->value() << " (editLock = 0). Just showing information message without generating the report or updating the lock.";
                QMessageBox::information(this, "Revertir contabilidad", "La contabilidad del trimestre " + QString::number(ui->sb_trim->value())
                                         + " para el año " + QString::number(ui->sb_year->value()) + " no estaba aun realizada.",
                                         QMessageBox::Ok, QMessageBox::Ok);
                keepDialogOpen = true;
            }
            else {
                generateContabilidad();
                qDebug() << "Contabilidad::on_bb_ok_cancel_accepted: contabilidad is done for trim" << ui->sb_trim->value()
                         << "year" << ui->sb_year->value() << ". Proceeding with report generation + editLock = " << ui->checkBox_lock->isChecked() << ".";
                QMessageBox::information(this, "Contabilidad", "La contabilidad del trimestre " + QString::number(ui->sb_trim->value())
                                         + " para el año " + QString::number(ui->sb_year->value()) + " se ha realizado."
                                         + (ui->checkBox_lock->isChecked() ?
                                                " El trimestre se ha bloqueado para evitar modificaciones posteriores." :
                                                " El trimestre no se ha bloqueado, por lo que se pueden realizar modificaciones posteriores."),
                                         QMessageBox::Ok, QMessageBox::Ok);
            }
            if (ui->checkBox_lock->isChecked()) {
                updateLock();
            }
            break;
        case 1:
            // contabilidad done
            if (revertirOn) {
                // revert contabilidad mode
                updateLock();
                qDebug() << "(Revertir) Contabilidad::on_bb_ok_cancel_accepted: contabilidad reverted for trim" << ui->sb_trim->value()
                         << "year" << ui->sb_year->value() << " (editLock = 0).";
                QMessageBox::information(this, "Revertir contabilidad", "La contabilidad del trimestre " + QString::number(ui->sb_trim->value())
                                         + " para el año " + QString::number(ui->sb_year->value()) + " se ha revertido.",
                                         QMessageBox::Ok, QMessageBox::Ok);
            }
            else {
                generateContabilidad();
                qDebug() << "Contabilidad::on_bb_ok_cancel_accepted: contabilidad already done for trim" << ui->sb_trim->value()
                         << "year" << ui->sb_year->value() << "and editLock was already set, so just generating the report.";
                QMessageBox::information(this, "Contabilidad", "La contabilidad del trimestre " + QString::number(ui->sb_trim->value())
                                         + " para el año " + QString::number(ui->sb_year->value()) + " ya estaba realizada."
                                         + " Documentacion generada de nuevo.",
                                         QMessageBox::Ok, QMessageBox::Ok);
            }
            break;
        default:
            qWarning() << "Contabilidad::on_bb_ok_cancel_accepted: invalid lock value read from database:" << editLock;
            QMessageBox::warning(this, "Contabilidad",
                                 "No hay registros de ingresos ni de gastos para realizar la contabilidad en el periodo indicado.",
                                 QMessageBox::Ok, QMessageBox::Ok);
            break;
        }
    }
    else {
        generateContabilidad();
    }

    if (!keepDialogOpen) {
        this->close();
    }
}

void Contabilidad::on_bb_ok_cancel_rejected()
{
    this->close();
}

void Contabilidad::on_cb_config_currentTextChanged(const QString &arg1)
{
    // Only a quarterly report can close the books; untick so a hidden choice never applies.
    const bool lockAvailable = lockOptionAvailable(currentMode(), revertirOn);
    if (!lockAvailable)
        ui->checkBox_lock->setChecked(false);
    ui->checkBox_lock->setEnabled(lockAvailable);
    if (currentMode() == Mensual) {
        ui->lbl_trim->setVisible(true);
        ui->sb_trim->setVisible(true);
        ui->lbl_trim->setText("Mes:");
        ui->sb_trim->setRange(1, 12);
    }
    else if (currentMode() == Trimestral) {
        ui->lbl_trim->setVisible(true);
        ui->sb_trim->setVisible(true);
        ui->lbl_trim->setText("Trimestre:");
        ui->sb_trim->setRange(1, 4);
    }
    else if (currentMode() == Anual) {
        ui->lbl_trim->setVisible(false);
        ui->sb_trim->setVisible(false);
    }
    else {
        qCritical() << "Contabilidad::on_cb_config_currentTextChanged: unsupported configuration option" << arg1;
        QMessageBox::critical(this, "Contabilidad",
                              "No se puede configurar de la forma indicada.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }
}

void Contabilidad::generateContabilidad()
{
    const int year = ui->sb_year->value();
    const double ivaRate = AppSettings::ivaRate();
    QString contabilidadHtml, path, filename;
    int invalidAmounts = 0;

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
            title = "Contabilidad - Trimestre " + QString::number(ui->sb_trim->value()) + " · " + QString::number(year);
            path = AppSettings::instance()->contabilidadPath();
            filename = "/contabilidad_trimestral_" + QString::number(year) + "_" + QString::number(ui->sb_trim->value()) + ".pdf";
        }
        else {
            // Closing is quarterly: the month reads closed when its quarter is (even a
            // month without rows, which readLockForMonthAndYear would report as open).
            const bool cerrada = quarterIsClosed(db, QDate(year, ui->sb_trim->value(), 1));
            title = "Reporte Mensual - Mes " + QString::number(ui->sb_trim->value()) + " · " + QString::number(year);
            subtitle += cerrada ? " · Contabilidad cerrada" : " · Contabilidad no cerrada";
            path = AppSettings::instance()->contabilidadPath() + "/Mensual";
            filename = "/reporte_mensual_" + QString::number(year) + "_" + QString::number(ui->sb_trim->value()) + ".pdf";
        }
        contabilidadHtml = ReportHtml::documentOpen(title, subtitle)
                + renderSection(figuresFromDetails(income, regs, expenses, ivaRate), "Resumen del periodo")
                + renderDetailTables("Detalle del periodo", income, regs, expenses, ivaRate)
                + ReportHtml::documentClose();
    }
    else {
        contabilidadHtml = ReportHtml::documentOpen("Reporte Anual - " + QString::number(year));
        // One scan per table for the whole year, bucketed by quarter; each quarter's
        // figures and detail tables come from the same rows.
        const QuarterlyDetails details = annualDetailsByQuarter(db, year);
        PeriodFigures annual;
        for (int trim = 1; trim < 5; trim++) {
            const int i = trim - 1;
            const bool cerrada = combinedLockState(readLockForQuarter(db, "ingresos", trim, year),
                                                   readLockForQuarter(db, "gastos", trim, year)) == 1;
            const PeriodFigures f = figuresFromDetails(details.income[i], details.regularizations[i],
                                                       details.expenses[i], ivaRate);
            annual.accumulate(f);
            invalidAmounts += invalidAmountCount(details.income[i], details.regularizations[i], details.expenses[i]);
            contabilidadHtml += "<h2>Trimestre " + QString::number(trim)
                    + (cerrada ? " · Contabilidad cerrada" : " · Contabilidad no cerrada") + "</h2>"
                    + renderSection(f, "Resumen del trimestre");
        }
        annual.ingTickets = yearTicketCount(details);   // distinct over the year, not the quarterly sum
        contabilidadHtml += "<h2>Resumen anual consolidado</h2>"
                + createHtmlSummary(annual, "Total a&ntilde;o " + QString::number(year));
        for (int trim = 1; trim < 5; trim++)
            contabilidadHtml += renderDetailTables("Detalle del trimestre " + QString::number(trim),
                                                   details.income[trim - 1], details.regularizations[trim - 1],
                                                   details.expenses[trim - 1], ivaRate);
        contabilidadHtml += ReportHtml::documentClose();
        path = AppSettings::instance()->contabilidadPath() + "/Anual";
        filename = "/reporte_anual_" + QString::number(year) + ".pdf";
    }
    if (invalidAmounts > 0) {
        qCritical() << "Contabilidad::generateContabilidad:" << invalidAmounts << "comma-decimal importe(s) not summed";
        QMessageBox::critical(this, "Error en la base de datos",
                              QString("Hay %1 importe(s) guardados con ',' como separador decimal: no se han sumado "
                                      "en la contabilidad y aparecen marcados en el detalle. Utilizar herramienta "
                                      "de limpiado de importes decimales.").arg(invalidAmounts),
                              QMessageBox::Ok, QMessageBox::Ok);
    }
    // create directory in case it does not exists
    if (!QFile::exists(path))
        QDir().mkpath(path);
    writeHtml(path + filename, contabilidadHtml);
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
    const int unit = (mode == Anual) ? trimForYearConfig : ui->sb_trim->value();
    periodRangeFor(mode, unit, ui->sb_year->value(), start, endExclusive);
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
                                                             double ivaRate)
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
    PeriodFigures f = figuresFromTotals(ingImporte - regularizacion, netTicketCount(income, regularizations),
                                        gas10, gas21, gasNi, expenses.size(), ivaRate);
    f.ingRegularizacion = regularizacion;
    return f;
}

int Contabilidad::netTicketCount(const QVector<IncomeTicketDetail> &income,
                                 const QVector<RegularizationDetail> &regularizations)
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

int Contabilidad::yearTicketCount(const QuarterlyDetails &details)
{
    QVector<IncomeTicketDetail> income;
    QVector<RegularizationDetail> regularizations;
    for (const QVector<IncomeTicketDetail> &quarter : details.income)
        income += quarter;
    for (const QVector<RegularizationDetail> &quarter : details.regularizations)
        regularizations += quarter;
    return netTicketCount(income, regularizations);
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
    const int year = ui->sb_year->value();
    switch (ui->sb_trim->value()) {
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
             << "for trim" << ui->sb_trim->value() << "year" << year;
}

void Contabilidad::writeHtml(QString filename,
                             QString html)
{
    QTextDocument document;
    document.setHtml(html);

    QPrinter printer(QPrinter::PrinterResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setPageSize(QPageSize::A4);
    printer.setOutputFileName(filename);
    printer.setPageMargins(QMarginsF(15, 15, 15, 15));

    document.print(&printer);

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
            "<th style='text-align:right;'>IVA 10%</th>"
            "<th style='text-align:right;'>IVA 21%</th>"
            "<th style='text-align:right;'>Sin IVA</th>"
            "<th style='text-align:right;'>Total</th>"
        "</tr>"
        "<tr>"
            "<td>Importe</td>"
            + euroCell(f.gas10Importe) + euroCell(f.gas21Importe) + euroCell(f.gasNiImporte) + euroCell(f.gastosImporteTotal()) +
        "</tr>"
        "<tr style='background-color:#f6f7f9;'>"
            "<td>Base imponible</td>"
            + euroCell(f.gas10Base) + euroCell(f.gas21Base) + euroCell(f.gasNiImporte) + euroCell(f.gastosBaseTotal()) +
        "</tr>"
        "<tr>"
            "<td>IVA soportado</td>"
            + euroCell(f.gas10Iva) + euroCell(f.gas21Iva) + "<td style='text-align:right;'>-</td>" + euroCell(f.gastosIvaTotal()) +
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
