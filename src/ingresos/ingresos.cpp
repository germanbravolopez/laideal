#include "ingresos.h"
#include "ui_ingresos.h"

Ingresos::Ingresos(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::Ingresos)
{
    ui->setupUi(this);
    populate_table();
}

Ingresos::~Ingresos()
{
    delete ui;
}

void Ingresos::populate_table()
{
    if (QSqlDatabase::contains("qt_sql_default_connection"))
    {
        model = new QSqlTableModel(this, QSqlDatabase::database("qt_sql_default_connection"));
        model->setTable("ingresos");
        model->setEditStrategy(QSqlTableModel::OnManualSubmit);
        model->select();
        ui->table_ingresos->setModel(model);
        ui->table_ingresos->resizeColumnsToContents();
        ui->table_ingresos->sortByColumn(TICKET_COLUMN_IDX, Qt::AscendingOrder);
        ui->statusBar->showMessage("Modo edición desactivado");
    }
}

void Ingresos::on_actionActualizar_triggered()
{
    populate_table();
}

void Ingresos::on_actionActivar_modo_edicion_triggered()
{
    model->setEditStrategy(QSqlTableModel::OnFieldChange);
    ui->statusBar->showMessage("Modo edición activado");
}

void Ingresos::on_actionDesactivar_modo_edicion_triggered()
{
    model->setEditStrategy(QSqlTableModel::OnManualSubmit);
    ui->statusBar->showMessage("Modo edición desactivado");
}
