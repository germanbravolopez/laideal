#include "lista_prendas.h"
#include "ui_lista_prendas.h"
#include "tableview.h"
#include "sql_lite.h"
#include "numberformatdelegate.h"

ListaPrendas::ListaPrendas(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ListaPrendas)
{
    ui->setupUi(this);
    populate_table();
    connect(ui->table_lista_prendas->action1, SIGNAL(triggered()),
            this, SLOT(on_actionAnadir_fila_triggered()));
    connect(ui->table_lista_prendas->action2, SIGNAL(triggered()),
            this, SLOT(on_actionEliminar_fila_triggered()));
}

ListaPrendas::~ListaPrendas()
{
    delete ui;
}

void ListaPrendas::populate_table()
{
    if (QSqlDatabase::contains("qt_sql_default_connection"))
    {
        model = new QSqlTableModel(this, QSqlDatabase::database("qt_sql_default_connection"));
        model->setTable("prendas");
        model->setEditStrategy(QSqlTableModel::OnFieldChange);
        model->select();
        ui->table_lista_prendas->setModel(model);
        ui->table_lista_prendas->resizeColumnsToContents();
        ui->table_lista_prendas->sortByColumn(NOMBRE_COLUMN_IDX, Qt::AscendingOrder);
        ui->table_lista_prendas->setItemDelegateForColumn(1, new NumberFormatDelegate(this));
        ui->table_lista_prendas->setItemDelegateForColumn(2, new NumberFormatDelegate(this));
    }
}

void ListaPrendas::on_actionActualizar_triggered()
{
    populate_table();
}

void ListaPrendas::on_actionAnadir_fila_triggered()
{
    model->insertRow(ui->table_lista_prendas->currentIndex().row() + 1);
    insert_new_item_to_table(db, {"", "", ""}, "prendas");
    populate_table();
}

void ListaPrendas::on_actionEliminar_fila_triggered()
{
    int ret = QMessageBox::question(this, "Eliminar fila",
                                    "¿Está seguro que desea eliminar la fila " +
                                    QString::number(ui->table_lista_prendas->currentIndex().row() + 1) + "?",
                                    QMessageBox::Yes | QMessageBox::No,
                                    QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        model->removeRow(ui->table_lista_prendas->currentIndex().row());
        populate_table();
    }
}

void ListaPrendas::closeEvent(QCloseEvent* event)
{
    emit populate_prendas();
    event->accept();
}
