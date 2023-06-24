#include "generar_listado.h"
#include "ui_generar_listado.h"
#include "qprinter.h"
#include "sql_lite.h"

GenerarListado::GenerarListado(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::GenerarListado)
{
    ui->setupUi(this);
    initial_settings();
}

GenerarListado::~GenerarListado()
{
    delete ui;
}

void GenerarListado::initial_settings()
{
    set_cb_fechas();
    ui->cb_agrupar->addItems({"Fechas", "Proveedores"});
    ui->cb_tipo_gastos->addItems({"Incluir todos", "Contabilidad cerrada"});
}

void GenerarListado::set_cb_fechas()
{
    int max_year = read_max_n_min_year_in_column_from_table(db, true, "fecha", "gastos");
    int min_year = read_max_n_min_year_in_column_from_table(db, false, "fecha", "gastos");
    QStringList fechas_list;
    int mid_year = max_year;
    while (mid_year >= min_year)
    {
        fechas_list.append(QString::number(mid_year));
        mid_year--;
    }
    ui->cb_fechas->addItems(fechas_list);
}

void GenerarListado::write_html(QString filename,
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

QString GenerarListado::generate_html_table()
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
            "<h1 style='text-align:center;'>Listado de Gastos</h1>"
            "<figure class='table' style='float:left;'>"
                "<table>"
                    "<thead>"
                        "<tr>"
                            "<th>&nbsp;N. Fra&nbsp;</th>"
                            "<th>&nbsp;Servicio&nbsp;</th>"
                            "<th>&nbsp;Producto&nbsp;</th>"
                            "<th>&nbsp;Empresa&nbsp;</th>"
                            "<th>&nbsp;Fecha&nbsp;</th>"
                            "<th>&nbsp;IVA&nbsp;</th>"
                            "<th>&nbsp;Importe&nbsp;</th>"
                            "<th>&nbsp;Cerrado por contabilidad&nbsp;</th>"
                        "</tr>"
                    "</thead>"
                    "<tbody>";
    // add each line of data
    for (int row = 0; row < model->rowCount(); row++)
    {
        // set background color for even rows
        if (row % 2 == 0)
            html_table_gastos += "<tr>";
        else
            html_table_gastos += "<tr style='background-color: #E3E1D3;'>";
        // set row content
        html_table_gastos +="<td>&nbsp;" + model->index(row, 1).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + model->index(row, 2).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + model->index(row, 3).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + model->index(row, 4).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + model->index(row, 5).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + model->index(row, 6).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + model->index(row, 7).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + model->index(row, 8).data().toString() + "&nbsp;</td>"
                            "</tr>";
    }
    html_table_gastos += "</tbody>" "</table>" "</figure>" "</body>" "</html>";
    return html_table_gastos;
}

void GenerarListado::on_buttonBox_accepted()
{
    // generate html table
    QString html_table_gastos = generate_html_table();

    // set path and print table
    QString path = "C:/Users/Usuario/OneDrive/Desktop/Tintoreria/Listados_gastos";
    QString filename = "/listado_gastos_" +
            QDate::currentDate().toString("yyyy-MM-dd") +
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

void GenerarListado::on_bb_ok_cancel_rejected()
{
    this->close();
}

void GenerarListado::on_checkb_allys_clicked(bool checked)
{
    ui->cb_fechas->setDisabled(checked);
}
