#include "facturas.h"
#include "ui_facturas.h"
#include "sql_lite.h"

Facturas::Facturas(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::Facturas)
{
    ui->setupUi(this);
    initial_settings();
}

Facturas::~Facturas()
{
    delete ui;
}

void Facturas::initial_settings()
{
    QStringList iva_list = {"", "10", "21"};
    ui->cb_iva->addItems(iva_list);
    reset_all_contents();
}

void Facturas::reset_all_contents()
{
    ui->le_fra->clear();
    ui->de_fecha->setDate(QDate::currentDate());
    ui->le_servicio->clear();
    ui->le_producto->clear();
    ui->cb_empresa->setCurrentText("");
    ui->cb_iva->setCurrentText("");
    ui->le_importe->clear();
}

void Facturas::populate_empresas()
{
    QStringList empresas_names = read_column_from_table(db, "nombre", "proveedores");
    ui->cb_empresa->addItems(empresas_names);
    ui->cb_empresa->setCurrentText("");
}

void Facturas::save_factura()
{

}

void Facturas::on_buttonBox_clicked(QAbstractButton *button)
{
    if (button == ui->buttonBox->button(QDialogButtonBox::Cancel))
    {
        this->close();
    }
    else if (button == ui->buttonBox->button(QDialogButtonBox::Reset))
    {
        reset_all_contents();
    }
    else if (button == ui->buttonBox->button(QDialogButtonBox::Save))
    {
        save_factura();
        reset_all_contents();
    }
    else
        QMessageBox::critical(this, "Formulario facturas",
                              "Boton no definido.",
                              QMessageBox::Ok, QMessageBox::Ok);
}
