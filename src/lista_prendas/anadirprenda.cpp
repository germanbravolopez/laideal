#include "anadirprenda.h"
#include "ui_anadirprenda.h"
#include "sql_lite.h"

AnadirPrenda::AnadirPrenda(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AnadirPrenda)
{
    ui->setupUi(this);
    anadirprenda_initial_settings();
}

AnadirPrenda::~AnadirPrenda()
{
    delete ui;
}

void AnadirPrenda::anadirprenda_initial_settings()
{
    // load items in cb
    ui->cb_nombre->addItems(read_column_from_table(db, "nombre", "prendas"));
    ui->cb_nombre->setCurrentText("");
    // clear all line editors
    ui->le_precio_limp->clear();
    ui->le_precio_plan->clear();
    // set focus on name
    ui->cb_nombre->setFocus();
}

void AnadirPrenda::on_bb_ok_cancel_clicked(QAbstractButton *button)
{
    if (button == ui->bb_ok_cancel->button(QDialogButtonBox::Cancel))
    {
        this->reject();
    }
    else if (button == ui->bb_ok_cancel->button(QDialogButtonBox::Ok))
    {
        if (check_garment_is_not_in_db())
        {
            add_garment_to_db();
            this->accept();
        }
        else
        {
            int ret = QMessageBox::question(this, tr("Sobreescribir datos"),
                                            tr("El nombre de la prenda introducido se encuentra en la tabla de prendas.\n"
                                               "¿Desea sobreescribir los precios con los datos introducidos?"),
                                            QMessageBox::Yes | QMessageBox::No,
                                            QMessageBox::No);
            if (ret == QMessageBox::Yes)
            {
                update_garment_to_db();
                this->accept();
            }
            else
                this->reject();
        }
    }
}

bool AnadirPrenda::check_garment_is_not_in_db()
{
    QString text = select_from_where_like(db, "nombre", "prendas", "nombre", ui->cb_nombre->currentText(), true);
    if (text.isEmpty())
        return 1;
    else
        return 0;
}

void AnadirPrenda::add_garment_to_db()
{
    db.open();
    QSqlQuery q;
    q.prepare("INSERT INTO prendas (nombre, precio_limpieza, precio_plancha) \
    VALUES (:nombre, :precio_limpieza, :precio_plancha);");
    q.bindValue(":nombre", ui->cb_nombre->currentText());
    q.bindValue(":precio_limpieza", ui->le_precio_limp->text());
    q.bindValue(":precio_plancha", ui->le_precio_plan->text());
    q.exec();
    q.clear();
    db.close();
}

void AnadirPrenda::update_garment_to_db()
{
    // Get exact name of the garment in database
    QString text = select_from_where_like(db, "nombre", "prendas", "nombre", ui->cb_nombre->currentText(), true);
    // Update database
    db.open();
    QSqlQuery q;
    q.prepare("UPDATE prendas SET precio_limpieza = :precio_limpieza, precio_plancha = :precio_plancha WHERE nombre = :nombre");
    q.bindValue(":nombre", text);
    q.bindValue(":precio_limpieza", ui->le_precio_limp->text());
    q.bindValue(":precio_plancha", ui->le_precio_plan->text());
    q.exec();
    q.clear();
    db.close();
}

void AnadirPrenda::on_cb_nombre_editTextChanged(const QString &arg1)
{
    if (arg1 != "")
    {
        ui->le_precio_limp->setText(select_from_where_like(db, "precio_limpieza", "prendas", "nombre", arg1, false));
        ui->le_precio_plan->setText(select_from_where_like(db, "precio_plancha", "prendas", "nombre", arg1, false));
    }
}
