#include "lista_clientes.h"
#include "ui_lista_clientes.h"
#include "tableview.h"
#include "sql_lite.h"

ListaClientes::ListaClientes(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ListaClientes)
{
    ui->setupUi(this);
    populate_table();
    connect(ui->table_lista_clientes->action1, SIGNAL(triggered()),
            this, SLOT(on_actionAnadir_fila_triggered()));
    connect(ui->table_lista_clientes->action2, SIGNAL(triggered()),
            this, SLOT(on_actionEliminar_fila_triggered()));
    resize_window_to_table();
}

ListaClientes::~ListaClientes()
{
    delete ui;
}

void ListaClientes::populate_table()
{
    if (QSqlDatabase::contains("qt_sql_default_connection"))
    {
        model = new QSqlTableModel(this, QSqlDatabase::database("qt_sql_default_connection"));
        model->setTable("clientes");
        model->setEditStrategy(QSqlTableModel::OnFieldChange);
        model->select();
        ui->table_lista_clientes->setModel(model);
        ui->table_lista_clientes->resizeColumnsToContents();
        ui->table_lista_clientes->sortByColumn(NOMBRE_COLUMN_IDX, Qt::AscendingOrder);
    }
}

void ListaClientes::resize_window_to_table()
{
    // Set window size to minimun of size of the table
    int size = 0;
    for (int column = 0; column < model->columnCount(); column++) {
        size += ui->table_lista_clientes->columnWidth(column);
    }
    if (this->width() < size + 40) {
        this->resize(size + 40, this->height());
    }
}

void ListaClientes::on_actionActualizar_triggered()
{
    populate_table();
}

void ListaClientes::on_actionAnadir_fila_triggered()
{
    model->insertRow(ui->table_lista_clientes->currentIndex().row() + 1);
    insert_new_item_to_table(db, {"", "", "", ""}, "clientes");
    populate_table();
}

void ListaClientes::on_actionEliminar_fila_triggered()
{
    int ret = QMessageBox::question(this, "Eliminar fila",
                                    "¿Está seguro que desea eliminar la fila " +
                                    QString::number(ui->table_lista_clientes->currentIndex().row() + 1) + "?",
                                    QMessageBox::Yes | QMessageBox::No,
                                    QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        model->removeRow(ui->table_lista_clientes->currentIndex().row());
        populate_table();
    }
}

void ListaClientes::closeEvent(QCloseEvent* event)
{
    emit populate_clientes();
    event->accept();
}
