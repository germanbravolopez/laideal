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

bool Facturas::save_factura()
{
    if (ui->le_fra->text() != "" && ui->le_servicio->text() != "" && ui->cb_empresa->currentText() != "" && ui->le_importe->text() != "")
    {
        if (!ui->le_importe->text().contains(","))
        {
            db.open();
            QSqlQuery q;
            q.prepare("INSERT INTO gastos (n_factura, servicio, producto, empresa, fecha, iva, importe, edit_lock) "
                      "VALUES (:n_factura, :servicio, :producto, :empresa, :fecha, :iva, :importe, :edit_lock);");
            q.bindValue(":n_factura", ui->le_fra->text());
            q.bindValue(":servicio", ui->le_servicio->text());
            q.bindValue(":producto", ui->le_producto->text());
            q.bindValue(":empresa", ui->cb_empresa->currentText());
            q.bindValue(":fecha", ui->de_fecha->date().toString("dd-MM-yyyy"));
            q.bindValue(":iva", ui->cb_iva->currentText());
            q.bindValue(":importe", ui->le_importe->text().replace(",","."));
            q.bindValue(":edit_lock", "0");
            q.exec();
            q.clear();
            db.close();
            return 1;
        }
        else
        {
            QMessageBox::warning(this, "Formulario factura",
                                 "Formulario incompleto.\n"
                                 "Para poder guardar la factura, el Importe decimal debe introducirse usando '.' como separador",
                                 QMessageBox::Ok, QMessageBox::Ok);
            return 0;
        }
    }
    else
    {
        QMessageBox::warning(this, "Formulario factura",
                             "Formulario incompleto.\n"
                             "Para poder guardar la factura, al menos es necesario rellenar los siguientes campos: Nº Fra., Servicio, Empresa e Importe",
                             QMessageBox::Ok, QMessageBox::Ok);
        return 0;
    }
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
        if (save_factura())
            reset_all_contents();
    }
    else
        QMessageBox::critical(this, "Formulario factura",
                              "Boton no definido.",
                              QMessageBox::Ok, QMessageBox::Ok);
}
