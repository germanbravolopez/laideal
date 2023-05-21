#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "sql_lite.h"
#include "ingresos.h"
#include "gastos.h"
#include "lista_prendas.h"
#include "lista_clientes.h"
#include "lista_proveedores.h"
#include "recog_prendas.h"
#include "contabilidad.h"
#include "facturas.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(DB_PATH);
    mainwindow_initial_settings();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::mainwindow_initial_settings()
{
    // Taskbar
    ui->menuArchivo->setToolTipsVisible(true);
    ui->menuHerramientas->setToolTipsVisible(true);
    ui->menuImprimir_ticket->setToolTipsVisible(true);
    ui->menuVisualizar->setToolTipsVisible(true);
    // Table settings
    ui->table_ticket->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->table_ticket->verticalHeader()->setVisible(false);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_QNTY, 60);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_GARM, 400);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_SERV, 70);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_OBSE, 150);
    // Push button settings
    ui->pb_payment->setStyleSheet("background-color: red; font-size: 20px");
    reset_all_contents();
}

void MainWindow::reset_all_contents()
{
    ui->cb_client->clear();
    ui->table_ticket->clearContents();
    set_next_ticket_number();
    populate_cb_client();
    resize_table();
    set_service_to_cb(0); // Set rows starting from 0
    set_garment_to_cb_and_populate(0); // Set rows starting from 0
    ui->le_addr->clear();
    ui->le_cost_total->clear();
    ui->le_mobile->clear();
    ui->le_phone->clear();
    ui->de_date_recep->setDate(QDate::currentDate());
    ui->pb_payment->setChecked(false);
}

/********************************************************************************************
 * FUNCTIONS FOR MAINWINDOW OBJECTS
 *******************************************************************************************/

void MainWindow::set_next_ticket_number()
{
    ui->le_nr_ticket->setText(QString::number(read_max_value_in_column_from_table(db, "n_recibo", "ingresos") + 1));
}

void MainWindow::populate_cb_client()
{
    ui->cb_client->addItems(read_column_from_table(db, "nombre", "clientes"));
    ui->cb_client->setCurrentText("");
}

void MainWindow::resize_table()
{
    for (int row = 0; row < pb_added_rows; row++)
    {
        ui->table_ticket->removeRow(ui->table_ticket->rowCount() - 1);
    }
    pb_added_rows = 0;
}

void MainWindow::set_service_to_cb(int initial_row = 0)
{
    for (int row = initial_row; row < ui->table_ticket->rowCount(); row++)
    {
        QComboBox *comBox = new QComboBox();
        comBox->addItem("Limp.");
        comBox->addItem("Plan.");
        comBox->setStyleSheet("background-color: white");
        ui->table_ticket->setCellWidget(row, TABLE_TICKET_SERV, comBox);
        // Connect each ComboBox to a different function
        connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_SERV)),
                SIGNAL(currentTextChanged(QString)),
                this, SLOT(cbServChanged(QString)));
    }
}

void MainWindow::set_garment_to_cb_and_populate(int initial_row = 0)
{
    // Get garment from db to a string list
    QStringList garment_list = read_column_from_table(db, "nombre", "prendas");
    // Create cb for all garment and populate the list
    for (int row = initial_row; row < ui->table_ticket->rowCount(); row++)
    {
        QComboBox *comBoxPrenda = new QComboBox();
        comBoxPrenda->setAutoFillBackground(true);
        comBoxPrenda->setEditable(true);
        comBoxPrenda->addItems(garment_list);
        comBoxPrenda->setCurrentText("");
        comBoxPrenda->setObjectName("cb_prenda_" + QString::number(row));
        ui->table_ticket->setCellWidget(row, TABLE_TICKET_GARM, comBoxPrenda);
        // Connect each ComboBox to a different function
        connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM)),
                SIGNAL(currentTextChanged(QString)),
                this, SLOT(cbGarmChanged(QString)));
    }
}

void MainWindow::set_garment_price(int garment_row, QString garment_text, QString service_text)
{
    QTableWidgetItem *qnty_item(ui->table_ticket->item(garment_row, TABLE_TICKET_QNTY));
    QTableWidgetItem *item = new QTableWidgetItem;
    item->setText("");
    if (qnty_item)
    {
        double price = qnty_item->text().toFloat() * read_garment_price(db, garment_text, service_text);
        // Check if any size is filled
        QTableWidgetItem *size_item(ui->table_ticket->item(garment_row, TABLE_TICKET_SIZE));
        if (size_item && size_item->text() != "" && size_item->text().toFloat() != 0.0)
        {
            double size = size_item->text().toFloat() * price;
            item->setText(QString::number(size, 'f', 2));
        }
        else
            item->setText(QString::number(price, 'f', 2));
    }
    else
        qDebug() << "Cantidad is empty";
    ui->table_ticket->setItem(garment_row, TABLE_TICKET_PRIC, item);
}

void MainWindow::on_pb_payment_toggled(bool checked)
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

void MainWindow::on_bb_save_reset_clicked(QAbstractButton *button)
{
    if (button == ui->bb_save_reset->button(QDialogButtonBox::Reset))
    {
        reset_all_contents();
    }
    else if (button == ui->bb_save_reset->button(QDialogButtonBox::Save))
    {
        if (validate_ticket())
        {
            check_client_data();
            save_ticket();
            reset_all_contents();
        }
    }
}

void MainWindow::on_cb_client_editTextChanged(const QString &arg1)
{
    if (arg1 != "")
    {
        ui->le_phone->setText(search_item_from_client(db, "tel_fijo", arg1));
        ui->le_mobile->setText(search_item_from_client(db, "movil", arg1));
        ui->le_addr->setText(search_item_from_client(db, "direccion", arg1));
    }
}

void MainWindow::on_table_ticket_cellChanged(int row, int column)
{
    QComboBox *cb_garment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
    if (column == TABLE_TICKET_QNTY || column == TABLE_TICKET_SIZE)
    {
        if (cb_garment->currentText() != "")
        {
            cbGarmChanged(cb_garment->currentText());
        }
    }
    else if (column == TABLE_TICKET_PRIC)
    {
        double total_price = 0.0;
        for (int row_cnt = 0; row_cnt < ui->table_ticket->rowCount(); row_cnt++)
        {
            QTableWidgetItem *price_item(ui->table_ticket->item(row_cnt, column));
            if (price_item && price_item->text() != "" && price_item->text().toFloat() != 0.0)
            {
                total_price = total_price + price_item->text().toFloat();
            }
        }
        ui->le_cost_total->setText(QString::number(total_price, 'f', 2));
    }
}

void MainWindow::on_pb_add_row_clicked()
{
    pb_added_rows++;
    ui->table_ticket->insertRow(ui->table_ticket->rowCount());
    set_service_to_cb(ui->table_ticket->rowCount() - 1);
    set_garment_to_cb_and_populate(ui->table_ticket->rowCount() - 1);
}

void MainWindow::cbGarmChanged(const QString &text)
{
    int garment_row = ui->table_ticket->currentRow();
    QComboBox *cb_service = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(garment_row, TABLE_TICKET_SERV));
    QComboBox *cb_garment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(garment_row, TABLE_TICKET_GARM));
    // Check garment is included in combobox
    if (cb_garment->findText(text, Qt::MatchExactly) != -1)
    {
        set_garment_price(garment_row, text, cb_service->currentText());
    }
}

void MainWindow::cbServChanged(const QString &text)
{
    int service_row = ui->table_ticket->currentRow();
    QComboBox *cb_garment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(service_row, TABLE_TICKET_GARM));
    if (cb_garment->currentText() != "")
    {
        set_garment_price(service_row, cb_garment->currentText(), text);
    }
}

bool MainWindow::validate_ticket()
{
    QMessageBox msgBox;
    float total_cost = 0.0;
    if (ui->cb_client->currentText().isEmpty())
    {
        msgBox.setText("No se ha introducido ningún cliente.");
        msgBox.setInformativeText("No se va a guardar nada en la tabla de ingresos.");
        msgBox.exec();
        return 0;
    }
    else
    {
        // Calculate the expected total cost
        for (int row = 0; row < ui->table_ticket->rowCount(); row++)
        {
            if (ui->table_ticket->item(row, TABLE_TICKET_PRIC))
            {
                if (!ui->table_ticket->item(row, TABLE_TICKET_QNTY))
                {
                    msgBox.setText("El valor del IMPORTE individual de la prenda " + QString::number(row + 1) + " no se puede cambiar directamente.");
                    msgBox.setInformativeText("La cantidad no puede estar vacía. No se va a guardar nada en la tabla de ingresos.");
                    msgBox.exec();
                    return 0;
                }
                else if (ui->table_ticket->item(row, TABLE_TICKET_QNTY)->text().toInt() == 0)
                {
                    msgBox.setText("El valor del IMPORTE individual de la prenda " + QString::number(row + 1) + " no se puede cambiar directamente.");
                    msgBox.setInformativeText("La cantidad no puede ser 0. No se va a guardar nada en la tabla de ingresos.");
                    msgBox.exec();
                    return 0;
                }
                else
                {
                    QComboBox *cb_garment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
                    if (cb_garment->currentText().isEmpty())
                    {
                        msgBox.setText("La prenda " + QString::number(row + 1) + " no puede tener el nombre vacío.");
                        msgBox.setInformativeText("No se va a guardar nada en la tabla de ingresos.");
                        msgBox.exec();
                        return 0;
                    }
                }
                // If there is data in the row get the float and accumulate
                total_cost = total_cost + ui->table_ticket->item(row, TABLE_TICKET_PRIC)->text().toFloat();
            }
        }
        if (total_cost == 0.0)
        {
            QComboBox *cb_garment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(0, TABLE_TICKET_GARM));
            QString left_side = cb_garment->currentText().left(8);
            if (left_side == "Alfombra")
            {
                return 1;
            }
            msgBox.setText("La suma de los IMPORTES individuales es 0.");
            msgBox.setInformativeText("No se va a guardar nada en la tabla de ingresos.");
            msgBox.exec();
            return 0;
        }
        else
        {
            // If there is data in lbl_cost_total it has to match with the previous calculation
            if (ui->le_cost_total->text().toFloat() != total_cost)
            {
                msgBox.setText("El valor del IMPORTE TOTAL no puede ser diferente al de los IMPORTES individuales.");
                msgBox.setInformativeText("No se va a guardar nada en la tabla de ingresos.");
                msgBox.exec();
                return 0;
            }
            else
            {
                // if the current date belongs to a locked quarter, data cannot be saved
                if (ui->pb_payment->text() == "SI"
                        && read_lock_for_month_and_year(db, ui->de_date_recep->date().month(), ui->de_date_recep->date().year()) == 1)
                {
                    msgBox.setText("No se puede introducir un nuevo recibo pagado en un trimestre que tiene la contabilidad cerrada.");
                    msgBox.setInformativeText("No se va a guardar nada en la tabla de ingresos.");
                    msgBox.exec();
                    return 0;
                }
                else
                    return 1;
            }
        }
    }
}

QString MainWindow::remove_special_char(QString str)
{
    str = str.normalized(QString::NormalizationForm_D).toLatin1();
    int index = str.indexOf("?");
    while (index != -1)
    {
        str = str.remove(index, 1);
        index = str.indexOf("?");
    }
    return str;
}

void MainWindow::check_client_data()
{
    QString currentClient = ui->cb_client->currentText();
    if (ui->cb_client->findText(currentClient) >= 0)
    {
        update_item_to_client(db, "tel_fijo",  ui->le_phone->text(),  currentClient);
        update_item_to_client(db, "movil",     ui->le_mobile->text(), currentClient);
        update_item_to_client(db, "direccion", ui->le_addr->text(),   currentClient);
    }
    else
    {
        currentClient = remove_special_char(currentClient.simplified().toLower());
        bool client_found = false;
        for (int idx = 0; idx < ui->cb_client->count(); idx++)
        {
            QString client_in_cb = remove_special_char(ui->cb_client->itemText(idx).simplified().toLower());
            if (currentClient == client_in_cb) client_found = true;
        }
        if (!client_found)
        {
            add_new_client(db, currentClient, ui->le_phone->text(), ui->le_addr->text(), ui->le_mobile->text());
        }
        else
        {
            QMessageBox::information(this, "Listado de clientes",
                                  "Cliente encontrado en la base de datos tras suprimir carácteres especiales como tildes o 'ñ'.\n"
                                  "Los datos introducidos para el cliente en este recibo no se han añadido al cliente en el listado. "
                                  "Si se desean actualizar los datos, añadir manualmente en el listado de clientes.",
                                  QMessageBox::Ok,
                                  QMessageBox::Ok);
        }
    }

}

void MainWindow::save_ticket()
{
    for (int row = 0; row < ui->table_ticket->rowCount(); row++)
    {
        // If there is any content in price of that row then save
        if (ui->table_ticket->item(row, TABLE_TICKET_PRIC))
        {
            db.open();
            QSqlQuery q;
            q.prepare("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, observaciones, edit_lock) \
            VALUES (:n_recibo, :cliente, :fecha_recepcion, :fecha_pago, :fecha_recogida, :importe, :pagado, :estado, :cantidad, :prenda, :size, :servicio, :observaciones, :edit_lock);");
            q.bindValue(":n_recibo", ui->le_nr_ticket->text());
            q.bindValue(":cliente", ui->cb_client->currentText());
            q.bindValue(":fecha_recepcion", ui->de_date_recep->date().toString("dd-MM-yyyy"));
            if (ui->pb_payment->text() == "SI")
                q.bindValue(":fecha_pago", ui->de_date_recep->date().toString("dd-MM-yyyy"));
            else
                q.bindValue(":fecha_pago", "");
            q.bindValue(":fecha_recogida", "");
            q.bindValue(":importe", ui->table_ticket->item(row, TABLE_TICKET_PRIC)->text());
            q.bindValue(":pagado", ui->pb_payment->text());
            q.bindValue(":estado", "En tienda");
            q.bindValue(":cantidad", ui->table_ticket->item(row, TABLE_TICKET_QNTY)->text());
            QComboBox *cb_garment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
            q.bindValue(":prenda", cb_garment->currentText());
            if (ui->table_ticket->item(row, TABLE_TICKET_SIZE))
                q.bindValue(":size", ui->table_ticket->item(row, TABLE_TICKET_SIZE)->text());
            else
                q.bindValue(":size", "");
            if (ui->table_ticket->item(row, TABLE_TICKET_OBSE))
                q.bindValue(":observaciones", ui->table_ticket->item(row, TABLE_TICKET_OBSE)->text());
            else
                q.bindValue(":observaciones", "");
            QComboBox *cb_service = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_SERV));
            q.bindValue(":servicio", cb_service->currentText());
            q.bindValue(":edit_lock", "0");
            q.exec();
            q.clear();
            db.close();
        }
    }
}

/********************************************************************************************
 * FUNCTIONS FOR TASKBAR OBJECTS
 *******************************************************************************************/

void MainWindow::on_actionCerrar_triggered()
{
    QCoreApplication::quit();
}

void MainWindow::on_actionIngresos_triggered()
{
    Ingresos *ui_ingr;
    ui_ingr = new Ingresos(this);
    ui_ingr->show();
}

void MainWindow::on_actionGastos_triggered()
{
    Gastos *ui_gast;
    ui_gast = new Gastos(this);
    ui_gast->show();
}

void MainWindow::on_actionListado_de_prendas_triggered()
{
    ListaPrendas *ui_prend;
    ui_prend = new ListaPrendas(this);
    ui_prend->db = db;
    ui_prend->show();
}

void MainWindow::on_actionListado_de_clientes_triggered()
{
    ListaClientes *ui_clien;
    ui_clien = new ListaClientes(this);
    ui_clien->show();
}

void MainWindow::on_actionListado_de_proveedores_triggered()
{
    ListaProveedores *ui_prove;
    ui_prove = new ListaProveedores(this);
    ui_prove->show();
}

void MainWindow::on_actionRecogida_de_prendas_triggered()
{
    RecogPrendas *ui_recog;
    ui_recog = new RecogPrendas(this);
    ui_recog->db = db;
    ui_recog->show();
}

void MainWindow::on_actionGenerar_contabilidad_triggered()
{
    Contabilidad *ui_contabilidad;
    ui_contabilidad = new Contabilidad(this);
    ui_contabilidad->db = db;
    ui_contabilidad->show();
}

void MainWindow::on_actionFormulario_facturas_triggered()
{
    Facturas *ui_facturas;
    ui_facturas = new Facturas(this);
    ui_facturas->db = db;
    ui_facturas->populate_empresas();
    ui_facturas->show();
}
