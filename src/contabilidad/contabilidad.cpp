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
    switch (read_lock_for_trim_and_year(db, ui->sb_trim->value(), ui->sb_year->value())) {
    case 0:
        // contabilidad not done
        generate_contabilidad();
        lock_data();
        break;
    case 1:
        // contabilidad done
        ask_for_repeat();
        qDebug() << "contabilidad done, repeat?";
        break;
    default:
        QMessageBox::warning(this, "Contabilidad",
                              "No hay registros de ingresos para realizar la contabilidad en el periodo indicado.",
                              QMessageBox::Ok, QMessageBox::Ok);
        qDebug() << "no data is found in ingresos";
        break;
    }
}

void Contabilidad::on_bb_ok_cancel_rejected()
{
    this->close();
}

void Contabilidad::generate_contabilidad()
{
    double total_income = get_total_income("ingresos", 0);
    double base = total_income / 1.21;
    double iva = total_income - base;
    qDebug().noquote() << "## AÑO "
                + QString::number(ui->sb_year->value())
                + ": TRIMESTRE "
                + QString::number(ui->sb_trim->value())
                + " ##";
    qDebug().noquote() << "  INGRESOS TOTALES:";
    qDebug().noquote() << "    Importe: "
                + QString::number(total_income, 'f', 2);
    qDebug().noquote() << "    Base:    "
                + QString::number(base, 'f', 2);
    qDebug().noquote() << "    IVA:     "
                + QString::number(iva, 'f', 2);

    total_income = get_total_income("gastos", 10);
    base = total_income / 1.1;
    iva = total_income - base;
    qDebug().noquote() << "  GASTOS TOTALES - 10:";
    qDebug().noquote() << "    Importe: "
                + QString::number(total_income, 'f', 2);
    qDebug().noquote() << "    Base:    "
                + QString::number(base, 'f', 2);
    qDebug().noquote() << "    IVA:     "
                + QString::number(iva, 'f', 2);

    double gastos_totales = total_income;
    double base_total = base;
    double iva_total = iva;

    total_income = get_total_income("gastos", 21);
    base = total_income / 1.21;
    iva = total_income - base;
    qDebug().noquote() << "  GASTOS TOTALES - 21:";
    qDebug().noquote() << "    Importe: "
                + QString::number(total_income, 'f', 2);
    qDebug().noquote() << "    Base:    "
                + QString::number(base, 'f', 2);
    qDebug().noquote() << "    IVA:     "
                + QString::number(iva, 'f', 2);

    gastos_totales += total_income;
    base_total += base;
    iva_total += iva;
    qDebug().noquote() << "  GASTOS CONJUNTOS:";
    qDebug().noquote() << "    Importe: "
                + QString::number(gastos_totales, 'f', 2);
    qDebug().noquote() << "    Base:    "
                + QString::number(base_total, 'f', 2);
    qDebug().noquote() << "    IVA:     "
                + QString::number(iva_total, 'f', 2);
}

double Contabilidad::get_total_income(QString table, int iva)
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
    return total_price_between_dates(db, table, start_date, end_date, iva);
}

void Contabilidad::lock_data()
{
    switch (ui->sb_trim->value()) {
    case 1:
        update_lock_in_ingresos(db, 1, 1, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 2, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 3, ui->sb_year->value());
        break;
    case 2:
        update_lock_in_ingresos(db, 1, 4, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 5, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 6, ui->sb_year->value());
        break;
    case 3:
        update_lock_in_ingresos(db, 1, 7, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 8, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 9, ui->sb_year->value());
        break;
    case 4:
        update_lock_in_ingresos(db, 1, 10, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 11, ui->sb_year->value());
        update_lock_in_ingresos(db, 1, 12, ui->sb_year->value());
        break;
    default:
        break;
    }
}

void Contabilidad::ask_for_repeat()
{
    int ret = QMessageBox::question(this, "Contabilidad",
                                    "La contabilidad del trimestre "
                                    + QString::number(ui->sb_trim->value())
                                    + " para el año "
                                    + QString::number(ui->sb_year->value())
                                    + " ya se ha realizado.\n¿Quieres volver a hacerla?",
                                    QMessageBox::Yes | QMessageBox::No,
                                    QMessageBox::No);
    if (ret == QMessageBox::Yes)
    {
        generate_contabilidad();
        lock_data();
    }
}
