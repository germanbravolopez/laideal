#include "lista_servicios.h"
#include "ui_lista_servicios.h"
#include "tableview.h"
#include "sql_lite.h"
#include "numberformatdelegate.h"

ListaServicios::ListaServicios(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ListaServicios)
{
    ui->setupUi(this);
    populate_table();
    connect(ui->table_lista_servicios->action1, SIGNAL(triggered()),
            this, SLOT(on_actionAnadir_fila_triggered()));
    connect(ui->table_lista_servicios->action2, SIGNAL(triggered()),
            this, SLOT(on_actionEliminar_fila_triggered()));
}

ListaServicios::~ListaServicios()
{
    delete ui;
}

void ListaServicios::populate_table()
{
    if (QSqlDatabase::contains("qt_sql_default_connection"))
    {
        model = new QSqlTableModel(this, QSqlDatabase::database("qt_sql_default_connection"));
        model->setTable("servicios");
        model->setEditStrategy(QSqlTableModel::OnFieldChange);
        model->select();
        ui->table_lista_servicios->setModel(model);
        ui->table_lista_servicios->resizeColumnsToContents();
        ui->table_lista_servicios->sortByColumn(NOMBRE_COLUMN_IDX, Qt::AscendingOrder);
        ui->table_lista_servicios->setItemDelegateForColumn(1, new NumberFormatDelegate(this));
        ui->table_lista_servicios->setItemDelegateForColumn(2, new NumberFormatDelegate(this));
    }
}

void ListaServicios::on_actionActualizar_triggered()
{
    populate_table();
}

void ListaServicios::on_actionAnadir_fila_triggered()
{
    model->insertRow(ui->table_lista_servicios->currentIndex().row() + 1);
    insert_new_item_to_table(db, {""}, "servicios");
    populate_table();
}

void ListaServicios::on_actionEliminar_fila_triggered()
{
    int ret = QMessageBox::question(this, "Eliminar fila",
                                    "¿Está seguro que desea eliminar la fila " +
                                    QString::number(ui->table_lista_servicios->currentIndex().row() + 1) + "?",
                                    QMessageBox::Yes | QMessageBox::No,
                                    QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        model->removeRow(ui->table_lista_servicios->currentIndex().row());
        populate_table();
    }
}
