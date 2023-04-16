#include "contabilidad.h"
#include "ui_contabilidad.h"
#include "sql_lite.h"
#include "qprinter.h"

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
    double total_income_ing = get_total_income("ingresos", 0);
    double base_ing = total_income_ing / 1.21;
    double iva_ing = total_income_ing - base_ing;
    double total_income_g10 = get_total_income("gastos", 10);
    double base_g10 = total_income_g10 / 1.1;
    double iva_g10 = total_income_g10 - base_g10;
    double total_income_g21 = get_total_income("gastos", 21);
    double base_g21 = total_income_g21 / 1.21;
    double iva_g21 = total_income_g21 - base_g21;
    double total_income_gni = get_total_income("gastos", 0);

    QString contabilidad_html =
    "<div align=right>"
       "Granada, " + QDate::currentDate().toString("dd-MM-yyyy") +
    "</div>"
    "<div align=left>"
       "Tintorería La Ideal<br>"
       "Plaza San Pantaleón 1, bajo 2<br>"
       "18012 Granada"
    "</div>"
    "<h1 align=center>Contabilidad</h1>"
    "<h2>Trimestre: " + QString::number(ui->sb_trim->value()) +
        ", Año: " + QString::number(ui->sb_year->value()) + "</h2>"
    "<h3>Importes totales</h3>"
    "<p align=left>"
        "Importe: " + QString::number(total_income_ing, 'f', 2) + "<br>"
        "Base:    " + QString::number(base_ing, 'f', 2) + "<br>"
        "IVA:     " + QString::number(iva_ing, 'f', 2) +
    "</p>"
    "<h3>Gastos totales - 10</h3>"
    "<p align=left>"
        "Importe: " + QString::number(total_income_g10, 'f', 2) + "<br>"
        "Base:    " + QString::number(base_g10, 'f', 2) + "<br>"
        "IVA:     " + QString::number(iva_g10, 'f', 2) +
    "</p>"
    "<h3>Gastos totales - 21</h3>"
    "<p align=left>"
        "Importe: " + QString::number(total_income_g21, 'f', 2) + "<br>"
        "Base:    " + QString::number(base_g21, 'f', 2) + "<br>"
        "IVA:     " + QString::number(iva_g21, 'f', 2) +
    "</p>"
    "<h3>Gastos sin IVA</h3>"
    "<p align=left>"
        "Importe: " + QString::number(total_income_gni, 'f', 2) +
    "</p>"
    "<h3>Gastos conjuntos</h3>"
    "<p align=left>"
        "Importe: " + QString::number(total_income_g10 + total_income_g21, 'f', 2) + "<br>"
        "Base:    " + QString::number(base_g10 + base_g21, 'f', 2) + "<br>"
        "IVA:     " + QString::number(iva_g10 + iva_g21, 'f', 2) +
    "</p>";

    write_to_pdf("C:/Users/gebra/Downloads/test_pdf_1.pdf", contabilidad_html);
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

void Contabilidad::write_to_pdf(QString filename, QString html)
{
    QTextDocument document;
    document.setHtml(html);

    QPrinter printer(QPrinter::PrinterResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setPageSize(QPageSize::A4);
    printer.setOutputFileName(filename);
    printer.setPageMargins(QMarginsF(15, 15, 15, 15));

    document.print(&printer);

    QDesktopServices::openUrl(QUrl::fromLocalFile(filename));
    //QDesktopServices::openUrl(QUrl::fromLocalFile(qApp->applicationDirPath() + "/docs/" + "nameof.pdf"));
}
