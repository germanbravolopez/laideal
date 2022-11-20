#include "lista_proveedores.h"
#include "ui_lista_proveedores.h"

ListaProveedores::ListaProveedores(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ListaProveedores)
{
    ui->setupUi(this);
    populate_table();
}

ListaProveedores::~ListaProveedores()
{
    delete ui;
}

void ListaProveedores::populate_table()
{
    if (QSqlDatabase::contains("qt_sql_default_connection"))
    {
        QSqlTableModel *model = new QSqlTableModel(this, QSqlDatabase::database("qt_sql_default_connection"));
        model->setTable("proveedores");
        model->setEditStrategy(QSqlTableModel::OnManualSubmit);
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
