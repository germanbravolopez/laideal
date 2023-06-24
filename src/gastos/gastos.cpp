#include "gastos.h"
#include "ui_gastos.h"
#include "generar_listado.h"

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
    GenerarListado *ui_generar_listado;
    ui_generar_listado = new GenerarListado(this);
    ui_generar_listado->db = db;
    ui_generar_listado->model = ui->table_gastos->model();
    ui_generar_listado->show();
}
