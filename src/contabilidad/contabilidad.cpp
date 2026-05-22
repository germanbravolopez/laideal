#include "contabilidad.h"
#include "ui_contabilidad.h"
#include "sql_lite.h"
#include "qprinter.h"
#include "appsettings.h"

Contabilidad::Contabilidad(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::Contabilidad)
{
    ui->setupUi(this);
    initialSettings();
}

Contabilidad::~Contabilidad()
{
    delete ui;
}

void Contabilidad::initialSettings()
{
    ui->sb_year->setRange(2019, QDate::currentDate().year());
    ui->sb_trim->setRange(1, 4);
    resetAllContents();
}

void Contabilidad::resetAllContents()
{
    ui->cb_config->setCurrentText(C_TRIMESTRAL);
    ui->cb_config->setDisabled(revertirOn);
    ui->sb_trim->minimum();
    ui->sb_year->setValue(QDate::currentDate().year());
    ui->checkBox_lock->setChecked(false);
    ui->checkBox_lock->setDisabled(revertirOn);
}

void Contabilidad::on_bb_ok_cancel_accepted()
{
    int editLock = readLockForMonthAndYear(db, "ingresos", ui->sb_trim->value() * 3, ui->sb_year->value());
    if (ui->cb_config->currentText() == C_TRIMESTRAL) {
        switch (editLock) {
        case 0:
            // contabilidad not done
            if (revertirOn) {
                qDebug() << "Contabilidad::on_bb_ok_cancel_accepted: contabilidad not done for trim" << ui->sb_trim->value()
                         << "year" << ui->sb_year->value() << "but lock is checked, so just showing information message without generating the report or updating the lock.";
                QMessageBox::information(this, "Revertir contabilidad",
                                         "La contabilidad del trimestre "
                                         + QString::number(ui->sb_trim->value())
                                         + " para el año " + QString::number(ui->sb_year->value())
                                         + " no está realizada.",
                                         QMessageBox::Ok, QMessageBox::Ok);
            }
            else {
                generateContabilidad();
            }
            if (ui->checkBox_lock->isChecked()) {
                updateLock();
            }
            break;
        case 1:
            // contabilidad done
            if (revertirOn) {
                updateLock();
            }
            else {
                generateContabilidad();
                qDebug() << "Contabilidad::on_bb_ok_cancel_accepted: contabilidad already done for trim" << ui->sb_trim->value()
                         << "year" << ui->sb_year->value() << "and lock is not checked, so just generating the report without updating the lock.";
                QMessageBox::information(this, "Contabilidad",
                                         "La contabilidad del trimestre "
                                         + QString::number(ui->sb_trim->value())
                                         + " para el año " + QString::number(ui->sb_year->value())
                                         + " ya se ha realizado.",
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

    if (!(editLock == 0 && revertirOn)) {
        this->close();
    }
}

void Contabilidad::on_bb_ok_cancel_rejected()
{
    this->close();
}

void Contabilidad::on_cb_config_currentTextChanged(const QString &arg1)
{
    if (arg1 == C_MENSUAL) {
        ui->lbl_trim->setVisible(true);
        ui->sb_trim->setVisible(true);
        ui->lbl_trim->setText("Mes:");
        ui->sb_trim->setRange(1, 12);
        ui->sb_trim->minimum();
    }
    else if (arg1 == C_TRIMESTRAL) {
        ui->lbl_trim->setVisible(true);
        ui->sb_trim->setVisible(true);
        ui->lbl_trim->setText("Trimestre:");
        ui->sb_trim->setRange(1, 4);
        ui->sb_trim->minimum();
    }
    else if (arg1 == C_ANUAL) {
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
    QString contabilidadHtml = "<!DOCTYPE html><html>";
    QString path, filename;

    if (ui->cb_config->currentText() == C_TRIMESTRAL) {
        contabilidadHtml = contabilidadHtml + createHtmlHeader()
                + "<h1 style='text-align:center;'>Contabilidad</h1>"
                + "<h2>Trimestre: " + QString::number(ui->sb_trim->value()) + ", Año: " + QString::number(ui->sb_year->value()) + "</h2>"
                + createHtmlTables(0) + "</body></html>";
        path = AppSettings::instance()->contabilidadPath();
        filename = "/contabilidad_trimestral_" +
                QString::number(ui->sb_year->value()) +
                "_" +
                QString::number(ui->sb_trim->value()) +
                ".pdf";
    }
    else if (ui->cb_config->currentText() == C_MENSUAL) {
        QString contabilidadStatus;
        if (readLockForMonthAndYear(db, "ingresos", ui->sb_trim->value(), ui->sb_year->value()) == 1)
            contabilidadStatus = "(Contabilidad cerrada)";
        else
            contabilidadStatus = "(Contabilidad no cerrada)";

        contabilidadHtml = contabilidadHtml + createHtmlHeader()
                + "<h1 style='text-align:center;'>Reporte Mensual</h1>"
                + "<h2>Mes: " + QString::number(ui->sb_trim->value()) + ", Año: " + QString::number(ui->sb_year->value())
                + " " + contabilidadStatus + "</h2>"
                + createHtmlTables(0) + "</body></html>";
        path = AppSettings::instance()->contabilidadPath() + "/Mensual";
        filename = "/reporte_mensual_" +
                QString::number(ui->sb_year->value()) +
                "_" +
                QString::number(ui->sb_trim->value()) +
                ".pdf";
    }
    else {
        QString contabilidadStatus;
        if (readLockForMonthAndYear(db, "ingresos", ui->sb_trim->value(), ui->sb_year->value()) == 1)
            contabilidadStatus = "(Contabilidad cerrada)";
        else
            contabilidadStatus = "(Contabilidad no cerrada)";

        contabilidadHtml = contabilidadHtml + createHtmlHeader()
                + "<h1 style='text-align:center;'>Reporte Anual - " + QString::number(ui->sb_year->value()) + "</h1>";
        for (int trim = 1; trim < 5; trim++) {
            QString trimStatus;
            if (readLockForMonthAndYear(db, "ingresos", trim * 3, ui->sb_year->value()) == 1)
                trimStatus = "(Contabilidad cerrada)";
            else
                trimStatus = "(Contabilidad no cerrada)";

            contabilidadHtml = contabilidadHtml
                + "<h2>Trimestre: " + QString::number(trim) + " " + trimStatus + "</h2>"
                + createHtmlTables(trim);
        }
        contabilidadHtml = contabilidadHtml + "</body></html>";

        path = AppSettings::instance()->contabilidadPath() + "/Anual";
        filename = "/reporte_anual_" +
                QString::number(ui->sb_year->value()) +
                ".pdf";
    }
    // create directory in case it does not exists
    if (!QFile::exists(path))
        QDir().mkpath(path);
    writeHtml(path + filename, contabilidadHtml);
}

float Contabilidad::getTotalIncome(QString table,
                                   int iva,
                                   int trimForYearConfig)
{
    QDate startDate, endDate;
    if (ui->cb_config->currentText() == C_MENSUAL) {
        startDate.setDate(ui->sb_year->value(), ui->sb_trim->value(), 1);
        if (ui->sb_trim->value() == 12)
            endDate.setDate(ui->sb_year->value() + 1, 1, 1);
        else
            endDate.setDate(ui->sb_year->value(), ui->sb_trim->value() + 1, 1);
    }
    else {
        int trim;
        if (ui->cb_config->currentText() == C_TRIMESTRAL)
            trim = ui->sb_trim->value();
        else
            trim = trimForYearConfig;

        switch (trim) {
        case 1:
            startDate.setDate(ui->sb_year->value(), 1, 1);
            endDate.setDate(ui->sb_year->value(), 4, 1);
            break;
        case 2:
            startDate.setDate(ui->sb_year->value(), 4, 1);
            endDate.setDate(ui->sb_year->value(), 7, 1);
            break;
        case 3:
            startDate.setDate(ui->sb_year->value(), 7, 1);
            endDate.setDate(ui->sb_year->value(), 10, 1);
            break;
        case 4:
            startDate.setDate(ui->sb_year->value(), 10, 1);
            endDate.setDate(ui->sb_year->value() + 1, 1, 1);
            break;
        default:
            break;
        }
    }

    return totalPriceBetweenDates(db, table, startDate, endDate, iva);
}

void Contabilidad::updateLock()
{
    switch (ui->sb_trim->value()) {
    case 1:
        updateLockInIngresos(db, static_cast<int>(!revertirOn), 1, ui->sb_year->value());
        updateLockInIngresos(db, static_cast<int>(!revertirOn), 2, ui->sb_year->value());
        updateLockInIngresos(db, static_cast<int>(!revertirOn), 3, ui->sb_year->value());
        break;
    case 2:
        updateLockInIngresos(db, static_cast<int>(!revertirOn), 4, ui->sb_year->value());
        updateLockInIngresos(db, static_cast<int>(!revertirOn), 5, ui->sb_year->value());
        updateLockInIngresos(db, static_cast<int>(!revertirOn), 6, ui->sb_year->value());
        break;
    case 3:
        updateLockInIngresos(db, static_cast<int>(!revertirOn), 7, ui->sb_year->value());
        updateLockInIngresos(db, static_cast<int>(!revertirOn), 8, ui->sb_year->value());
        updateLockInIngresos(db, static_cast<int>(!revertirOn), 9, ui->sb_year->value());
        break;
    case 4:
        updateLockInIngresos(db, static_cast<int>(!revertirOn), 10, ui->sb_year->value());
        updateLockInIngresos(db, static_cast<int>(!revertirOn), 11, ui->sb_year->value());
        updateLockInIngresos(db, static_cast<int>(!revertirOn), 12, ui->sb_year->value());
        break;
    default:
        break;
    }
    if (revertirOn) {
        QMessageBox::information(this, "Revertir Contabilidad",
                                 "Contabilidad revertida con éxito para el trimestre "
                                 + ui->sb_trim->text() +
                                 " del año "
                                 + ui->sb_year->text() + ".",
                                 QMessageBox::Ok, QMessageBox::Ok);
    } else {
        QMessageBox::information(this, "Contabilidad",
                                 "Contabilidad realizada con éxito para el trimestre "
                                 + ui->sb_trim->text() +
                                 " del año "
                                 + ui->sb_year->text() + ".",
                                 QMessageBox::Ok, QMessageBox::Ok);
    }
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

QString Contabilidad::createHtmlHeader()
{
    QString contabilidadHtmlHeader =
    "<head>"
        "<meta charset='UTF-8'>"
        "<meta http-equiv='X-UA-Compatible' content='IE=edge'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<style>"
            "table, tr, th, td {"
                "text-align: left;"
                "border: 1px solid black;"
                "border-collapse: collapse;"
                "padding:3px 4px;"
            "}"
        "</style>"
    "</head>"
    "<body>"
        "<p style='text-align:right;'>Granada, " + QDate::currentDate().toString("dd-MM-yyyy") + "</p>"
        "<p><span class='text-small'>" + AppSettings::instance()->businessName() + "</span><br>"
        "<span class='text-small'>" + AppSettings::instance()->businessAddress() + "</span><br>"
        "<span class='text-small'>" + AppSettings::instance()->businessCity() + "</span></p>";

    return contabilidadHtmlHeader;
}

QString Contabilidad::createHtmlTables(int trimForYearConfig)
{
    return createHtmlTableIngresos(trimForYearConfig) + createHtmlTableGastos(trimForYearConfig);
}

QString Contabilidad::createHtmlTableIngresos(int trimForYearConfig)
{
    float totalIncomeIng = getTotalIncome("ingresos", 0, trimForYearConfig);
    float ivaRate = static_cast<float>(AppSettings::instance()->ivaRate());
    float baseIng = totalIncomeIng / (1.0f + ivaRate / 100.0f);
    float ivaIng = totalIncomeIng - baseIng;

    QString contabilidadHtmlTable =
    "<h3>Tabla Ingresos</h3>"
    "<figure class='table' style='float:left;'>"
        "<table>"
            "<thead>"
                "<tr>"
                    "<th>&nbsp;</th>"
                    "<th>Ingreso total</th>"
                "</tr>"
            "</thead>"
            "<tbody>"
                "<tr>"
                    "<th<'>Importe</th>"
                    "<td>"
                        "<p style='text-align:right;'>" + QString::number(totalIncomeIng, 'f', 2) + " €</p>"
                    "</td>"
                "</tr>"
                "<tr>"
                    "<th<'>Base</th>"
                    "<td>"
                        "<p style='text-align:right;'>" + QString::number(baseIng, 'f', 2) + " €</p>"
                    "</td>"
                "</tr>"
                "<tr>"
                    "<th<'>IVA</th>"
                    "<td>"
                        "<p style='text-align:right;'>" + QString::number(ivaIng, 'f', 2) + " €</p>"
                    "</td>"
                "</tr>"
            "</tbody>"
        "</table>"
    "</figure>";

    return contabilidadHtmlTable;
}

QString Contabilidad::createHtmlTableGastos(int trimForYearConfig)
{
    float totalIncomeG10 = getTotalIncome("gastos", 10, trimForYearConfig);
    float baseG10 = totalIncomeG10 / 1.1;
    float ivaG10 = totalIncomeG10 - baseG10;
    float totalIncomeG21 = getTotalIncome("gastos", 21, trimForYearConfig);
    float baseG21 = totalIncomeG21 / 1.21;
    float ivaG21 = totalIncomeG21 - baseG21;
    float totalIncomeGni = getTotalIncome("gastos", 0, trimForYearConfig);

    QString contabilidadHtmlTable =
    "<h3>Tabla Gastos</h3>"
    "<figure class='table' style='float:left;'>"
        "<table>"
            "<thead>"
                "<tr>"
                    "<th>&nbsp;</th>"
                    "<th>IVA = 10%</th>"
                    "<th>IVA = 21%</th>"
                    "<th>Sin IVA</th>"
                    "<th><p style='text-align:center;'>Total</p></th>"
                "</tr>"
            "</thead>"
            "<tbody>"
                "<tr>"
                    "<th<'>Importe</th>"
                    "<td>"
                        "<p style='text-align:right;'>" + QString::number(totalIncomeG10, 'f', 2) + " €</p>"
                    "</td>"
                    "<td>"
                        "<p style='text-align:right;'>" + QString::number(totalIncomeG21, 'f', 2) + " €</p>"
                    "</td>"
                    "<td>"
                        "<p style='text-align:right;'>" + QString::number(totalIncomeGni, 'f', 2) + " €</p>"
                    "</td>"
                    "<td>"
                        "<p style='text-align:right;'>" + QString::number(totalIncomeG10 + totalIncomeG21 + totalIncomeGni, 'f', 2) + " €</p>"
                    "</td>"
                "</tr>"
                "<tr>"
                    "<th<'>Base</th>"
                    "<td>"
                        "<p style='text-align:right;'>" + QString::number(baseG10, 'f', 2) + " €</p>"
                    "</td>"
                    "<td>"
                        "<p style='text-align:right;'>" + QString::number(baseG21, 'f', 2) + " €</p>"
                    "</td>"
                    "<td>"
                        "<p style='text-align:right;'>-</p>"
                    "</td>"
                    "<td>"
                        "<p style='text-align:right;'>" + QString::number(baseG10 + baseG21, 'f', 2) + " €</p>"
                    "</td>"
                "</tr>"
                "<tr>"
                    "<th<'>IVA</th>"
                    "<td>"
                        "<p style='text-align:right;'>" + QString::number(ivaG10, 'f', 2) + " €</p>"
                    "</td>"
                    "<td>"
                        "<p style='text-align:right;'>" + QString::number(ivaG21, 'f', 2) + " €</p>"
                    "</td>"
                    "<td>"
                        "<p style='text-align:right;'>-</p>"
                    "</td>"
                    "<td>"
                        "<p style='text-align:right;'>" + QString::number(ivaG10 + ivaG21, 'f', 2) + " €</p>"
                    "</td>"
                "</tr>"
            "</tbody>"
        "</table>"
    "</figure>";

    return contabilidadHtmlTable;
}
