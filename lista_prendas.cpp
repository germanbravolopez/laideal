#include "lista_prendas.h"
#include "ui_lista_prendas.h"

ListaPrendas::ListaPrendas(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ListaPrendas)
{
    ui->setupUi(this);
    populate_table();
}

ListaPrendas::~ListaPrendas()
{
    delete ui;
}

void ListaPrendas::populate_table()
{
    if (QSqlDatabase::contains("qt_sql_default_connection"))
    {
        QSqlTableModel *model = new QSqlTableModel(this, QSqlDatabase::database("qt_sql_default_connection"));
        model->setTable("prendas");
        model->setEditStrategy(QSqlTableModel::OnManualSubmit);
        model->select();
        ui->table_lista_prendas->setModel(model);
        ui->table_lista_prendas->resizeColumnsToContents();
    }
}

void ListaPrendas::on_actionActualizar_triggered()
{
    populate_table();
}
