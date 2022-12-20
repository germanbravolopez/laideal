#include "lista_proveedores.h"
#include "ui_lista_proveedores.h"
#include "tableview.h"

ListaProveedores::ListaProveedores(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ListaProveedores)
{
    ui->setupUi(this);
    populate_table();
    connect(ui->table_lista_proveedores->action1, SIGNAL(triggered()),
            this, SLOT(on_actionAnadir_fila_triggered()));
    connect(ui->table_lista_proveedores->action2, SIGNAL(triggered()),
            this, SLOT(on_actionEliminar_fila_triggered()));
}

ListaProveedores::~ListaProveedores()
{
    delete ui;
}

void ListaProveedores::populate_table()
{
    if (QSqlDatabase::contains("qt_sql_default_connection"))
    {
        model = new QSqlTableModel(this, QSqlDatabase::database("qt_sql_default_connection"));
        model->setTable("proveedores");
        model->setEditStrategy(QSqlTableModel::OnFieldChange);
        model->select();
        ui->table_lista_proveedores->setModel(model);
        ui->table_lista_proveedores->resizeColumnsToContents();
        ui->table_lista_proveedores->sortByColumn(NOMBRE_COLUMN_IDX, Qt::AscendingOrder);
    }
}

void ListaProveedores::on_actionActualizar_triggered()
{
    populate_table();
}

void ListaProveedores::on_actionAnadir_fila_triggered()
{
    model->insertRow(ui->table_lista_proveedores->currentIndex().row() + 1);
}

void ListaProveedores::on_actionEliminar_fila_triggered()
{
    int ret = QMessageBox::question(this, "Eliminar fila",
                                    "¿Está seguro que desea eliminar la fila " +
                                    QString::number(ui->table_lista_proveedores->currentIndex().row() + 1) + "?",
                                    QMessageBox::Yes | QMessageBox::No,
                                    QMessageBox::No);
    if (ret == QMessageBox::Yes)
    {
        model->removeRow(ui->table_lista_proveedores->currentIndex().row());
        populate_table();
    }
}
