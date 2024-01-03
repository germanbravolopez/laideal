#include "recog_prendas.h"
#include "ui_recog_prendas.h"
#include "sql_lite.h"
#include "imprimir.h"
#include "textcolordelegate.h"
#include "numberformatdelegate.h"

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
    ui->pb_payment->setStyleSheet("background-color: red; font-size: 18px");
    ui->pb_state->setStyleSheet("background-color: red; font-size: 18px");
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
    QSqlQuery q;
    switch (op) {
    case PAY_YES:
        // If edit_lock payment info cannot be changed
        if (!edit_lock) {
            // dont update payment date for blocked quarters
            if (read_lock_for_month_and_year(db, "ingresos", ui->de_date_paym->date().month(), ui->de_date_paym->date().year()) == 1)
                QMessageBox::warning(this, tr("Trimestre bloqueado"),
                                     tr("La fecha de pago pertenece a un trimestre que se encuentra bloqueado por la contabilidad."),
                                     QMessageBox::Ok, QMessageBox::Ok);
            else {
                db.open();
                q.prepare("UPDATE ingresos SET fecha_pago = :new_fecha_pago, pagado = :new_pagado WHERE \
                    n_recibo = :n_re AND importe = :impo AND pagado = :paga AND estado = :esta AND cantidad = :cant AND prenda = :pren AND size = :size AND servicio = :serv AND observaciones = :obsv");
                // Set new values
                q.bindValue(":new_fecha_pago", ui->de_date_paym->date().toString("dd-MM-yyyy"));
                q.bindValue(":new_pagado",     ui->pb_payment->text());
                // Set old values
                q.bindValue(":n_re", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_TICKET)).toString());
                q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString().replace(",","."));
                q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
                q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
                q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
                q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
                q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString().replace(",","."));
                q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
                q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
                // Write to db
                q.exec();
                q.clear();
                db.close();
            }
        }
        else
            QMessageBox::warning(this, tr("Ticket bloqueado"),
                                 tr("El ticket actual se encuentra bloqueado por la contabilidad."),
                                 QMessageBox::Ok, QMessageBox::Ok);
        break;
    case PAY_NO:
        // If edit_lock payment info cannot be changed
        if (!edit_lock) {
            db.open();
            QSqlQuery q;
            q.prepare("UPDATE ingresos SET fecha_pago = :new_fecha_pago, pagado = :new_pagado WHERE \
                n_recibo = :n_re AND importe = :impo AND pagado = :paga AND estado = :esta AND cantidad = :cant AND prenda = :pren AND size = :size AND servicio = :serv AND observaciones = :obsv");
            // Set new values
            q.bindValue(":new_fecha_pago", "");
            q.bindValue(":new_pagado",     ui->pb_payment->text());
            // Set old values
            q.bindValue(":n_re", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_TICKET)).toString());
            q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString().replace(",","."));
            q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
            q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
            q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
            q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
            q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString().replace(",","."));
            q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
            q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
            // Write to db
            q.exec();
            q.clear();
            db.close();
        }
        else
            QMessageBox::warning(this, tr("Ticket bloqueado"),
                                 tr("El ticket actual se encuentra bloqueado por la contabilidad."),
                                 QMessageBox::Ok, QMessageBox::Ok);
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
        q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString().replace(",","."));
        q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
        q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
        q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
        q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
        q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString().replace(",","."));
        q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
        q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
        // Write to db
        q.exec();
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
        q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString().replace(",","."));
        q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
        q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
        q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
        q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
        q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString().replace(",","."));
        q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
        q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
        // Write to db
        q.exec();
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
        q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString().replace(",","."));
        q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
        q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
        q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
        q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
        q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString().replace(",","."));
        q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
        q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
        // Write to db
        q.exec();
        q.clear();
        db.close();
        break;
    case SIZE_AND_PRICE:
        if (!edit_lock && ui->pb_payment->text() == "NO") {
            db.open();
            q.prepare("UPDATE ingresos SET size = :new_size, importe = :new_importe WHERE \
                n_recibo = :n_re AND importe = :impo AND pagado = :paga AND estado = :esta AND cantidad = :cant AND prenda = :pren AND size = :size AND servicio = :serv AND observaciones = :obsv");
            // Set new values
            q.bindValue(":new_size", ui->le_size->text());
            q.bindValue(":new_importe", ui->le_price->text());
            // Set old values
            q.bindValue(":n_re", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_TICKET)).toString());
            q.bindValue(":impo", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_PRICE)).toString().replace(",","."));
            q.bindValue(":paga", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_IS_PAYED)).toString());
            q.bindValue(":esta", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_STATE)).toString());
            q.bindValue(":cant", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_QUANTITY)).toString());
            q.bindValue(":pren", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_GARMENT)).toString());
            q.bindValue(":size", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SIZE)).toString().replace(",","."));
            q.bindValue(":serv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_SERVICE)).toString());
            q.bindValue(":obsv", sql_query_model->data(sql_query_model->index(row_clicked_cell, TABLE_OBSERV)).toString());
            // Write to db
            q.exec();
            q.clear();
            db.close();

        }
    default:
        break;
    }
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
    ui->le_phone->setText(select_from_where_like(db, "movil", "clientes", "nombre", ui->le_client->text(), true, false));
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
    if (ui->le_size->text().contains(",")) {
        QStringList size_splitted = ui->le_size->text().split(",");
        ui->le_size->setText(size_splitted.first() + "." + size_splitted.last());
    }
    float calculated_price = item_price * ui->le_qty->text().toFloat() * ui->le_size->text().toFloat();
    ui->le_price->setText(QString::number(calculated_price));
}

void RecogPrendas::on_le_search_returnPressed()
{
    on_pb_search_clicked();
}

void RecogPrendas::on_cb_search_date_currentTextChanged(const QString &arg1)
{
    on_pb_search_clicked();
}

void RecogPrendas::on_pb_search_clicked()
{
    reset_all_contents();
    if (ui->le_search->text() != "") {
        bool ok = false, total_price_active = true;
        ui->le_search->text().toUInt(&ok);
        if (ok) {
            if(ui->le_search->text().length() >= 9) {
                // Phone number
                QString client_from_phone = select_from_where_like(db, "nombre", "clientes", "tel_fijo",
                                                                      ui->le_search->text() + "' OR movil like '" + ui->le_search->text(), true, true);
                if (!client_from_phone.isNull()) {
                    db.open();
                    sql_query_model->setQuery("SELECT * \
                                     FROM ingresos \
                                     WHERE cliente = '" + client_from_phone + "'");
                    db.close();
                }
            }
            else {
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
        else if (ui->le_search->text().isSimpleText()) {
            // Text (client, address or another)
            QDate date_slash = QDate::fromString(ui->le_search->text(), "dd/MM/yyyy");
            QDate date_dash = QDate::fromString(ui->le_search->text(), "dd-MM-yyyy");
            QDate date = (!date_slash.isNull()) ? date_slash :
                         (!date_dash.isNull()) ? date_dash: QDate::currentDate();
            QString date_type = (ui->cb_search_date->currentText() == "Recepción") ? "fecha_recepcion" :
                                (ui->cb_search_date->currentText() == "Pago") ? "fecha_pago" :
                                (ui->cb_search_date->currentText() == "Recogida") ? "fecha_recogida" : "";
            if (!date_slash.isNull() || !date_dash.isNull()) {
                // Text is date
                db.open();
                sql_query_model->setQuery("SELECT * \
                                FROM ingresos \
                                WHERE " + date_type + " = '" + date.toString("dd-MM-yyyy") + "'");
                db.close();
            }
            else {
                // Text is not a date
                db.open();
                sql_query_model->setQuery("SELECT * \
                                FROM ingresos \
                                WHERE cliente like '%" + ui->le_search->text() + "%'");
                db.close();
            }
        }
        else
            QMessageBox::warning(this, tr("Búsqueda incorrecta"),
                                  tr("El contenido de la búsqueda no se ha identificado.\n"
                                  "Hablar con Germán..."),
                                  QMessageBox::Ok, QMessageBox::Ok);
        // Complete model and set to the view
        sql_query_model->setHeaderData(TABLE_TICKET   , Qt::Horizontal, tr("Nº"));
        sql_query_model->setHeaderData(TABLE_CLIENT   , Qt::Horizontal, tr("Cliente"));
        sql_query_model->setHeaderData(TABLE_DATE_RCP , Qt::Horizontal, tr("Recepción"));
        sql_query_model->setHeaderData(TABLE_DATE_PAY , Qt::Horizontal, tr("Pago"));
        sql_query_model->setHeaderData(TABLE_DATE_PKU , Qt::Horizontal, tr("Recogida"));
        sql_query_model->setHeaderData(TABLE_PRICE    , Qt::Horizontal, tr("Importe"));
        sql_query_model->setHeaderData(TABLE_IS_PAYED , Qt::Horizontal, tr("Pagado"));
        sql_query_model->setHeaderData(TABLE_STATE    , Qt::Horizontal, tr("Estado"));
        sql_query_model->setHeaderData(TABLE_SIZE     , Qt::Horizontal, tr("Cant."));
        sql_query_model->setHeaderData(TABLE_GARMENT  , Qt::Horizontal, tr("Prenda"));
        sql_query_model->setHeaderData(TABLE_SIZE     , Qt::Horizontal, tr("Tam."));
        sql_query_model->setHeaderData(TABLE_SERVICE  , Qt::Horizontal, tr("Serv."));
        sql_query_model->setHeaderData(TABLE_OBSERV   , Qt::Horizontal, tr("Obs."));
        sql_query_model->setHeaderData(TABLE_EDIT_LOCK, Qt::Horizontal, tr("Bloqueo"));
        // Set model to table
        proxyModel = new MySortFilterProxyModel(this);
        proxyModel->setSourceModel(sql_query_model);
        ui->tableView->setModel(proxyModel);
        ui->tableView->sortByColumn(0, Qt::AscendingOrder);
        //ui->tableView->setColumnHidden(TABLE_CLIENT, true);
        ui->tableView->setItemDelegateForColumn(TABLE_PRICE, new NumberFormatDelegate(this));
        ui->tableView->setItemDelegateForColumn(TABLE_IS_PAYED, new TextColorDelegate(ui->tableView, this));
        ui->tableView->setItemDelegateForColumn(TABLE_STATE, new TextColorDelegate(ui->tableView, this));
        ui->tableView->resizeColumnsToContents();
        ui->tableView->resizeRowsToContents();
        // Fill total_price if enabled
        if (total_price_active) {
            float total_price = 0.0;
            for (int row = 0; row < sql_query_model->rowCount(); row++)
                total_price = total_price + sql_query_model->data(sql_query_model->index(row, TABLE_PRICE)).toFloat();
            ui->le_total_price->setText(QString::number(total_price, 'f', 2));
        }
        else
            ui->le_total_price->setText("");
    }
    else {
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
    if (checked) {
        ui->pb_payment->setText("SI");
        ui->pb_payment->setStyleSheet("background-color: green; font-size: 18px");
        if (is_cell_clicked)
            update_db(PAY_YES);
    }
    else {
        ui->pb_payment->setText("NO");
        ui->pb_payment->setStyleSheet("background-color: red; font-size: 18px");
        if (is_cell_clicked)
            update_db(PAY_NO);
    }
}

void RecogPrendas::on_pb_state_toggled(bool checked)
{
    if (checked) {
        ui->pb_state->setText("Recogido");
        ui->pb_state->setStyleSheet("background-color: green; font-size: 18px");
        if (is_cell_clicked)
            update_db(PKU_YES);
    }
    else {
        ui->pb_state->setText("En tienda");
        ui->pb_state->setStyleSheet("background-color: red; font-size: 18px");
        if (is_cell_clicked)
            update_db(PKU_NO);
    }
}

void RecogPrendas::on_tableView_clicked(const QModelIndex &index)
{
    // Check if another row is clicked
    if (index.row() != row_clicked_cell)
        is_cell_clicked = false;
    // Update pointers to cell clicked
    row_clicked_cell    = index.row();
    column_clicked_cell = index.column();
    update_row_clicked_to_fields();
    // Set clicked cell
    is_cell_clicked     = true;
}

void RecogPrendas::on_le_obsv_editingFinished()
{
    if (is_cell_clicked)
        update_db(OBSV);
}

void RecogPrendas::on_le_size_editingFinished()
{
    if (is_cell_clicked && ui->le_garm->text().contains("m2")) {
        calculate_price();
        update_db(SIZE_AND_PRICE);
    }
}

void RecogPrendas::on_pb_pay_all_clicked()
{
    int current_row = row_clicked_cell;
    for (int row = 0; row < sql_query_model->rowCount(); row++) {
        on_tableView_clicked(sql_query_model->index(row, 0));
        on_pb_payment_toggled(true);
    }
    if (current_row >= 0)
        on_tableView_clicked(sql_query_model->index(current_row, 0));
    else
        reset_all_contents();
}

void RecogPrendas::on_pb_pku_all_clicked()
{
    int current_row = row_clicked_cell;
    for (int row = 0; row < sql_query_model->rowCount(); row++) {
        on_tableView_clicked(sql_query_model->index(row, 0));
        on_pb_state_toggled(true);
    }
    if (current_row >= 0)
        on_tableView_clicked(sql_query_model->index(current_row, 0));
    else
        reset_all_contents();
}

void RecogPrendas::on_pb_print_clicked()
{
    Imprimir *ui_impr;
    ui_impr = new Imprimir(this);
    ui_impr->db = db;
    ui_impr->is_recibo = false;
    ui_impr->is_complete_invoice = false;
    ui_impr->le_n_ticket->setText(ui->le_nr_ticket->text());
    ui_impr->get_ticket_info();
    ui_impr->create_ticket_excel(false, ui->pb_payment->isChecked());
    ui_impr->print_ticket();
}
