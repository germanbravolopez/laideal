#include "contabilidad.h"
#include "ui_contabilidad.h"
#include "sql_lite.h"

Contabilidad::Contabilidad(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::Contabilidad)
{
    ui->setupUi(this);
    initial_settings();
}

Contabilidad::~Contabilidad()
{
    delete ui;
}

void Contabilidad::initial_settings()
{
    ui->sb_year->setRange(2019, QDate::currentDate().year());
    ui->sb_trim->setRange(1, 4);
    reset_all_contents();
}

void Contabilidad::reset_all_contents()
{
    ui->sb_trim->minimum();
    ui->sb_year->setValue(QDate::currentDate().year());
}

void Contabilidad::on_bb_ok_cancel_accepted()
{
    generate_contabilidad();
}

void Contabilidad::on_bb_ok_cancel_rejected()
{
    this->close();
}

void Contabilidad::generate_contabilidad()
{
    if (check_contabilidad_not_done())
    {
        double total_income = get_total_income();
        double base = total_income / 1.21;
        double iva = total_income - base;

        qDebug().noquote() << "## AÑO "
                    + QString::number(ui->sb_year->value())
                    + ": TRIMESTRE "
                    + QString::number(ui->sb_trim->value())
                    + " ##";
        qDebug().noquote() << "    Importe total: "
                    + QString::number(total_income, 'f', 2);
        qDebug().noquote() << "    Base:          "
                    + QString::number(base, 'f', 2);
        qDebug().noquote() << "    IVA:           "
                    + QString::number(iva, 'f', 2);
    }
}

bool Contabilidad::check_contabilidad_not_done()
{
    return true;
}

double Contabilidad::get_total_income()
{
    QDate start_date, end_date;
    switch (ui->sb_trim->value()) {
        case 1:
            start_date.setDate(ui->sb_year->value(), 1, 1);
            end_date.setDate(ui->sb_year->value(), 3, 31);
            break;
        case 2:
            start_date.setDate(ui->sb_year->value(), 4, 1);
            end_date.setDate(ui->sb_year->value(), 6, 30);
            break;
        case 3:
            start_date.setDate(ui->sb_year->value(), 7, 1);
            end_date.setDate(ui->sb_year->value(), 9, 30);
            break;
        case 4:
            start_date.setDate(ui->sb_year->value(), 10, 1);
            end_date.setDate(ui->sb_year->value(), 12, 31);
            break;
        default:
            break;
    }
    return total_payed_income_between_dates(db, start_date, end_date);
}
