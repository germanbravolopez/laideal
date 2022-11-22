#include "gastos.h"
#include "ui_gastos.h"

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
        QSqlTableModel *model = new QSqlTableModel(this, QSqlDatabase::database("qt_sql_default_connection"));
        model->setTable("gastos");
        model->setEditStrategy(QSqlTableModel::OnManualSubmit);
        model->select();
        ui->table_gastos->setModel(model);
        ui->table_gastos->resizeColumnsToContents();
        ui->table_gastos->sortByColumn(FECHA_COLUMN_IDX, Qt::AscendingOrder);
    }
}

void Gastos::on_actionActualizar_triggered()
{
    populate_table();
}
