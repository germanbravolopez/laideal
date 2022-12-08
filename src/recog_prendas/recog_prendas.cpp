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
    ui->table->verticalHeader()->setVisible(false);
}

void RecogPrendas::reset_all_contents()
{
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
    is_cell_clicked = false;
    model_updated = false;
    clear_table_remove_rows();
}

void RecogPrendas::clear_table_remove_rows()
{
    ui->table->clearContents();
    ui->table->setRowCount(0);
}

void RecogPrendas::setup_model_table(QAbstractItemModel *model)
{
    for (int row = 0; row < model->rowCount(); row++)
    {
        // Insert new row
        ui->table->insertRow(ui->table->rowCount());
        for (int column = 0; column < ui->table->columnCount(); column++)
        {
            ui->table->setItem(row, column, new QTableWidgetItem(model->data(model->index(row, column)).toString()));
            //qDebug()<<model->data(model->index(row, column)).toString();
            //qDebug()<<ui->table->item(row, column)->text();
        }
    }
    // Resize columns
    ui->table->resizeColumnsToContents();
    ui->table->sortByColumn(TABLE_TICKET, Qt::AscendingOrder);
}

void RecogPrendas::check_new_search_may_proceed()
{
    if (model_updated)
    {
        int res = QMessageBox::question(this, tr("Datos actualizados"),
                              tr("Los datos de la búsqueda anterior han sido modificados pero no se han guardado.\n"
                              "¿Desea guardar los datos antes de continuar con una nueva búsqueda?"),
                              QMessageBox::Cancel | QMessageBox::Save,
                              QMessageBox::Save);
        if (res == QMessageBox::Save)
        {
            qDebug()<<"save_model_changes()";
        }
    }
}

void RecogPrendas::on_le_search_returnPressed()
{
    on_pb_search_clicked();
}

void RecogPrendas::on_pb_search_clicked()
{
    check_new_search_may_proceed();
    reset_all_contents();
    QSqlQueryModel *sql_query_model = new QSqlQueryModel;
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
            QMessageBox::question(this, tr("Búsqueda incorrecta"),
                                  tr("El contenido de la búsqueda no se ha identificado.\n"
                                  "Hablar con Germán..."),
                                  QMessageBox::Ok, QMessageBox::Ok);
            qDebug() << "Text is not identified!";
        }
        // Setup editable model to keep track of the changes
        setup_model_table(sql_query_model);
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
        clear_table_remove_rows();
    }
}

void RecogPrendas::on_pb_reset_clicked()
{
    reset_all_contents();
    ui->le_search->setText("");
}

void RecogPrendas::on_pb_payment_toggled(bool checked)
{
    if (checked)
    {
        ui->pb_payment->setText("SI");
        ui->pb_payment->setStyleSheet("background-color: green; font-size: 20px");
        if (is_cell_clicked)
        {
            ui->table->setItem(row_clicked_cell, TABLE_IS_PAYED, new QTableWidgetItem("SI"));
            model_updated = true;
        }
    }
    else
    {
        ui->pb_payment->setText("NO");
        ui->pb_payment->setStyleSheet("background-color: red; font-size: 20px");
        if (is_cell_clicked)
        {
            ui->table->setItem(row_clicked_cell, TABLE_IS_PAYED, new QTableWidgetItem("NO"));
            model_updated = true;
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
            ui->table->setItem(row_clicked_cell, TABLE_STATE, new QTableWidgetItem("Recogido"));
            model_updated = true;
        }
    }
    else
    {
        ui->pb_state->setText("En tienda");
        ui->pb_state->setStyleSheet("background-color: red; font-size: 20px");
        if (is_cell_clicked)
        {
            ui->table->setItem(row_clicked_cell, TABLE_STATE, new QTableWidgetItem("En tienda"));
            model_updated = true;
        }
    }
}

void RecogPrendas::on_table_cellDoubleClicked(int row, int column)
{
    bool local_model_updated = model_updated;
    // Save global variables for tracking index of clicked cell
    is_cell_clicked = true;
    row_clicked_cell = row;
    column_clicked_cell = column;
    // Edit objects
    ui->le_nr_ticket->setText(ui->table->item(row, TABLE_TICKET)->text());
    ui->le_client->setText(ui->table->item(row, TABLE_CLIENT)->text());
    ui->le_phone->setText(select_from_where_like(db, "movil", "clientes", "nombre", ui->le_client->text(), true));
    ui->le_garm->setText(ui->table->item(row, TABLE_GARMENT)->text());
    ui->le_qty->setText(ui->table->item(row, TABLE_QUANTITY)->text());
    ui->le_servic->setText(ui->table->item(row, TABLE_SERVICE)->text());
    ui->le_size->setText(ui->table->item(row, TABLE_SIZE)->text());
    ui->le_price->setText(ui->table->item(row, TABLE_PRICE)->text());
    ui->le_obsv->setText(ui->table->item(row, TABLE_OBSERV)->text());
    ui->pb_payment->setChecked(ui->table->item(row, TABLE_IS_PAYED)->text() == "SI");
    ui->pb_state->setChecked(ui->table->item(row, TABLE_STATE)->text() == "Recogido");
    ui->de_date_recep->setDate(QDate::fromString(ui->table->item(row, TABLE_DATE_RCP)->text(),"dd-MM-yyyy"));
    ui->de_date_paym->setDate(QDate::fromString(ui->table->item(row, TABLE_DATE_PAY)->text(),"dd-MM-yyyy"));
    ui->de_date_pickup->setDate(QDate::fromString(ui->table->item(row, TABLE_DATE_PKU)->text(),"dd-MM-yyyy"));
    // Leave model_updated unchanged
    if (local_model_updated)
        model_updated = true;
    else
        model_updated = false;
}

void RecogPrendas::on_pb_save_clicked()
{
    //save_model_changes();
}

void RecogPrendas::on_le_obsv_editingFinished()
{
    if (is_cell_clicked)
    {
        ui->table->setItem(row_clicked_cell, TABLE_OBSERV, new QTableWidgetItem(ui->le_obsv->text()));
        model_updated = true;
    }
}
