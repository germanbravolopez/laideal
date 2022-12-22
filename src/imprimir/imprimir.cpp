#include "imprimir.h"
#include "ui_imprimir.h"
#include "sql_lite.h"

Imprimir::Imprimir(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Imprimir)
{
    ui->setupUi(this);
}

Imprimir::~Imprimir()
{
    delete ui;
}

void Imprimir::create_ticket_and_print()
{

}

void Imprimir::on_bb_ok_cancel_accepted()
{
    if (ui->le_n_ticket->text() ==
        select_from_where_like(db, "n_recibo", "ingresos", "n_recibo", ui->le_n_ticket->text(), true))
    {
        create_ticket_and_print();
    }
    else
    {
        QMessageBox::information(this, "Imprimir",
                              "El número de recibo no se ha encontrado en la base de datos.\n"
                              "Utilizar otro número o buscarlo en la lista de ingresos.",
                              QMessageBox::Ok,
                              QMessageBox::Ok);
    }
}

void Imprimir::on_bb_ok_cancel_rejected()
{
    this->close();
}
