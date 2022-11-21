#include "anadirproveedor.h"
#include "ui_anadirproveedor.h"
#include "sql_lite.h"

AnadirProveedor::AnadirProveedor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AnadirProveedor)
{
    ui->setupUi(this);
    anadirproveedor_initial_settings();
}

AnadirProveedor::~AnadirProveedor()
{
    delete ui;
}

void AnadirProveedor::anadirproveedor_initial_settings()
{
    // load items in cb
    ui->cb_empresa->addItems(read_column_from_table(db, "empresa", "proveedores"));
    ui->cb_empresa->setCurrentText("");
    // clear all line editors
    ui->le_cif->clear();
    ui->le_direccion->clear();
    ui->le_telefono->clear();
    // set focus on name
    ui->cb_empresa->setFocus();
}

void AnadirProveedor::on_bb_ok_cancel_clicked(QAbstractButton *button)
{
    if (button == ui->bb_ok_cancel->button(QDialogButtonBox::Cancel))
    {
        this->reject();
    }
    else if (button == ui->bb_ok_cancel->button(QDialogButtonBox::Ok))
    {
        if (check_supplier_is_not_in_db())
        {
            add_supplier_to_db();
            this->accept();
        }
        else
        {
            int ret = QMessageBox::question(this, tr("Sobreescribir datos"),
                                            tr("El nombre de la empresa introducido se encuentra en la tabla de proveedores.\n"
                                               "¿Desea sobreescribir los datos de la empresa con los datos introducidos?"),
                                            QMessageBox::Yes | QMessageBox::No,
                                            QMessageBox::No);
            if (ret == QMessageBox::Yes)
            {
                update_supplier_to_db();
                this->accept();
            }
            else
                this->reject();
        }
    }
}

bool AnadirProveedor::check_supplier_is_not_in_db()
{
    QString text = select_from_where_like(db, "empresa", "proveedores", "empresa", ui->cb_empresa->currentText(), true);
    if (text.isEmpty())
        return 1;
    else
        return 0;
}

void AnadirProveedor::add_supplier_to_db()
{
    db.open();
    QSqlQuery q;
    q.prepare("INSERT INTO proveedores (empresa, cif, direccion, telefono) \
    VALUES (:empresa, :cif, :direccion, :telefono);");
    q.bindValue(":empresa", ui->cb_empresa->currentText());
    q.bindValue(":cif", ui->le_cif->text());
    q.bindValue(":direccion", ui->le_direccion->text());
    q.bindValue(":telefono", ui->le_telefono->text());
    q.exec();
    q.clear();
    db.close();
}

void AnadirProveedor::update_supplier_to_db()
{
    // Get exact name of the supplier in database
    QString text = select_from_where_like(db, "empresa", "proveedores", "empresa", ui->cb_empresa->currentText(), true);
    // Update database
    db.open();
    QSqlQuery q;
    q.prepare("UPDATE proveedores SET cif = :cif, direccion = :direccion, telefono = :telefono WHERE empresa = :empresa");
    q.bindValue(":empresa", text);
    q.bindValue(":cif", ui->le_cif->text());
    q.bindValue(":direccion", ui->le_direccion->text());
    q.bindValue(":telefono", ui->le_telefono->text());
    q.exec();
    q.clear();
    db.close();
}

void AnadirProveedor::on_cb_empresa_editTextChanged(const QString &arg1)
{
    if (arg1 != "")
    {
        ui->le_cif->setText(select_from_where_like(db, "cif", "proveedores", "empresa", arg1, false));
        ui->le_direccion->setText(select_from_where_like(db, "direccion", "proveedores", "empresa", arg1, false));
        ui->le_telefono->setText(select_from_where_like(db, "telefono", "proveedores", "empresa", arg1, false));
    }
}
