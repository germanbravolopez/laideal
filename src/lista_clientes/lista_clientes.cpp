#include "lista_clientes.h"
#include "ui_lista_clientes.h"
#include "anadircliente.h"

ListaClientes::ListaClientes(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ListaClientes)
{
    ui->setupUi(this);
    populate_table();
}

ListaClientes::~ListaClientes()
{
    delete ui;
}

void ListaClientes::populate_table()
{
    if (QSqlDatabase::contains("qt_sql_default_connection"))
    {
        QSqlTableModel *model = new QSqlTableModel(this, QSqlDatabase::database("qt_sql_default_connection"));
        model->setTable("clientes");
        model->setEditStrategy(QSqlTableModel::OnFieldChange);
        model->select();
        ui->table_lista_clientes->setModel(model);
        ui->table_lista_clientes->resizeColumnsToContents();
        ui->table_lista_clientes->sortByColumn(NOMBRE_COLUMN_IDX, Qt::AscendingOrder);
    }
}

void ListaClientes::on_actionActualizar_triggered()
{
    populate_table();
}

void ListaClientes::on_actionAnadir_cliente_triggered()
{
    AnadirCliente *ui_add_cliente;
    ui_add_cliente = new AnadirCliente(this);
    ui_add_cliente->db = db;
    ui_add_cliente->setModal(true);
    if (ui_add_cliente->exec() == QDialog::Accepted)
    {
        qDebug() << "Exited pressing OK";
    }
    else
    {
        qDebug() << "Exited pressing Cancel";
    }
    populate_table();
}
