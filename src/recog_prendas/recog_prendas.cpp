#include "recog_prendas.h"
#include "ui_recog_prendas.h"
#include "sql_lite.h"

RecogPrendas::RecogPrendas(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::RecogPrendas)
{
    ui->setupUi(this);
    initial_settings();
}

RecogPrendas::~RecogPrendas()
{
    delete ui;
}

void RecogPrendas::initial_settings()
{
    reset_all_contents();
    ui->pb_payment->setStyleSheet("background-color: red; font-size: 20px");
    ui->pb_state->setStyleSheet("background-color: red; font-size: 20px");
    ui->le_search->setFocus();
    ui->tableView->verticalHeader()->setVisible(false);
}

void RecogPrendas::reset_all_contents()
{
    is_cell_clicked = false;
    ui->le_nr_ticket->clear();
    ui->le_phone->clear();
    ui->le_client->clear();
    ui->le_garm->clear();
    ui->le_qty->clear();
    ui->le_servic->clear();
    ui->le_size->clear();
    ui->le_price->clear();
    ui->le_obsv->clear();
    ui->le_total_price->clear();
    ui->pb_payment->setChecked(false);
    ui->pb_state->setChecked(false);
    ui->de_date_recep->setDate(QDate::currentDate());
    ui->de_date_paym->setDate(QDate::currentDate());
    ui->de_date_pickup->setDate(QDate::currentDate());
}

void RecogPrendas::update_db(UpdateDBop op)
{
    bool edit_lock = sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_EDIT_LOCK)).toBool();
    bool ok = false;
    QSqlQuery q;
    switch (op) {
    case PAY_YES:
        // If edit_lock payment info cannot be changed
        if (!edit_lock)
        {
            db.open();
            q.prepare("UPDATE ingresos SET fecha_pago = :new_fecha_pago, pagado = :new_pagado WHERE \
                n_recibo = :n_re AND importe = :impo AND pagado = :paga AND estado = :esta AND cantidad = :cant AND prenda = :pren AND size = :size AND servicio = :serv AND observaciones = :obsv");
            // Set new values
            q.bindValue(":new_fecha_pago", ui->de_date_paym->date().toString("dd-MM-yyyy"));
            q.bindValue(":new_pagado",     ui->pb_payment->text());
            // Set old values
            q.bindValue(":n_re", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_TICKET)).toString());
            q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString());
            q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
            q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
            q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
            q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
            q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString());
            q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
            q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
            // Write to db
            ok = q.exec();
            q.clear();
            db.close();
        }
        else
        {
            qDebug() << "DB corrupted, item with edit_lock 1 and is_payed NO";
            qDebug() << sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_TICKET)).toString();
        }
        break;
    case PAY_NO:
        // If edit_lock payment info cannot be changed
        if (!edit_lock)
        {
            db.open();
            QSqlQuery q;
            q.prepare("UPDATE ingresos SET fecha_pago = :new_fecha_pago, pagado = :new_pagado WHERE \
                n_recibo = :n_re AND importe = :impo AND pagado = :paga AND estado = :esta AND cantidad = :cant AND prenda = :pren AND size = :size AND servicio = :serv AND observaciones = :obsv");
            // Set new values
            q.bindValue(":new_fecha_pago", "");
            q.bindValue(":new_pagado",     ui->pb_payment->text());
            // Set old values
            q.bindValue(":n_re", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_TICKET)).toString());
            q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString());
            q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
            q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
            q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
            q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
            q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString());
            q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
            q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
            // Write to db
            ok = q.exec();
            q.clear();
            db.close();
        }
        break;
    case PKU_YES:
        db.open();
        q.prepare("UPDATE ingresos SET fecha_recogida = :new_fecha_recogida, estado = :new_estado WHERE \
            n_recibo = :n_re AND importe = :impo AND pagado = :paga AND estado = :esta AND cantidad = :cant AND prenda = :pren AND size = :size AND servicio = :serv AND observaciones = :obsv");
        // Set new values
        q.bindValue(":new_fecha_recogida", ui->de_date_pickup->date().toString("dd-MM-yyyy"));
        q.bindValue(":new_estado",         ui->pb_state->text());
        // Set old values
        q.bindValue(":n_re", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_TICKET)).toString());
        q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString());
        q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
        q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
        q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
        q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
        q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString());
        q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
        q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
        // Write to db
        ok = q.exec();
        q.clear();
        db.close();
        break;
    case PKU_NO:
        db.open();
        q.prepare("UPDATE ingresos SET fecha_recogida = :new_fecha_recogida, estado = :new_estado WHERE \
            n_recibo = :n_re AND importe = :impo AND pagado = :paga AND estado = :esta AND cantidad = :cant AND prenda = :pren AND size = :size AND servicio = :serv AND observaciones = :obsv");
        // Set new values
        q.bindValue(":new_fecha_recogida", "");
        q.bindValue(":new_estado",         ui->pb_state->text());
        // Set old values
        q.bindValue(":n_re", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_TICKET)).toString());
        q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString());
        q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
        q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
        q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
        q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
        q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString());
        q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
        q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
        // Write to db
        ok = q.exec();
        q.clear();
        db.close();
        break;
    case OBSV:
        db.open();
        q.prepare("UPDATE ingresos SET observaciones = :new_observaciones WHERE \
            n_recibo = :n_re AND importe = :impo AND pagado = :paga AND estado = :esta AND cantidad = :cant AND prenda = :pren AND size = :size AND servicio = :serv AND observaciones = :obsv");
        // Set new values
        q.bindValue(":new_observaciones", ui->le_obsv->text());
        // Set old values
        q.bindValue(":n_re", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_TICKET)).toString());
        q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString());
        q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
        q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
        q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
        q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
        q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString());
        q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
        q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
        // Write to db
        ok = q.exec();
        q.clear();
        db.close();
        break;
    case PAY_DE_CH:
        // If edit_lock payment info cannot be changed
        if (!edit_lock && ui->pb_payment->text() == "SI")
        {
            db.open();
            q.prepare("UPDATE ingresos SET fecha_pago = :new_fecha_pago WHERE \
                n_recibo = :n_re AND importe = :impo AND pagado = :paga AND estado = :esta AND cantidad = :cant AND prenda = :pren AND size = :size AND servicio = :serv AND observaciones = :obsv");
            // Set new values
            q.bindValue(":new_fecha_pago", ui->de_date_paym->date().toString("dd-MM-yyyy"));
            // Set old values
            q.bindValue(":n_re", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_TICKET)).toString());
            q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString());
            q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
            q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
            q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
            q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
            q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString());
            q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
            q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
            // Write to db
            ok = q.exec();
            q.clear();
            db.close();
        }
        break;
    case PKU_DE_CH:
        if (ui->pb_state->text() == "Recogido")
        {
            db.open();
            q.prepare("UPDATE ingresos SET fecha_recogida = :new_fecha_recogida WHERE \
                n_recibo = :n_re AND importe = :impo AND pagado = :paga AND estado = :esta AND cantidad = :cant AND prenda = :pren AND size = :size AND servicio = :serv AND observaciones = :obsv");
            // Set new values
            q.bindValue(":new_fecha_recogida", ui->de_date_pickup->date().toString("dd-MM-yyyy"));
            // Set old values
            q.bindValue(":n_re", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_TICKET)).toString());
            q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString());
            q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
            q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
            q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
            q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
            q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString());
            q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
            q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
            // Write to db
            ok = q.exec();
            q.clear();
            db.close();
        }
        break;
    case SIZE_AND_PRICE:
        if (!edit_lock && ui->pb_payment->text() == "NO")
        {
            db.open();
            q.prepare("UPDATE ingresos SET size = :new_size, importe = :new_importe WHERE \
                n_recibo = :n_re AND importe = :impo AND pagado = :paga AND estado = :esta AND cantidad = :cant AND prenda = :pren AND size = :size AND servicio = :serv AND observaciones = :obsv");
            // Set new values
            q.bindValue(":new_size", ui->le_size->text());
            q.bindValue(":new_importe", ui->le_price->text());
            // Set old values
            q.bindValue(":n_re", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_TICKET)).toString());
            q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString());
            q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
            q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
            q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
            q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
            q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString());
            q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
            q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
            // Write to db
            ok = q.exec();
            q.clear();
            db.close();

        }
    default:
        break;
    }
    qDebug() << ok;
    // Search again
    on_pb_search_clicked();
    // Load again data from table
    update_row_clicked_to_fields();
    is_cell_clicked = true;
}

void RecogPrendas::update_row_clicked_to_fields()
{
    // Update content from clicked row
    ui->le_nr_ticket->setText(sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_TICKET)).toString());
    ui->le_client->setText(sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_CLIENT)).toString());
    ui->le_phone->setText(select_from_where_like(db, "movil", "clientes", "nombre", ui->le_client->text(), true));
    ui->le_garm->setText(sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
    ui->le_qty->setText(sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
    ui->le_servic->setText(sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
    ui->le_size->setText(sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString());
    ui->le_price->setText(sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString());
    ui->le_obsv->setText(sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
    ui->pb_payment->setChecked(sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString() == "SI");
    ui->pb_state->setChecked(sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString() == "Recogido");
    ui->de_date_recep->setDate(QDate::fromString(sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_DATE_RCP)).toString(),"dd-MM-yyyy"));
    ui->de_date_paym->setDate(QDate::fromString(sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_DATE_PAY)).toString(),"dd-MM-yyyy"));
    ui->de_date_pickup->setDate(QDate::fromString(sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_DATE_PKU)).toString(),"dd-MM-yyyy"));
}

void RecogPrendas::calculate_price()
{
    float item_price = read_garment_price(db, ui->le_garm->text(), ui->le_servic->text());
    float calculated_price = item_price * ui->le_qty->text().toFloat() * ui->le_size->text().toFloat();
    ui->le_price->setText(QString::number(calculated_price));
}

void RecogPrendas::on_le_search_returnPressed()
{
    on_pb_search_clicked();
}

void RecogPrendas::on_pb_search_clicked()
{
    reset_all_contents();
    if (ui->le_search->text() != "")
    {
        bool ok = false, total_price_active = false;
        ui->le_search->text().toUInt(&ok);
        if (ok)
        {
            if(ui->le_search->text().length() >= 9)
            {
                // Phone number
                QString client_from_tel_fijo = select_from_where_like(db, "nombre", "clientes", "tel_fijo", ui->le_search->text(), true);
                QString client_from_movil = select_from_where_like(db, "nombre", "clientes", "movil", ui->le_search->text(), true);
                if (!client_from_tel_fijo.isNull())
                {
                    db.open();
                    sql_query_model->setQuery("SELECT * \
                                     FROM ingresos \
                                     WHERE cliente = '" + client_from_tel_fijo + "'");
                    db.close();
                }
                else if (!client_from_movil.isNull())
                {
                    db.open();
                    sql_query_model->setQuery("SELECT * \
                                    FROM ingresos \
                                    WHERE cliente = '" + client_from_movil + "'");
                    db.close();
                }
            }
            else
            {
                // Ticket number
                db.open();
                sql_query_model->setQuery("SELECT * \
                                FROM ingresos \
                                WHERE n_recibo = '" + ui->le_search->text() + "'");
                db.close();
                total_price_active = true;
            }
        }
        // Search is not a number
        else if (ui->le_search->text().isSimpleText())
        {
            // Text (client, address or another)
            QDate date_slash = QDate::fromString(ui->le_search->text(), "dd/MM/yyyy");
            QDate date_dash = QDate::fromString(ui->le_search->text(), "dd-MM-yyyy");
            if (!date_slash.isNull())
            {
                // Text is date
                db.open();
                sql_query_model->setQuery("SELECT * \
                                FROM ingresos \
                                WHERE fecha_recepcion = '" + date_slash.toString("dd-MM-yyyy") + "' \
                                    OR fecha_pago = '" + date_slash.toString("dd-MM-yyyy") + "' \
                                    OR fecha_recogida = '" + date_slash.toString("dd-MM-yyyy") + "'");
                db.close();
            }
            else if (!date_dash.isNull())
            {
                // Text is date
                db.open();
                sql_query_model->setQuery("SELECT * \
                                FROM ingresos \
                                WHERE fecha_recepcion = '" + date_dash.toString("dd-MM-yyyy") + "' \
                                    OR fecha_pago = '" + date_dash.toString("dd-MM-yyyy") + "' \
                                    OR fecha_recogida = '" + date_dash.toString("dd-MM-yyyy") + "'");
                db.close();
            }
            else
            {
                // Text is not a date
                db.open();
                sql_query_model->setQuery("SELECT * \
                                FROM ingresos \
                                WHERE cliente like '%" + ui->le_search->text() + "%'");
                db.close();
            }
        }
        else
        {
            QMessageBox::warning(this, tr("Búsqueda incorrecta"),
                                  tr("El contenido de la búsqueda no se ha identificado.\n"
                                  "Hablar con Germán..."),
                                  QMessageBox::Ok, QMessageBox::Ok);
            qDebug() << "Text is not identified!";
        }
        // Complete model and set to the view
        sql_query_model->setHeaderData(TABLE_TICKET   , Qt::Horizontal, tr("n_recibo"));
        sql_query_model->setHeaderData(TABLE_CLIENT   , Qt::Horizontal, tr("cliente"));
        sql_query_model->setHeaderData(TABLE_DATE_RCP , Qt::Horizontal, tr("fecha_recepcion"));
        sql_query_model->setHeaderData(TABLE_DATE_PAY , Qt::Horizontal, tr("fecha_pago"));
        sql_query_model->setHeaderData(TABLE_DATE_PKU , Qt::Horizontal, tr("fecha_recogida"));
        sql_query_model->setHeaderData(TABLE_PRICE    , Qt::Horizontal, tr("importe"));
        sql_query_model->setHeaderData(TABLE_IS_PAYED , Qt::Horizontal, tr("pagado"));
        sql_query_model->setHeaderData(TABLE_STATE    , Qt::Horizontal, tr("estado"));
        sql_query_model->setHeaderData(TABLE_SIZE     , Qt::Horizontal, tr("cantidad"));
        sql_query_model->setHeaderData(TABLE_GARMENT  , Qt::Horizontal, tr("prenda"));
        sql_query_model->setHeaderData(TABLE_SIZE     , Qt::Horizontal, tr("size"));
        sql_query_model->setHeaderData(TABLE_SERVICE  , Qt::Horizontal, tr("servicio"));
        sql_query_model->setHeaderData(TABLE_OBSERV   , Qt::Horizontal, tr("observaciones"));
        sql_query_model->setHeaderData(TABLE_EDIT_LOCK, Qt::Horizontal, tr("edit_lock"));
        // Set model to table
        ui->tableView->setModel(sql_query_model);
        ui->tableView->resizeColumnsToContents();
        ui->tableView->sortByColumn(0, Qt::AscendingOrder);
        // Fill total_price if enabled
        if (total_price_active)
        {
            double total_price = 0.0;
            for (int row = 0; row < sql_query_model->rowCount(); row++)
            {
                total_price = total_price + sql_query_model->data(sql_query_model->index(row, TABLE_PRICE)).toFloat();
            }
            ui->le_total_price->setText(QString::number(total_price, 'f', 2));
        }
        else
        {
            ui->le_total_price->setText("");
        }
    }
    else
    {
        sql_query_model->clear();
        ui->tableView->setModel(sql_query_model);
    }
}

void RecogPrendas::on_pb_reset_clicked()
{
    reset_all_contents();
    ui->le_search->setText("");
    on_pb_search_clicked();
}

void RecogPrendas::on_pb_payment_toggled(bool checked)
{
    if (checked)
    {
        ui->pb_payment->setText("SI");
        ui->pb_payment->setStyleSheet("background-color: green; font-size: 20px");
        if (is_cell_clicked)
        {
            update_db(PAY_YES);
        }
    }
    else
    {
        ui->pb_payment->setText("NO");
        ui->pb_payment->setStyleSheet("background-color: red; font-size: 20px");
        if (is_cell_clicked)
        {
            update_db(PAY_NO);
        }
    }
}

void RecogPrendas::on_pb_state_toggled(bool checked)
{
    if (checked)
    {
        ui->pb_state->setText("Recogido");
        ui->pb_state->setStyleSheet("background-color: green; font-size: 20px");
        if (is_cell_clicked)
        {
            update_db(PKU_YES);
        }
    }
    else
    {
        ui->pb_state->setText("En tienda");
        ui->pb_state->setStyleSheet("background-color: red; font-size: 20px");
        if (is_cell_clicked)
        {
            update_db(PKU_NO);
        }
    }
}

void RecogPrendas::on_tableView_clicked(const QModelIndex &index)
{
    // Check if another row is clicked
    if (index.row() != row_clicked_cell)
    {
        is_cell_clicked = false;
    }
    // Update pointers to cell clicked
    row_clicked_cell    = index.row();
    column_clicked_cell = index.column();
    update_row_clicked_to_fields();
    // Set clicked cell
    is_cell_clicked     = true;
}

void RecogPrendas::on_le_obsv_returnPressed()
{
    if (is_cell_clicked)
    {
        update_db(OBSV);
    }
}

void RecogPrendas::on_de_date_paym_userDateChanged(const QDate &date)
{
    if (is_cell_clicked)
    {
        update_db(PAY_DE_CH);
    }
}

void RecogPrendas::on_de_date_pickup_userDateChanged(const QDate &date)
{
    if (is_cell_clicked)
    {
        update_db(PKU_DE_CH);
    }
}

void RecogPrendas::on_le_size_editingFinished()
{
    QString left_side = ui->le_garm->text().left(8);
    if (is_cell_clicked && left_side == "Alfombra")
    {
        calculate_price();
        update_db(SIZE_AND_PRICE);
    }
}

void RecogPrendas::on_pb_pay_all_clicked()
{
    int current_row = row_clicked_cell;
    for (int row = 0; row < sql_query_model->rowCount(); row++)
    {
        on_tableView_clicked(sql_query_model->index(row, 0));
        on_pb_payment_toggled(true);
    }
    if (current_row >= 0)
    {
        on_tableView_clicked(sql_query_model->index(current_row, 0));
    }
    else
    {
        reset_all_contents();
    }
}

void RecogPrendas::on_pb_pku_all_clicked()
{
    int current_row = row_clicked_cell;
    for (int row = 0; row < sql_query_model->rowCount(); row++)
    {
        on_tableView_clicked(sql_query_model->index(row, 0));
        on_pb_state_toggled(true);
    }
    if (current_row >= 0)
    {
        on_tableView_clicked(sql_query_model->index(current_row, 0));
    }
    else
    {
        reset_all_contents();
    }
}

void RecogPrendas::on_pb_pay_pku_all_clicked()
{
    on_pb_pay_all_clicked();
    on_pb_pku_all_clicked();
}
