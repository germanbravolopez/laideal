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

    reset_all_contents();
}

void Facturas::reset_all_contents()
{

}
