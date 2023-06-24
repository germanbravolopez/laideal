#include "gastos.h"
#include "ui_gastos.h"
#include "qprinter.h"

Gastos::Gastos(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::Gastos)
{
    ui->setupUi(this);
    populate_table();
}

Gastos::~Gastos()
{
    delete ui;
}

void Gastos::populate_table()
{
    if (QSqlDatabase::contains("qt_sql_default_connection"))
    {
        model = new QSqlTableModel(this, QSqlDatabase::database("qt_sql_default_connection"));
        model->setTable("gastos");
        model->setEditStrategy(QSqlTableModel::OnManualSubmit);
        model->select();
        proxyModel = new MySortFilterProxyModel(this);
        proxyModel->setSourceModel(model);
        ui->table_gastos->setModel(proxyModel);
        ui->table_gastos->resizeColumnsToContents();
        ui->table_gastos->sortByColumn(FECHA_COLUMN_IDX, Qt::AscendingOrder);
        ui->statusBar->showMessage("Modo edición desactivado");
    }
}

void Gastos::on_actionActualizar_triggered()
{
    populate_table();
}

void Gastos::on_actionActivar_modo_edicion_triggered()
{
    model->setEditStrategy(QSqlTableModel::OnFieldChange);
    ui->statusBar->showMessage("Modo edición activado");
}

void Gastos::on_actionDesactivar_modo_edicion_triggered()
{
    model->setEditStrategy(QSqlTableModel::OnManualSubmit);
    ui->statusBar->showMessage("Modo edición desactivado");
}

void Gastos::on_actionAnadir_fila_triggered()
{
    model->insertRow(ui->table_gastos->currentIndex().row() + 1);
}


void Gastos::on_actionEliminar_fila_triggered()
{
    int ret = QMessageBox::question(this, "Eliminar fila",
                                        "¿Está seguro que desea eliminar la fila " +
                                        QString::number(ui->table_gastos->currentIndex().row() + 1) + "?",
                                        QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No);
    if (ret == QMessageBox::Yes)
    {
        model->removeRow(ui->table_gastos->currentIndex().row());
        populate_table();
    }
}

void Gastos::on_actionGenerar_pdf_con_el_listado_triggered()
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

void Gastos::write_html(QString filename,
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

QString Gastos::generate_html_table()
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
        html_table_gastos +="<td>&nbsp;" + ui->table_gastos->model()->index(row, 1).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + ui->table_gastos->model()->index(row, 2).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + ui->table_gastos->model()->index(row, 3).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + ui->table_gastos->model()->index(row, 4).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + ui->table_gastos->model()->index(row, 5).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + ui->table_gastos->model()->index(row, 6).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + ui->table_gastos->model()->index(row, 7).data().toString() + "&nbsp;</td>"
                            "<td>&nbsp;" + ui->table_gastos->model()->index(row, 8).data().toString() + "&nbsp;</td>"
                            "</tr>";
    }
    html_table_gastos += "</tbody>" "</table>" "</figure>" "</body>" "</html>";
    return html_table_gastos;
}
