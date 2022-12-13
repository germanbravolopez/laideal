#include "lista_proveedores.h"
#include "ui_lista_proveedores.h"
#include "anadirproveedor.h"

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

void ListaProveedores::on_actionAnadir_proveedor_triggered()
{
    AnadirProveedor *ui_add_prov;
    ui_add_prov = new AnadirProveedor(this);
    ui_add_prov->db = db;
    ui_add_prov->setModal(true);
    if (ui_add_prov->exec() == QDialog::Accepted)
    {
        qDebug() << "Exited pressing OK";
    }
    else
    {
        qDebug() << "Exited pressing Cancel";
    }
    populate_table();
}
