#include "genlistado.h"
#include "ui_genlistado.h"
#include "qprinter.h"
#include "sql_lite.h"

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
    int max_year = read_max_n_min_year_in_column_from_table(db, true, "fecha", "gastos");
    int min_year = read_max_n_min_year_in_column_from_table(db, false, "fecha", "gastos");
    QStringList fechas_list;
    int mid_year = max_year;
    while (mid_year >= min_year) {
        fechas_list.append(QString::number(mid_year));
        mid_year--;
    }
    ui->cb_fechas->addItems(fechas_list);
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

QString GenListado::generate_table_with_specific_conditions()
{
    // agrupar por proveedores if selected
    if (ui->cb_agrupar->currentText() == C_PROVEEDORES)
        model->sort(C_CLIENT_COLUMN_IDX, Qt::AscendingOrder);
    return generate_html_table();
}

QString GenListado::generate_html_table()
{
    QString html_table_gastos;
    html_table_gastos = "<!DOCTYPE html>"
        "<html>"
        "<head>"
            "<meta charset='UTF-8'>"
            "<meta http-equiv='X-UA-Compatible' content='IE=edge'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
            "<style>"
                "table,tr, th, td {"
                    "border: 1px solid black;"
                    "border-collapse: collapse;"
                "}"
                "td {"
                    "text-align: left;"
                "}"
            "</style>"
        "</head>"
        "<body>"
            "<p style='text-align:right;'>Granada, " + QDate::currentDate().toString("dd-MM-yyyy") + "</p>"
            "<p><span class='text-small'>Tintorería La Ideal</span><br><span class='text-small'>Plaza San Pantaleón 1, bajo 2</span><br><span class='text-small'>18012 Granada</span></p>"
            "<h1 style='text-align:left;'>Listado de Gastos</h1>"
            "<figure class='table' style='float:left;'>"
                "<table>"
                    "<thead>"
                        "<tr>"
                            "<th>&nbsp;N. Fra&nbsp;</th>"
                            "<th>&nbsp;Servicio&nbsp;</th>"
                            "<th>&nbsp;Descripción&nbsp;</th>"
                            "<th>&nbsp;Empresa&nbsp;</th>"
                            "<th>&nbsp;Fecha&nbsp;</th>"
                            "<th>&nbsp;IVA [€]&nbsp;</th>"
                            "<th>&nbsp;Base [€]&nbsp;</th>"
                            "<th>&nbsp;Importe [€]&nbsp;</th>";
    if (ui->cb_tipo_gastos->currentText() == C_INCL_TODOS)
        html_table_gastos += "<th>&nbsp;Cerrado por contabilidad&nbsp;</th>";
    html_table_gastos += "</tr>" "</thead>" "<tbody>";

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
                iva = QString::number(model->index(row, 6).data().toFloat() * importe.toFloat() / 100, 'f', 2);
            else
                iva = QString::number(0.0, 'f', 2);
            base = QString::number(importe.toFloat() - iva.toFloat(), 'f', 2);
            // generate new row if group per clients
            if (ui->cb_agrupar->currentText() == C_PROVEEDORES) {
                client = model->index(row, 4).data().toString();
                if (client != client_prev_row && row_printed != 0) {
                    // set background color for even rows
                    if (row_printed % 2 == 0)
                        html_table_gastos += "<tr>";
                    else
                        html_table_gastos += "<tr style='background-color: #E3E1D3;'>";
                    // set total costs row
                    html_table_gastos +="<td colspan='7', style='text-align:right;'>&nbsp;IMPORTE TOTAL:&nbsp;</td>"
                                        "<td colspan='2', style='text-align:left;'>&nbsp;" + QString::number(importe_tot, 'f', 2) +
                                        "&nbsp;</td></tr>";
                    importe_tot = 0.0;
                    row_printed++;
                }
                importe_tot += importe.toFloat();
                client_prev_row = client;
            }
            // set background color for even rows
            if (row_printed % 2 == 0)
                html_table_gastos += "<tr>";
            else
                html_table_gastos += "<tr style='background-color: #E3E1D3;'>";
            // set row content
            html_table_gastos +="<td>&nbsp;" + model->index(row, 1).data().toString() + "&nbsp;</td>"
                                "<td>&nbsp;" + model->index(row, 2).data().toString() + "&nbsp;</td>"
                                "<td>&nbsp;" + model->index(row, 3).data().toString() + "&nbsp;</td>"
                                "<td>&nbsp;" + model->index(row, 4).data().toString() + "&nbsp;</td>"
                                "<td>&nbsp;" + model->index(row, 5).data().toString() + "&nbsp;</td>"
                                "<td>&nbsp;" + iva + "&nbsp;</td>"
                                "<td>&nbsp;" + base + "&nbsp;</td>"
                                "<td>&nbsp;" + importe + "&nbsp;</td>";
            if (ui->cb_tipo_gastos->currentText() == C_INCL_TODOS) {
                if (model->index(row, 8).data().toBool())
                    html_table_gastos += "<td>&nbsp;Si&nbsp;</td>";
                else
                    html_table_gastos += "<td>&nbsp;No&nbsp;</td>";
            }
            html_table_gastos += "</tr>";
            row_printed++;
        }
    }
    // generate new row for last group of clients
    if (ui->cb_agrupar->currentText() == C_PROVEEDORES) {
        // set background color for even rows
        if (row_printed % 2 == 0)
            html_table_gastos += "<tr>";
        else
            html_table_gastos += "<tr style='background-color: #E3E1D3;'>";
        // set total costs row
        html_table_gastos +="<td colspan='7', style='text-align:right;'>&nbsp;IMPORTE TOTAL:&nbsp;</td>"
                            "<td colspan='2', style='text-align:left;'>&nbsp;" + QString::number(importe_tot, 'f', 2) +
                            "&nbsp;</td></tr>";
    }
    html_table_gastos += "</tbody>" "</table>" "</figure>" "</body>" "</html>";
    if (row_printed == 0)
        return C_NO_ROWS;
    else
        return html_table_gastos;
}

bool GenListado::check_years_invoice_type_for_row(int row)
{
    // check years
    bool print_current_row_date = false;
    if (!ui->checkb_allys->isChecked()) {
        // print only selected years
        if (ui->cb_fechas->currentText() == model->index(row, C_FECHA_COLUMN_IDX).data().toString().mid(6, 4))
            print_current_row_date = true;
    }
    else // print all years
        print_current_row_date = true;
    // check type of invoice
    bool print_current_row_cont = false;
    if (ui->cb_tipo_gastos->currentText() == C_CONTAB_CERR) {
        // print only closed accountings
        if (model->index(row, C_CONTAB_COLUMN_IDX).data().toBool())
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
    QString html_table_gastos = generate_table_with_specific_conditions();

    if (html_table_gastos == C_NO_ROWS)
        QMessageBox::critical(this, "Generar listado de gastos en pdf.",
                              "No existen gastos para la configuracion seleccionada. Por favor, revise la tabla de gastos.",
                              QMessageBox::Ok,
                              QMessageBox::Ok);
    else {
        // set path and print table
        QString path = "C:/Users/Usuario/OneDrive/Desktop/Tintoreria/Listados_gastos";
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
