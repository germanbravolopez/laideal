#include "anadircliente.h"
#include "ui_anadircliente.h"
#include "sql_lite.h"

AnadirCliente::AnadirCliente(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AnadirCliente)
{
    ui->setupUi(this);
}

AnadirCliente::~AnadirCliente()
{
    delete ui;
}

void AnadirCliente::on_bb_ok_cancel_clicked(QAbstractButton *button)
{
    if (button == ui->bb_ok_cancel->button(QDialogButtonBox::Cancel))
    {
        this->reject();
    }
    else if (button == ui->bb_ok_cancel->button(QDialogButtonBox::Ok))
    {
        if (check_client_is_not_in_db())
        {
            add_client_to_db();
            this->accept();
        }
        else
        {
            int ret = QMessageBox::question(this, tr("Sobreescribir datos"),
                                            tr("El nombre del cliente introducido se encuentra en la tabla de clientes.\n"
                                               "¿Desea sobreescribir los datos guardados con los datos introducidos?"),
                                            QMessageBox::Yes | QMessageBox::No,
                                            QMessageBox::No);
            if (ret == QMessageBox::Yes)
            {
                update_client_to_db();
                this->accept();
            }
            else
                this->reject();
        }
    }
}

bool AnadirCliente::check_client_is_not_in_db()
{
    QString text = select_from_where_like(db, "nombre", "clientes", "nombre", ui->le_nombre->text(), true);
    if (text.isEmpty())
        return 1;
    else
        return 0;
}

void AnadirCliente::add_client_to_db()
{
    db.open();
    QSqlQuery q;
    q.prepare("INSERT INTO clientes (nombre, tel_fijo, movil, direccion) \
    VALUES (:nombre, :tel_fijo, :movil, :direccion);");
    q.bindValue(":nombre", ui->le_nombre->text());
    q.bindValue(":tel_fijo", ui->le_tel_fijo->text());
    q.bindValue(":movil", ui->le_movil->text());
    q.bindValue(":direccion", ui->le_direccion->text());
    q.exec();
    q.clear();
    db.close();
}

void AnadirCliente::update_client_to_db()
{
    // Get exact name of the client in database
    QString text = select_from_where_like(db, "nombre", "clientes", "nombre", ui->le_nombre->text(), true);
    // Update database
    db.open();
    QSqlQuery q;
    q.prepare("UPDATE clientes SET tel_fijo = :tel_fijo, movil = :movil, direccion = :direccion WHERE nombre = :nombre");
    q.bindValue(":nombre", text);
    q.bindValue(":tel_fijo", ui->le_tel_fijo->text());
    q.bindValue(":movil", ui->le_movil->text());
    q.bindValue(":direccion", ui->le_direccion->text());
    q.exec();
    q.clear();
    db.close();
}
