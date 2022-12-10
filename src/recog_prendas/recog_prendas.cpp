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
}

void RecogPrendas::on_le_search_returnPressed()
{
    on_pb_search_clicked();
}

void RecogPrendas::on_pb_search_clicked()
{
    //check_new_search_may_proceed();
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
            QMessageBox::question(this, tr("Búsqueda incorrecta"),
                                  tr("El contenido de la búsqueda no se ha identificado.\n"
                                  "Hablar con Germán..."),
                                  QMessageBox::Ok, QMessageBox::Ok);
            qDebug() << "Text is not identified!";
        }
        // Complete model and set to the view
        sql_query_model->setHeaderData(TABLE_TICKET   ,  Qt::Horizontal, tr("n_recibo"));
        sql_query_model->setHeaderData(TABLE_CLIENT   ,  Qt::Horizontal, tr("cliente"));
        sql_query_model->setHeaderData(TABLE_DATE_RCP ,  Qt::Horizontal, tr("fecha_recepcion"));
        sql_query_model->setHeaderData(TABLE_DATE_PAY ,  Qt::Horizontal, tr("fecha_pago"));
        sql_query_model->setHeaderData(TABLE_DATE_PKU ,  Qt::Horizontal, tr("fecha_recogida"));
        sql_query_model->setHeaderData(TABLE_PRICE    ,  Qt::Horizontal, tr("importe"));
        sql_query_model->setHeaderData(TABLE_IS_PAYED ,  Qt::Horizontal, tr("pagado"));
        sql_query_model->setHeaderData(TABLE_STATE    ,  Qt::Horizontal, tr("estado"));
        sql_query_model->setHeaderData(TABLE_SIZE     ,  Qt::Horizontal, tr("cantidad"));
        sql_query_model->setHeaderData(TABLE_GARMENT  ,  Qt::Horizontal, tr("prenda"));
        sql_query_model->setHeaderData(TABLE_SIZE     , Qt::Horizontal, tr("size"));
        sql_query_model->setHeaderData(TABLE_SERVICE  , Qt::Horizontal, tr("servicio"));
        sql_query_model->setHeaderData(TABLE_OBSERV   , Qt::Horizontal, tr("observaciones"));
        sql_query_model->setHeaderData(TABLE_EDIT_LOCK, Qt::Horizontal, tr("edit_lock"));
        // Setup editable model to keep track of the changes
        //setup_model_table(sql_query_model);
        // Remove edit_lock column to show
        sql_query_model->removeColumn(TABLE_EDIT_LOCK);
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
    }
    else
    {
        ui->pb_payment->setText("NO");
        ui->pb_payment->setStyleSheet("background-color: red; font-size: 20px");
    }
}

void RecogPrendas::on_pb_state_toggled(bool checked)
{
    if (checked)
    {
        ui->pb_state->setText("Recogido");
        ui->pb_state->setStyleSheet("background-color: green; font-size: 20px");
    }
    else
    {
        ui->pb_state->setText("En tienda");
        ui->pb_state->setStyleSheet("background-color: red; font-size: 20px");
    }
}

void RecogPrendas::on_tableView_doubleClicked(const QModelIndex &index)
{
    //model_index_clicked = index;
    ui->le_nr_ticket->setText(sql_query_model->data(sql_query_model->index(index.row(), TABLE_TICKET)).toString());
    ui->le_client->setText(sql_query_model->data(sql_query_model->index(index.row(), TABLE_CLIENT)).toString());
    ui->le_phone->setText(select_from_where_like(db, "movil", "clientes", "nombre", ui->le_client->text(), true));
    ui->le_garm->setText(sql_query_model->data(sql_query_model->index(index.row(), TABLE_GARMENT)).toString());
    ui->le_qty->setText(sql_query_model->data(sql_query_model->index(index.row(), TABLE_QUANTITY)).toString());
    ui->le_servic->setText(sql_query_model->data(sql_query_model->index(index.row(), TABLE_SERVICE)).toString());
    ui->le_size->setText(sql_query_model->data(sql_query_model->index(index.row(), TABLE_SIZE)).toString());
    ui->le_price->setText(sql_query_model->data(sql_query_model->index(index.row(), TABLE_PRICE)).toString());
    ui->le_obsv->setText(sql_query_model->data(sql_query_model->index(index.row(), TABLE_OBSERV)).toString());
    ui->pb_payment->setChecked(sql_query_model->data(sql_query_model->index(index.row(), TABLE_IS_PAYED)).toString() == "SI");
    ui->pb_state->setChecked(sql_query_model->data(sql_query_model->index(index.row(), TABLE_STATE)).toString() == "Recogido");
    ui->de_date_recep->setDate(QDate::fromString(sql_query_model->data(sql_query_model->index(index.row(), TABLE_DATE_RCP)).toString(),"dd-MM-yyyy"));
    ui->de_date_paym->setDate(QDate::fromString(sql_query_model->data(sql_query_model->index(index.row(), TABLE_DATE_PAY)).toString(),"dd-MM-yyyy"));
    ui->de_date_pickup->setDate(QDate::fromString(sql_query_model->data(sql_query_model->index(index.row(), TABLE_DATE_PKU)).toString(),"dd-MM-yyyy"));
}

void RecogPrendas::on_pb_save_clicked()
{
    //save_model_changes();
    int ok = sql_query_model->setData(sql_query_model->index(0,0), "hola", Qt::EditRole);
    qDebug()<<ok;
}
/*
void RecogPrendas::setup_model_table(QAbstractItemModel *model)
{
    for (int row = 0; row < model->rowCount(); row++)
    {
        for (int column = 0; column < model->columnCount(); column++)
        {
            QTableWidgetItem *item = new QTableWidgetItem;
            item->setText(model->data(model->index(row, column)).toString());
            table_widget_model.setItem(row, column, item);
            qDebug()<<sql_query_model->data(sql_query_model->index(row, column)).toString();
            qDebug()<<item->text();
        }
    }
}

void RecogPrendas::check_new_search_may_proceed()
{
    bool ok = compare_models();
    if (!ok)
    {
        save_model_changes();
    }
}

bool RecogPrendas::compare_models()
{
    for (int row = 0; row < sql_query_model->rowCount(); row++)
    {
        for (int column = 0; column < sql_query_model->columnCount(); column++)
        {
            QTableWidgetItem *item(table_widget_model.item(row, column));
            qDebug()<<sql_query_model->data(sql_query_model->index(row, column)).toString();
            qDebug()<<table_widget_model.item(row, column)->text();
            qDebug()<<item->text();
            //if (sql_query_model->data(sql_query_model->index(row, column)).toString() != item->text())
            //{
            //    return 0;
            //}
        }
    }
    return 1;
}

void RecogPrendas::save_model_changes()
{
    int rpy = QMessageBox::question(this, tr("Actualizar datos"),
                              tr("¿Desea guardar los cambios efectuados?"),
                              QMessageBox::Cancel | QMessageBox::Save,
                              QMessageBox::Save);
    if (rpy == QMessageBox::Save)
    {
        for (int row = 0; row < sql_query_model->rowCount(); row++)
        {
            for (int column = 0; column < sql_query_model->columnCount(); column++)
            {
                QTableWidgetItem *item = table_widget_model.item(row, column);
                if (sql_query_model->data(sql_query_model->index(row, column)).toString() != item->text())
                {
                    qDebug() << "Guardar item[" + QString::number(row) + "][" + QString::number(column) + "]; \
                                old = '" + sql_query_model->data(sql_query_model->index(row, column)).toString() + "'; \
                                new = '" + item->text() + "'\n";
                }
            }
        }
    }
    else
    {
        qDebug() << "cancelar";
    }
}
*/
