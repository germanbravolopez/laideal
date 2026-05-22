#include "genlistado.h"
#include "ui_genlistado.h"
#include "qprinter.h"
#include "sql_lite.h"
#include "mysortfilterproxymodel.h"
#include "appsettings.h"

#include <QMessageBox>

GenListado::GenListado(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::GenListado)
{
    ui->setupUi(this);
    initial_settings();
}

GenListado::~GenListado()
{
    delete ui;
}

void GenListado::initial_settings()
{
    set_cb_fechas();
    ui->cb_agrupar->addItems({C_FECHAS, C_PROVEEDORES});
    ui->cb_tipo_gastos->addItems({C_INCL_TODOS, C_CONTAB_CERR});
}

void GenListado::set_cb_fechas()
{
    int max_year = readMaxNMinYearInColumnFromTable(db, true, "fecha", "gastos");
    int min_year = readMaxNMinYearInColumnFromTable(db, false, "fecha", "gastos");
    QStringList fechas_list;
    int mid_year = max_year;
    while (mid_year >= min_year) {
        fechas_list.append(QString::number(mid_year));
        mid_year--;
    }
    ui->cb_fechas->addItems(fechas_list);
}

void GenListado::print_table()
{
    model->sort(0, Qt::AscendingOrder);
    QString html_table_prendas = generate_html_prendas_table();
    // set path and print table
    QString path = AppSettings::instance()->listadosPrendasPath();
    QString filename = "/listado_prendas_" +
            QDate::currentDate().toString("yyyy-MM-dd") +
            ".pdf";
    // create directory in case it does not exists
    if (!QFile::exists(path))
        QDir().mkpath(path);
    // open file in case it already exists
    if (!QFile::exists(path + filename))
        write_html(path + filename, html_table_prendas);
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(path + filename));
}

QString GenListado::generate_html_prendas_table()
{
    QString html_table;
    html_table = "<!DOCTYPE html>"
        "<html>"
        "<head>"
            "<meta charset='UTF-8'>"
            "<meta http-equiv='X-UA-Compatible' content='IE=edge'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
            "<style>"
                "table, tr, td {"
                    "text-align: left;"
                    "border: 1px solid black;"
                    "border-collapse: collapse;"
                    "padding:2px 3px;"
                "}"
                "th {"
                    "text-align: center;"
                    "border: 1px solid black;"
                    "border-collapse: collapse;"
                    "padding:2px 3px;"
                "}"
            "</style>"
        "</head>"
        "<body>"
            "<p style='text-align:right;'>Granada, " + QDate::currentDate().toString("dd-MM-yyyy") + "</p>"
            "<p><span class='text-small'>" + AppSettings::instance()->businessName() + "</span><br>"
            "<span class='text-small'>" + AppSettings::instance()->businessAddress() + "</span><br>"
            "<span class='text-small'>" + AppSettings::instance()->businessCity() + "</span></p>"
            "<h1 style='text-align:left;'>Listado de Prendas</h1>"
            "<figure class='table' style='float:left;'>"
                "<table>"
                    "<thead>"
                        "<tr>"
                            "<th>Nombre</th>"
                            "<th>Precio Limpieza</th>"
                            "<th>Precio Plancha</th>";
    html_table += "</tr>" "</thead>" "<tbody>";

    // add each line of data
    int row_printed = 0;
    for (int row = 0; row < model->rowCount(); row++) {
        // set background color for even rows
        if (row_printed % 2 == 0)
            html_table += "<tr>";
        else
            html_table += "<tr style='background-color: #E3E1D3;'>";
        // set row content
        html_table += "<td>" + model->index(row, 0).data().toString() + "</td>";
        if (model->index(row, 1).data().toFloat() == 0.0)
            html_table += "<td></td>";
        else
            html_table += "<td><p style='text-align:right;'>"
                    + QString::number(model->index(row, 1).data().toFloat(), 'f', 2)
                    + " €</p></td>";
        if (model->index(row, 2).data().toFloat() == 0.0)
            html_table += "<td></td>";
        else
            html_table += "<td><p style='text-align:right;'>"
                    + QString::number(model->index(row, 2).data().toFloat(), 'f', 2)
                    + " €</p></td>";
        html_table += "</tr>";
        row_printed++;
    }
    html_table += "</tbody>" "</table>" "</figure>" "</body>" "</html>";
    return html_table;
}

void GenListado::write_html(QString filename,
                            QString html)
{
    QTextDocument document;
    document.setHtml(html);

    QPrinter printer(QPrinter::PrinterResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setPageSize(QPageSize::A4);
    printer.setOutputFileName(filename);

    document.print(&printer);

    QDesktopServices::openUrl(QUrl::fromLocalFile(filename));
}

QString GenListado::generate_html_gastos_table_with_specific_conditions()
{
    // agrupar por proveedores if selected
    if (ui->cb_agrupar->currentText() == C_PROVEEDORES)
        model->sort(GASTOS_IDX_CLIENT, Qt::AscendingOrder);
    return generate_html_gastos_table();
}

QString GenListado::generate_html_gastos_table()
{
    QString html_table;
    html_table = "<!DOCTYPE html>"
        "<html>"
        "<head>"
            "<meta charset='UTF-8'>"
            "<meta http-equiv='X-UA-Compatible' content='IE=edge'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
            "<style>"
                "table, tr, td {"
                    "text-align: left;"
                    "border: 1px solid black;"
                    "border-collapse: collapse;"
                    "padding:2px 3px;"
                "}"
                "th {"
                    "text-align: center;"
                    "border: 1px solid black;"
                    "border-collapse: collapse;"
                    "padding:2px 3px;"
                "}"
            "</style>"
        "</head>"
        "<body>"
            "<p style='text-align:right;'>Granada, " + QDate::currentDate().toString("dd-MM-yyyy") + "</p>"
            "<p><span class='text-small'>" + AppSettings::instance()->businessName() + "</span><br>"
            "<span class='text-small'>" + AppSettings::instance()->businessAddress() + "</span><br>"
            "<span class='text-small'>" + AppSettings::instance()->businessCity() + "</span></p>"
            "<h1 style='text-align:left;'>Listado de Gastos</h1>"
            "<figure class='table' style='float:left;'>"
                "<table>"
                    "<thead>"
                        "<tr>"
                            "<th>N. Fra</th>"
                            "<th>Servicio</th>"
                            "<th>Empresa</th>"
                            "<th>Fecha</th>"
                            "<th>IVA<br>[€]</th>"
                            "<th>Base<br>[€]</th>"
                            "<th>Importe<br>[€]</th>";
    if (ui->cb_tipo_gastos->currentText() == C_INCL_TODOS)
        html_table += "<th>Cerrado por contabilidad</th>";
    html_table += "</tr>" "</thead>" "<tbody>";

    // add each line of data
    int row_printed = 0;
    QString client, client_prev_row, importe, iva, base;
    float importe_tot = 0.0;
    for (int row = 0; row < model->rowCount(); row++) {
        // print row based on previous analysis
        if (check_years_invoice_type_for_row(row)) {
            // calculate iva and base
            importe = QString::number(model->index(row, 7).data().toFloat(), 'f', 2);
            if (model->index(row, 6).data().toString() == "21" || model->index(row, 6).data().toString() == "10")
                base = QString::number(importe.toFloat() / (1 + (model->index(row, 6).data().toFloat() / 100)), 'f', 2);
            else
                base = QString::number(importe.toFloat(), 'f', 2);
            iva = QString::number(importe.toFloat() - base.toFloat(), 'f', 2);

            // generate new row if group per clients
            if (ui->cb_agrupar->currentText() == C_PROVEEDORES) {
                client = model->index(row, 4).data().toString();
                if (client != client_prev_row && row_printed != 0) {
                    // set background color for even rows
                    if (row_printed % 2 == 0)
                        html_table += "<tr>";
                    else
                        html_table += "<tr style='background-color: #E3E1D3;'>";
                    // set total costs row
                    html_table +="<td colspan='6', style='text-align:right;'>IMPORTE TOTAL:</td>"
                                        "<td colspan='2', style='text-align:left;'>" + QString::number(importe_tot, 'f', 2) +
                                        " €</td></tr>";
                    importe_tot = 0.0;
                    row_printed++;
                }
                importe_tot += importe.toFloat();
                client_prev_row = client;
            }
            // set background color for even rows
            if (row_printed % 2 == 0)
                html_table += "<tr>";
            else
                html_table += "<tr style='background-color: #E3E1D3;'>";
            // set row content
            html_table +="<td>" + model->index(row, 1).data().toString() + "</td>"
                                "<td>" + model->index(row, 2).data().toString() + "</td>"
                                "<td>" + model->index(row, 4).data().toString() + "</td>"
                                "<td>" + model->index(row, 5).data().toString() + "</td>"
                                "<td>" + iva + "</td>"
                                "<td>" + base + "</td>"
                                "<td>" + importe + "</td>";
            if (ui->cb_tipo_gastos->currentText() == C_INCL_TODOS) {
                if (model->index(row, 8).data().toBool())
                    html_table += "<td>Si</td>";
                else
                    html_table += "<td>No</td>";
            }
            html_table += "</tr>";
            row_printed++;
        }
    }
    // generate new row for last group of clients
    if (ui->cb_agrupar->currentText() == C_PROVEEDORES) {
        // set background color for even rows
        if (row_printed % 2 == 0)
            html_table += "<tr>";
        else
            html_table += "<tr style='background-color: #E3E1D3;'>";
        // set total costs row
        html_table +="<td colspan='6', style='text-align:right;'>IMPORTE TOTAL:</td>"
                            "<td colspan='2', style='text-align:left;'>" + QString::number(importe_tot, 'f', 2) +
                            " €</td></tr>";
    }
    html_table += "</tbody>" "</table>" "</figure>" "</body>" "</html>";
    if (row_printed == 0)
        return C_NO_ROWS;
    else
        return html_table;
}

bool GenListado::check_years_invoice_type_for_row(int row)
{
    // check years
    bool print_current_row_date = false;
    if (!ui->checkb_allys->isChecked()) {
        // print only selected years
        if (ui->cb_fechas->currentText() == model->index(row, GASTOS_IDX_FECHA).data().toString().mid(6, 4))
            print_current_row_date = true;
    }
    else // print all years
        print_current_row_date = true;
    // check type of invoice
    bool print_current_row_cont = false;
    if (ui->cb_tipo_gastos->currentText() == C_CONTAB_CERR) {
        // print only closed accountings
        if (model->index(row, GASTOS_IDX_CONTAB).data().toBool())
            print_current_row_cont = true;
    }
    else // print all types of invoices
        print_current_row_cont = true;
    // return both
    return print_current_row_date && print_current_row_cont;
}

QString GenListado::add_sufix_to_filename()
{
    QString filename_sufix = "";
    // add group of rows
    filename_sufix += "agrupado_" + ui->cb_agrupar->currentText().toLower().replace(" ", "_") + "_";
    // add accountings
    filename_sufix += ui->cb_tipo_gastos->currentText().toLower().replace(" ", "_") + "_";
    // add date info
    if (ui->checkb_allys->isChecked())
        filename_sufix += "todos_los_años";
    else
        filename_sufix += ui->cb_fechas->currentText();

    return filename_sufix;
}

void GenListado::on_bb_ok_cancel_accepted()
{
    // generate html table
    QString html_table_gastos = generate_html_gastos_table_with_specific_conditions();

    if (html_table_gastos == C_NO_ROWS) {
        qCritical() << "GenListado::on_bb_ok_cancel_accepted: no rows to print in 'gastos' table for selected configuration";
        QMessageBox::critical(this, "Generar listado de gastos en pdf.",
                              "No existen gastos para la configuracion seleccionada. Por favor, revise la tabla de gastos.",
                              QMessageBox::Ok,
                              QMessageBox::Ok);
    }
    else {
        // set path and print table
        QString path = AppSettings::instance()->listadosGastosPath();
        QString filename = "/listado_gastos_" +
                QDate::currentDate().toString("yyyy-MM-dd_") +
                add_sufix_to_filename() +
                ".pdf";
        // create directory in case it does not exists
        if (!QFile::exists(path))
            QDir().mkpath(path);
        // open file in case it already exists
        if (!QFile::exists(path + filename))
            write_html(path + filename, html_table_gastos);
        else
            QDesktopServices::openUrl(QUrl::fromLocalFile(path + filename));
    }
    // close at openning ;)
    this->close();
}

void GenListado::on_bb_ok_cancel_rejected()
{
    this->close();
}

void GenListado::on_checkb_allys_clicked(bool checked)
{
    ui->cb_fechas->setDisabled(checked);
}
