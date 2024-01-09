#include "contabilidad.h"
#include "ui_contabilidad.h"
#include "sql_lite.h"
#include "qprinter.h"

Contabilidad::Contabilidad(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::Contabilidad)
{
    ui->setupUi(this);
    initial_settings();
}

Contabilidad::~Contabilidad()
{
    delete ui;
}

void Contabilidad::initial_settings()
{
    ui->sb_year->setRange(2019, QDate::currentDate().year());
    ui->sb_trim->setRange(1, 4);
    reset_all_contents();
}

void Contabilidad::reset_all_contents()
{
    ui->cb_config->setCurrentText(C_TRIMESTRAL);
    ui->cb_config->setDisabled(revertir_on);
    ui->sb_trim->minimum();
    ui->sb_year->setValue(QDate::currentDate().year());
    ui->checkBox_lock->setChecked(false);
    ui->checkBox_lock->setDisabled(revertir_on);
}

void Contabilidad::on_bb_ok_cancel_accepted()
{
    if (ui->cb_config->currentText() == C_TRIMESTRAL) {
        switch (read_lock_for_month_and_year(db, "ingresos", ui->sb_trim->value() * 3, ui->sb_year->value())) {
        case 0:
            // contabilidad not done
            if (revertir_on)
                QMessageBox::information(this, "Contabilidad",
                                         "La contabilidad del trimestre "
                                         + QString::number(ui->sb_trim->value())
                                         + " para el año " + QString::number(ui->sb_year->value())
                                         + " no está realizada.",
                                         QMessageBox::Ok, QMessageBox::Ok);
            else
                generate_contabilidad();

            if (ui->checkBox_lock->isChecked())
                update_lock();
            break;
        case 1:
            // contabilidad done
            if (revertir_on)
                update_lock();
            else {
                generate_contabilidad();
                QMessageBox::information(this, "Contabilidad",
                                         "La contabilidad del trimestre "
                                         + QString::number(ui->sb_trim->value())
                                         + " para el año " + QString::number(ui->sb_year->value())
                                         + " ya se ha realizado.",
                                         QMessageBox::Ok, QMessageBox::Ok);
            }
            break;
        default:
            QMessageBox::warning(this, "Contabilidad",
                                  "No hay registros de ingresos para realizar la contabilidad en el periodo indicado.",
                                  QMessageBox::Ok, QMessageBox::Ok);
            break;
        }
    }
    else
        generate_contabilidad();

    this->close();
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
    else
        QMessageBox::critical(this, "Contabilidad",
                              "No se puede configurar de la forma indicada.",
                              QMessageBox::Ok, QMessageBox::Ok);
}

void Contabilidad::generate_contabilidad()
{
    QString contabilidad_html = "<!DOCTYPE html><html>";
    QString path, filename;

    if (ui->cb_config->currentText() == C_TRIMESTRAL) {
        contabilidad_html = contabilidad_html + create_html_header()
                + "<h1 style='text-align:center;'>Contabilidad</h1>"
                + "<h2>Trimestre: " + QString::number(ui->sb_trim->value()) + ", Año: " + QString::number(ui->sb_year->value()) + "</h2>"
                + create_html_tables(0) + "</body></html>";
        path = "C:/Users/Usuario/OneDrive/Desktop/Tintoreria/Contabilidad";
        filename = "/contabilidad_trimestral_" +
                QString::number(ui->sb_year->value()) +
                "_" +
                QString::number(ui->sb_trim->value()) +
                ".pdf";
    }
    else if (ui->cb_config->currentText() == C_MENSUAL) {
        QString contabilidad_status;
        if (read_lock_for_month_and_year(db, "ingresos", ui->sb_trim->value(), ui->sb_year->value()) == 1)
            contabilidad_status = "(Contabilidad cerrada)";
        else
            contabilidad_status = "(Contabilidad no cerrada)";

        contabilidad_html = contabilidad_html + create_html_header()
                + "<h1 style='text-align:center;'>Reporte Mensual</h1>"
                + "<h2>Mes: " + QString::number(ui->sb_trim->value()) + ", Año: " + QString::number(ui->sb_year->value())
                + " " + contabilidad_status + "</h2>"
                + create_html_tables(0) + "</body></html>";
        path = "C:/Users/Usuario/OneDrive/Desktop/Tintoreria/Contabilidad/Mensual";
        filename = "/reporte_mensual_" +
                QString::number(ui->sb_year->value()) +
                "_" +
                QString::number(ui->sb_trim->value()) +
                ".pdf";
    }
    else {
        QString contabilidad_status;
        if (read_lock_for_month_and_year(db, "ingresos", ui->sb_trim->value(), ui->sb_year->value()) == 1)
            contabilidad_status = "(Contabilidad cerrada)";
        else
            contabilidad_status = "(Contabilidad no cerrada)";

        contabilidad_html = contabilidad_html + create_html_header()
                + "<h1 style='text-align:center;'>Reporte Anual - " + QString::number(ui->sb_year->value()) + "</h1>";
        for (int trim = 1; trim < 5; trim++) {
            QString contabilidad_status;
            if (read_lock_for_month_and_year(db, "ingresos", trim * 3, ui->sb_year->value()) == 1)
                contabilidad_status = "(Contabilidad cerrada)";
            else
                contabilidad_status = "(Contabilidad no cerrada)";

            contabilidad_html = contabilidad_html
                + "<h2>Trimestre: " + QString::number(trim) + " " + contabilidad_status + "</h2>"
                + create_html_tables(trim);
        }
        contabilidad_html = contabilidad_html + "</body></html>";

        path = "C:/Users/Usuario/OneDrive/Desktop/Tintoreria/Contabilidad/Anual";
        filename = "/reporte_anual_" +
                QString::number(ui->sb_year->value()) +
                ".pdf";
    }
    // create directory in case it does not exists
    if (!QFile::exists(path))
        QDir().mkpath(path);
    write_html(path + filename, contabilidad_html);
}

float Contabilidad::get_total_income(QString table,
                                     int iva,
                                     int trim_for_year_config)
{
    QDate start_date, end_date;
    if (ui->cb_config->currentText() == C_MENSUAL) {
        start_date.setDate(ui->sb_year->value(), ui->sb_trim->value(), 1);
        if (ui->sb_trim->value() == 12)
            end_date.setDate(ui->sb_year->value() + 1, 1, 1);
        else
            end_date.setDate(ui->sb_year->value(), ui->sb_trim->value() + 1, 1);
    }
    else {
        int trim;
        if (ui->cb_config->currentText() == C_TRIMESTRAL)
            trim = ui->sb_trim->value();
        else
            trim = trim_for_year_config;

        switch (trim) {
        case 1:
            start_date.setDate(ui->sb_year->value(), 1, 1);
            end_date.setDate(ui->sb_year->value(), 4, 1);
            break;
        case 2:
            start_date.setDate(ui->sb_year->value(), 4, 1);
            end_date.setDate(ui->sb_year->value(), 7, 1);
            break;
        case 3:
            start_date.setDate(ui->sb_year->value(), 7, 1);
            end_date.setDate(ui->sb_year->value(), 10, 1);
            break;
        case 4:
            start_date.setDate(ui->sb_year->value(), 10, 1);
            end_date.setDate(ui->sb_year->value() + 1, 1, 1);
            break;
        default:
            break;
        }
    }

    return total_price_between_dates(db, table, start_date, end_date, iva);
}

void Contabilidad::update_lock()
{
    switch (ui->sb_trim->value()) {
    case 1:
        update_lock_in_ingresos(db, static_cast<int>(!revertir_on), 1, ui->sb_year->value());
        update_lock_in_ingresos(db, static_cast<int>(!revertir_on), 2, ui->sb_year->value());
        update_lock_in_ingresos(db, static_cast<int>(!revertir_on), 3, ui->sb_year->value());
        break;
    case 2:
        update_lock_in_ingresos(db, static_cast<int>(!revertir_on), 4, ui->sb_year->value());
        update_lock_in_ingresos(db, static_cast<int>(!revertir_on), 5, ui->sb_year->value());
        update_lock_in_ingresos(db, static_cast<int>(!revertir_on), 6, ui->sb_year->value());
        break;
    case 3:
        update_lock_in_ingresos(db, static_cast<int>(!revertir_on), 7, ui->sb_year->value());
        update_lock_in_ingresos(db, static_cast<int>(!revertir_on), 8, ui->sb_year->value());
        update_lock_in_ingresos(db, static_cast<int>(!revertir_on), 9, ui->sb_year->value());
        break;
    case 4:
        update_lock_in_ingresos(db, static_cast<int>(!revertir_on), 10, ui->sb_year->value());
        update_lock_in_ingresos(db, static_cast<int>(!revertir_on), 11, ui->sb_year->value());
        update_lock_in_ingresos(db, static_cast<int>(!revertir_on), 12, ui->sb_year->value());
        break;
    default:
        break;
    }
    if (revertir_on) {
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

void Contabilidad::write_html(QString filename,
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

QString Contabilidad::create_html_header()
{
    QString contabilidad_html_header =
    "<head>"
       "<meta charset='UTF-8'>"
       "<meta http-equiv='X-UA-Compatible' content='IE=edge'>"
       "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
       "<style>"
          "table,"
          "tr,"
          "th,"
          "td {"
             "border: 1px solid black;"
             "border-collapse: collapse;"
          "}"
       "</style>"
    "</head>"
    "<body>"
        "<p style='text-align:right;'>Granada, " + QDate::currentDate().toString("dd-MM-yyyy") + "</p>"
        "<p><span class='text-small'>Tintorería La Ideal</span><br><span class='text-small'>Plaza San Pantaleón 1, bajo 2</span><br><span class='text-small'>18012 Granada</span></p>";

    return contabilidad_html_header;
}

QString Contabilidad::create_html_tables(int trim_for_year_config)
{
    return create_html_table_ingresos(trim_for_year_config) + create_html_table_gastos(trim_for_year_config);
}

QString Contabilidad::create_html_table_ingresos(int trim_for_year_config)
{
    float total_income_ing = get_total_income("ingresos", 0, trim_for_year_config);
    float base_ing = total_income_ing / 1.21;
    float iva_ing = total_income_ing - base_ing;

    QString contabilidad_html_table =
    "<h3>Tabla Ingresos</h3>"
    "<figure class='table' style='float:left;'>"
        "<table>"
            "<thead>"
                "<tr>"
                    "<th style='border:1px solid hsl(0, 0%, 0%);'>&nbsp;</th>"
                    "<th style='border:1px solid hsl(0, 0%, 0%);'>Ingreso total </th>"
                "</tr>"
            "</thead>"
            "<tbody>"
                "<tr>"
                    "<th style='text-align:left; border:1px solid hsl(0, 0%, 0%);'>Importe </th>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>" + QString::number(total_income_ing, 'f', 2) + " € </p>"
                    "</td>"
                "</tr>"
                "<tr>"
                    "<th style='text-align:left; border:1px solid hsl(0, 0%, 0%);'>Base</th>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>" + QString::number(base_ing, 'f', 2) + " € </p>"
                    "</td>"
                "</tr>"
                "<tr>"
                    "<th style='text-align:left; border:1px solid hsl(0, 0%, 0%);'>IVA</th>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>" + QString::number(iva_ing, 'f', 2) + " € </p>"
                    "</td>"
                "</tr>"
            "</tbody>"
        "</table>"
    "</figure>";

    return contabilidad_html_table;
}

QString Contabilidad::create_html_table_gastos(int trim_for_year_config)
{
    float total_income_g10 = get_total_income("gastos", 10, trim_for_year_config);
    float base_g10 = total_income_g10 / 1.1;
    float iva_g10 = total_income_g10 - base_g10;
    float total_income_g21 = get_total_income("gastos", 21, trim_for_year_config);
    float base_g21 = total_income_g21 / 1.21;
    float iva_g21 = total_income_g21 - base_g21;
    float total_income_gni = get_total_income("gastos", 0, trim_for_year_config);

    QString contabilidad_html_table =
    "<h3>Tabla Gastos</h3>"
    "<figure class='table' style='float:left;'>"
        "<table>"
            "<thead>"
                "<tr>"
                    "<th style='border:1px solid hsl(0, 0%, 0%);'>&nbsp;</th>"
                    "<th style='border:1px solid hsl(0, 0%, 0%);'>IVA = 10% </th>"
                    "<th style='border:1px solid hsl(0, 0%, 0%);'>IVA = 21% </th>"
                    "<th style='border:1px solid hsl(0, 0%, 0%);'>Sin IVA </th>"
                    "<th style='border:1px solid hsl(0, 0%, 0%);'>Total </th>"
                "</tr>"
            "</thead>"
            "<tbody>"
                "<tr>"
                    "<th style='text-align:left; border:1px solid hsl(0, 0%, 0%);'>Importe </th>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>" + QString::number(total_income_g10, 'f', 2) + " € </p>"
                    "</td>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>" + QString::number(total_income_g21, 'f', 2) + " € </p>"
                    "</td>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>" + QString::number(total_income_gni, 'f', 2) + " € </p>"
                    "</td>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>" + QString::number(total_income_g10 + total_income_g21 + total_income_gni, 'f', 2) + " € </p>"
                    "</td>"
                "</tr>"
                "<tr>"
                    "<th style='text-align:left; border:1px solid hsl(0, 0%, 0%);'>Base</th>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>" + QString::number(base_g10, 'f', 2) + " € </p>"
                    "</td>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>" + QString::number(base_g21, 'f', 2) + " € </p>"
                    "</td>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>- </p>"
                    "</td>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>" + QString::number(base_g10 + base_g21, 'f', 2) + " € </p>"
                    "</td>"
                "</tr>"
                "<tr>"
                    "<th style='text-align:left; border:1px solid hsl(0, 0%, 0%);'>IVA</th>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>" + QString::number(iva_g10, 'f', 2) + " € </p>"
                    "</td>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>" + QString::number(iva_g21, 'f', 2) + " € </p>"
                    "</td>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>- </p>"
                    "</td>"
                    "<td style='border:1px solid hsl(0, 0%, 0%);'>"
                        "<p style='text-align:right;'>" + QString::number(iva_g10 + iva_g21, 'f', 2) + " € </p>"
                    "</td>"
                "</tr>"
            "</tbody>"
        "</table>"
    "</figure>";

    return contabilidad_html_table;
}
