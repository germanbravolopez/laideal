#include "ingresos.h"
#include "ui_ingresos.h"
#include "numberformatdelegate.h"
#include "textcolordelegate.h"

Ingresos::Ingresos(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::Ingresos)
{
    ui->setupUi(this);
    populate_table();
    resize_window_to_table();
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
        proxyModel = new MySortFilterProxyModel(this);
        proxyModel->table_name = "ingresos";
        proxyModel->setSourceModel(model);
        ui->table_ingresos->setModel(proxyModel);
        ui->table_ingresos->resizeColumnsToContents();
        ui->table_ingresos->resizeRowsToContents();
        ui->table_ingresos->sortByColumn(COLUMN_IDX_TICKET, Qt::DescendingOrder);
        ui->table_ingresos->setItemDelegateForColumn(COLUMN_IDX_IMPORTE, new NumberFormatDelegate(this));
        ui->table_ingresos->setItemDelegateForColumn(COLUMN_IDX_PAYED, new TextColorDelegate(ui->table_ingresos, this));
        ui->table_ingresos->setItemDelegateForColumn(COLUMN_IDX_STATE, new TextColorDelegate(ui->table_ingresos, this));
        ui->statusBar->showMessage("Modo edición desactivado");
    }
}

void Ingresos::resize_window_to_table()
{
    // Set window size to minimun of size of the table
    int size = 0;
    for (int column = 0; column < model->columnCount(); column++) {
        size += ui->table_ingresos->columnWidth(column);
    }
    if (this->width() < size + 40) {
        this->resize(size + 40, this->height());
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
