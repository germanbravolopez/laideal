#include "lista_prendas.h"
#include "ui_lista_prendas.h"
#include "anadirprenda.h"

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

void ListaPrendas::on_actionAnadir_prenda_triggered()
{
    AnadirPrenda *ui_add_prend;
    ui_add_prend = new AnadirPrenda(this);
    ui_add_prend->db = db;
    ui_add_prend->setModal(true);
    if (ui_add_prend->exec() == QDialog::Accepted)
    {
        qDebug() << "Exited pressing OK";
    }
    else
    {
        qDebug() << "Exited pressing Cancel";
    }
    populate_table();
}
