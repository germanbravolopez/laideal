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
    ui->sb_trim->minimum();
    ui->sb_year->setValue(QDate::currentDate().year());
}

void Contabilidad::on_bb_ok_cancel_accepted()
{
    if (ui->cb_config->currentText() == C_TRIMESTRAL)
    {
        switch (read_lock_for_month_and_year(db, ui->sb_trim->value() * 3, ui->sb_year->value())) {
        case 0:
            // contabilidad not done
            generate_contabilidad();
            lock_data();
            break;
        case 1:
            // contabilidad done
            qDebug() << "La contabilidad del trimestre " + QString::number(ui->sb_trim->value())
                        + " para el año " + QString::number(ui->sb_year->value())
                        + " ya se ha realizado";
            generate_contabilidad();
            lock_data();
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
    if (arg1 == C_MENSUAL)
    {
        ui->lbl_trim->setVisible(true);
        ui->sb_trim->setVisible(true);
        ui->lbl_trim->setText("Mes:");
        ui->sb_trim->setRange(1, 12);
        ui->sb_trim->minimum();
    }
    else if (arg1 == C_TRIMESTRAL)
    {
        ui->lbl_trim->setVisible(true);
        ui->sb_trim->setVisible(true);
        ui->lbl_trim->setText("Trimestre:");
        ui->sb_trim->setRange(1, 4);
        ui->sb_trim->minimum();
    }
    else if (arg1 == C_ANUAL)
    {
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

    if (ui->cb_config->currentText() == C_TRIMESTRAL)
    {
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
    else if (ui->cb_config->currentText() == C_MENSUAL)
    {
        QString contabilidad_status;
        if (read_lock_for_month_and_year(db, ui->sb_trim->value(), ui->sb_year->value()) == 1)
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
    else
    {
        QString contabilidad_status;
        if (read_lock_for_month_and_year(db, ui->sb_trim->value(), ui->sb_year->value()) == 1)
            contabilidad_status = "(Contabilidad cerrada)";
        else
            contabilidad_status = "(Contabilidad no cerrada)";

        contabilidad_html = contabilidad_html + create_html_header()
                + "<h1 style='text-align:center;'>Reporte Anual - " + QString::number(ui->sb_year->value()) + "</h1>";
        for (int trim = 1; trim < 5; trim++)
        {
            QString contabilidad_status;
            if (read_lock_for_month_and_year(db, trim * 3, ui->sb_year->value()) == 1)
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
    // open file in case it already exists
    if (!QFile::exists(filename))
        write_html(path + filename, contabilidad_html);
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(filename));
}

double Contabilidad::get_total_income(QString table, int iva, int trim_for_year_config)
{
    QDate start_date, end_date;
    if (ui->cb_config->currentText() == C_MENSUAL)
    {
        start_date.setDate(ui->sb_year->value(), ui->sb_trim->value(), 1);
        end_date.setDate(ui->sb_year->value(), ui->sb_trim->value() + 1, 1);
    }
    else
    {
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

void Contabilidad::lock_data()
{
    switch (ui->sb_trim->value()) {
    case 1:
        update_lock_in_ingresos(db, 1, 1, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 2, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 3, ui->sb_year->value());
        break;
    case 2:
        update_lock_in_ingresos(db, 1, 4, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 5, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 6, ui->sb_year->value());
        break;
    case 3:
        update_lock_in_ingresos(db, 1, 7, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 8, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 9, ui->sb_year->value());
        break;
    case 4:
        update_lock_in_ingresos(db, 1, 10, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 11, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 12, ui->sb_year->value());
        break;
    default:
        break;
    }
}

void Contabilidad::write_html(QString filename, QString html)
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
    double total_income_ing = get_total_income("ingresos", 0, trim_for_year_config);
    double base_ing = total_income_ing / 1.21;
    double iva_ing = total_income_ing - base_ing;

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
    double total_income_g10 = get_total_income("gastos", 10, trim_for_year_config);
    double base_g10 = total_income_g10 / 1.1;
    double iva_g10 = total_income_g10 - base_g10;
    double total_income_g21 = get_total_income("gastos", 21, trim_for_year_config);
    double base_g21 = total_income_g21 / 1.21;
    double iva_g21 = total_income_g21 - base_g21;
    double total_income_gni = get_total_income("gastos", 0, trim_for_year_config);

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
