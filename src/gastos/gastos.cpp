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
    // prepare table
    ui->table_gastos->hideColumn(model->columnCount() - 1);
    ui->table_gastos->hideColumn(0);

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
        write_html(path + filename);
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(path + filename));

    // reset table
    ui->table_gastos->showColumn(model->columnCount() - 1);
    ui->table_gastos->showColumn(0);
}

void Gastos::write_html(QString filename)
{
    QPrinter printer(QPrinter::PrinterResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setPageSize(QPageSize::A4);
    printer.setOutputFileName(filename);

    QPainter painter;
    if (!painter.begin(&printer)) {
        qWarning("No se pudo abrir el archivo de impresión");
        return;
    }

    ui->table_gastos->render(&painter);
    painter.end();

    QDesktopServices::openUrl(QUrl::fromLocalFile(filename));
}
