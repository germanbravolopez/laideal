#include "add_garment.h"
#include "ui_add_garment.h"
#include "sql_lite.h"
#include "imprimir.h"

AddGarment::AddGarment(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::AddGarment)
{
    ui->setupUi(this);
    initial_settings();
}

AddGarment::~AddGarment()
{
    delete ui;
}

void AddGarment::initial_settings()
{
    reset_all_contents();
    ui->pb_pagado->setStyleSheet("background-color: red; font-size: 18px");
    ui->pb_estado->setStyleSheet("background-color: red; font-size: 18px");
    ui->le_n_recibo->setFocus();
}

void AddGarment::reset_all_contents()
{
    ticket_found = false;
    ui->le_n_recibo->clear();
    ui->le_cliente->clear();
    ui->de_fecha_rcp->setDate(QDate::currentDate());
    ui->cb_prenda->setCurrentText("");
    ui->le_cantidad->clear();
    ui->pb_pagado->setChecked(false);
    ui->de_fecha_pago->setDate(QDate::currentDate());
    ui->le_size->clear();
    ui->pb_estado->setChecked(false);
    ui->de_fecha_recog->setDate(QDate::currentDate());
    ui->le_importe->setText("0.00");
    ui->cb_servicio->setCurrentText("Limp.");
    ui->le_observaciones->clear();
}

void AddGarment::on_pb_search_pressed()
{
    db.open();
    sql_query_model->setQuery("SELECT * \
                    FROM ingresos \
                    WHERE n_recibo = '" + ui->le_n_recibo->text() + "'");
    db.close();
    // check if not empty
    if (sql_query_model->rowCount() > 0) {
        ticket_found = true;
        fill_content_from_db();
        populate_garments();
    } else {
        ticket_found = false;
        QMessageBox::warning(this, "Añadir prenda",
                             "No se han encontrado ningún Nº de recibo para el número introducido.",
                             QMessageBox::Ok, QMessageBox::Ok);
    }
}

void AddGarment::fill_content_from_db()
{
    ui->le_cliente->setText(sql_query_model->data(sql_query_model->index(0, TABLE_CLIENT)).toString());
    ui->de_fecha_rcp->setDate(QDate::fromString(sql_query_model->data(sql_query_model->index(0, TABLE_DATE_RCP)).toString(), "dd-MM-yyyy"));
}

void AddGarment::populate_garments()
{
    QStringList garment_list = read_column_from_table(db, "nombre", "prendas", "");
    ui->cb_prenda->addItems(garment_list);
    ui->cb_prenda->setCurrentText("");
}

void AddGarment::on_pb_pagado_toggled(bool checked)
{
    if (checked) {
        ui->pb_pagado->setText("SI");
        ui->pb_pagado->setStyleSheet("background-color: green; font-size: 18px");
    }
    else {
        ui->pb_pagado->setText("NO");
        ui->pb_pagado->setStyleSheet("background-color: red; font-size: 18px");
    }
}

void AddGarment::on_pb_estado_toggled(bool checked)
{
    if (checked) {
        ui->pb_estado->setText("Recogido");
        ui->pb_estado->setStyleSheet("background-color: green; font-size: 18px");
    }
    else {
        ui->pb_estado->setText("En tienda");
        ui->pb_estado->setStyleSheet("background-color: red; font-size: 18px");
    }
}

void AddGarment::set_garment_price()
{
    if (ui->le_cantidad->text() != "") {
        float price = ui->le_cantidad->text().toFloat() * read_garment_price(db, ui->cb_prenda->currentText(), ui->cb_servicio->currentText());
        if (ui->le_size->text() != "") {
            price = ui->le_size->text().toFloat() * price;
        }
        ui->le_importe->setText(QString::number(price, 'f', 2));
    } else {
        ui->le_importe->setText("0.00");
    }
}

void AddGarment::on_le_cantidad_textChanged(const QString &arg1)
{
    set_garment_price();
}

void AddGarment::on_le_size_textChanged(const QString &arg1)
{
    set_garment_price();
}

void AddGarment::on_cb_servicio_currentTextChanged(const QString &arg1)
{
    set_garment_price();
}

void AddGarment::on_cb_prenda_currentTextChanged(const QString &arg1)
{
    if (ui->cb_prenda->findText(arg1, Qt::MatchExactly) != -1)
        set_garment_price();
}

void AddGarment::on_buttonBox_clicked(QAbstractButton *button)
{
    if (button == ui->buttonBox->button(QDialogButtonBox::Cancel))
        this->close();
    else if (button == ui->buttonBox->button(QDialogButtonBox::Reset))
        reset_all_contents();
    else if (button == ui->buttonBox->button(QDialogButtonBox::Save)) {
        if (validate_form()) {
            save_factura();
            reset_all_contents();
        }
    }
    else
        QMessageBox::critical(this, "Añadir prenda",
                              "Botón no definido.",
                              QMessageBox::Ok, QMessageBox::Ok);
}

bool AddGarment::validate_form()
{
    bool ok = 1;
    if (ticket_found) {
        // Avoid n_recibo, client, garment, quantity to be empty
        if (ui->le_n_recibo->text() != "" &&
                ui->le_cliente->text() != "" &&
                ui->cb_prenda->currentText() != "" &&
                ui->le_cantidad->text() != "") {
            // Check if current garment require a size
            if (ui->cb_prenda->currentText().contains("m2") && ui->le_size->text() == "") {
                ok = 0;
                QMessageBox::warning(nullptr, "Añadir prenda",
                                     "La prenda introducida requiere un tamaño.",
                                     QMessageBox::Ok, QMessageBox::Ok);
            }
            // Check current month is not blocked for payment
            if (ui->pb_pagado->isChecked()) {
                if (read_lock_for_month_and_year(db, "ingresos", ui->de_fecha_pago->date().month(), ui->de_fecha_pago->date().year()) == 1) {
                    ok = 0;
                    QMessageBox::warning(this, tr("Añadir prenda"),
                                         tr("La fecha de pago pertenece a un trimestre que se encuentra bloqueado por la contabilidad."),
                                         QMessageBox::Ok, QMessageBox::Ok);
                }
            }
        } else {
            ok = 0;
            QMessageBox::warning(this, "Añadir prenda",
                                 "Formulario incompleto.\n"
                                 "Para poder guardar la factura, al menos es necesario rellenar los siguientes campos: Nº Recibo, Cliente, Prenda y Cantidad.",
                                 QMessageBox::Ok, QMessageBox::Ok);
        }
    } else {
        ok = 0;
        QMessageBox::critical(nullptr, "Añadir prenda",
                              "No se ha buscado ningún Nº recibo previo a guardar los datos actuales.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }
    return ok;
}

void AddGarment::save_factura()
{
    db.open();
    QSqlQuery q;
    q.prepare("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, observaciones, edit_lock) \
    VALUES (:n_recibo, :cliente, :fecha_recepcion, :fecha_pago, :fecha_recogida, :importe, :pagado, :estado, :cantidad, :prenda, :size, :servicio, :observaciones, :edit_lock);");
    q.bindValue(":n_recibo", ui->le_n_recibo->text());
    q.bindValue(":cliente", ui->le_cliente->text());
    q.bindValue(":fecha_recepcion", ui->de_fecha_rcp->date().toString("dd-MM-yyyy"));
    q.bindValue(":pagado", ui->pb_pagado->text());
    if (ui->pb_pagado->isChecked())
        q.bindValue(":fecha_pago", ui->de_fecha_pago->date().toString("dd-MM-yyyy"));
    else
        q.bindValue(":fecha_pago", "");
    q.bindValue(":estado", ui->pb_estado->text());
    if (ui->pb_estado->isChecked())
        q.bindValue(":fecha_recogida", ui->de_fecha_recog->date().toString("dd-MM-yyyy"));
    else
        q.bindValue(":fecha_recogida", "");
    q.bindValue(":importe", ui->le_importe->text().replace(",","."));
    q.bindValue(":cantidad", ui->le_cantidad->text());
    q.bindValue(":prenda", ui->cb_prenda->currentText());
    q.bindValue(":size", ui->le_size->text().replace(",","."));
    q.bindValue(":observaciones", ui->le_observaciones->text());
    q.bindValue(":servicio", ui->cb_servicio->currentText());
    q.bindValue(":edit_lock", "0");
    q.exec();
    q.clear();
    db.close();
}
