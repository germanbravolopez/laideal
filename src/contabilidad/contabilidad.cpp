#include "contabilidad.h"
#include "ui_contabilidad.h"
#include "sql_lite.h"
#include "qprinter.h"
#include "appsettings.h"
#include "reporthtml.h"

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
    ui->checkBox_lock->setDisabled(revertirOn); // disable lock checkbox when reverting
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
        int editLock = readLockForQuarter(db, "ingresos", ui->sb_trim->value(), ui->sb_year->value());
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
                                 "No hay registros de ingresos para realizar la contabilidad en el periodo indicado.",
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
    QString contabilidadHtml, path, filename;

    if (currentMode() == Trimestral) {
        contabilidadHtml = ReportHtml::documentOpen("Contabilidad - Trimestre " + QString::number(ui->sb_trim->value())
                                                    + " · " + QString::number(year),
                                                    periodSubtitle(0))
                + renderSection(computeFigures(0), "Resumen del periodo")
                + ReportHtml::documentClose();
        path = AppSettings::instance()->contabilidadPath();
        filename = "/contabilidad_trimestral_" + QString::number(year) + "_" + QString::number(ui->sb_trim->value()) + ".pdf";
    }
    else if (currentMode() == Mensual) {
        bool cerrada = readLockForMonthAndYear(db, "ingresos", ui->sb_trim->value(), year) == 1;
        contabilidadHtml = ReportHtml::documentOpen("Reporte Mensual - Mes " + QString::number(ui->sb_trim->value())
                                                    + " · " + QString::number(year),
                                                    periodSubtitle(0) + (cerrada ? " · Contabilidad cerrada"
                                                                                  : " · Contabilidad no cerrada"))
                + renderSection(computeFigures(0), "Resumen del periodo")
                + ReportHtml::documentClose();
        path = AppSettings::instance()->contabilidadPath() + "/Mensual";
        filename = "/reporte_mensual_" + QString::number(year) + "_" + QString::number(ui->sb_trim->value()) + ".pdf";
    }
    else {
        contabilidadHtml = ReportHtml::documentOpen("Reporte Anual - " + QString::number(year));
        // One grouped query per table for the whole year (bucketed by quarter)
        // instead of computeFigures' 6 full-table scans per quarter (24 total).
        // The IVA base/cuota math is identical (figuresFromTotals) - the grouped
        // aggregation is asserted equal to the per-quarter helpers in
        // test_sql_lite::test_annualAccountingByQuarter.
        const double ivaRate = static_cast<double>(AppSettings::instance()->ivaRate());
        const QuarterlyAccountingTotals totals = annualAccountingByQuarter(db, year);
        PeriodFigures annual;
        for (int trim = 1; trim < 5; trim++) {
            const int i = trim - 1;
            bool cerrada = readLockForQuarter(db, "ingresos", trim, year) == 1;
            PeriodFigures f = figuresFromTotals(
                totals.ingImporte[i], totals.ingTickets[i],
                totals.gas10Importe[i], totals.gas21Importe[i], totals.gasNiImporte[i],
                totals.gasFacturas[i], ivaRate);
            annual.accumulate(f);
            contabilidadHtml += "<h2>Trimestre " + QString::number(trim)
                    + (cerrada ? " · Contabilidad cerrada" : " · Contabilidad no cerrada") + "</h2>"
                    + renderSection(f, "Resumen del trimestre");
        }
        contabilidadHtml += "<h2>Resumen anual consolidado</h2>"
                + createHtmlSummary(annual, "Total a&ntilde;o " + QString::number(year))
                + ReportHtml::documentClose();
        path = AppSettings::instance()->contabilidadPath() + "/Anual";
        filename = "/reporte_anual_" + QString::number(year) + ".pdf";
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

float Contabilidad::getTotalIncome(QString table,
                                   int iva,
                                   int trimForYearConfig)
{
    QDate startDate, endDate;
    periodRange(trimForYearConfig, startDate, endDate);
    return totalPriceBetweenDates(db, table, startDate, endDate, iva);
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

Contabilidad::PeriodFigures Contabilidad::computeFigures(int trimForYearConfig)
{
    const double ivaRate = static_cast<double>(AppSettings::instance()->ivaRate());
    QDate start, endExclusive;
    periodRange(trimForYearConfig, start, endExclusive);
    return figuresFromTotals(
        getTotalIncome("ingresos", 0, trimForYearConfig),
        countOperationsBetweenDates(db, "ingresos", start, endExclusive),
        getTotalIncome("gastos", 10, trimForYearConfig),
        getTotalIncome("gastos", 21, trimForYearConfig),
        getTotalIncome("gastos", 0, trimForYearConfig),
        countOperationsBetweenDates(db, "gastos", start, endExclusive),
        ivaRate);
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
