#include "mainwindow.h"
#include "./ui_mainwindow.h"

#define TABLE_TICKET_QNTY   0
#define TABLE_TICKET_GARM   1
#define TABLE_TICKET_SIZE   2
#define TABLE_TICKET_SERV   3
#define TABLE_TICKET_OBSE   4
#define TABLE_TICKET_PRIC   5

int pb_added_rows = 0;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("C:/work/personal_projects/tintoreria/laideal/laideal.db");
    mainwindow_initial_settings();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::mainwindow_initial_settings()
{
    MainWindow::setWindowTitle("La Ideal");
    // Table settings
    ui->table_ticket->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->table_ticket->verticalHeader()->setVisible(false);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_QNTY, 60);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_GARM, 400);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_SERV, 70);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_OBSE, 150);
    // Date settings
    ui->de_date_recep->setDate(QDate::currentDate());
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
    ui->pb_payment->setChecked(false);
}

/********************************************************************************************
 * FUNCTIONS FOR MAINWINDOW OBJECTS
 *******************************************************************************************/

void MainWindow::set_next_ticket_number()
{
    db.open();
    QSqlQuery q;
    q.exec("SELECT MAX(n_recibo) FROM ingresos");
    if (q.isSelect())
    {
        if(q.first())
            ui->le_nr_ticket->setText(QString::number(q.value(0).toInt() + 1));
        else
            qDebug() << "Query is not available!";
    }
    else
        qDebug() << "Query is not Select!";
    q.clear();
    db.close();
}

void MainWindow::populate_cb_client()
{
    db.open();
    QSqlQuery q;
    q.exec(QString::fromStdString("SELECT nombre FROM clientes"));
    if (q.isSelect())
    {
        QStringList client_list;
        while(q.next())
            client_list.append(q.value(0).toString());
        ui->cb_client->addItems(client_list);
    }
    else
        qDebug() << "Query is not Select!";
    q.clear();
    db.close();
    ui->cb_client->setCurrentText("");
}

void MainWindow::resize_table()
{
    for (int i=0; i < pb_added_rows; i++)
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
    db.open();
    QSqlQuery q;
    q.exec(QString::fromStdString("SELECT nombre FROM prendas"));
    QStringList garment_list;
    if (q.isSelect())
    {
        while(q.next())
            garment_list.append(q.value(0).toString());
    }
    else
        qDebug() << "Query is not Select!";
    q.clear();
    db.close();
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
    db.open();
    QSqlQuery q;
    if (service_text == "Limp.")
    {
        //qDebug() << text;
        q.exec(QString::fromStdString("SELECT precio_limpieza FROM prendas WHERE nombre LIKE '" + garment_text.toStdString() + "'"));
    }
    else if (service_text == "Plan.")
    {
        q.exec(QString::fromStdString("SELECT precio_plancha FROM prendas WHERE nombre LIKE '" + garment_text.toStdString() + "'"));
    }
    if (q.isSelect())
    {
        if (q.first())
        {
            QTableWidgetItem *qnty_item(ui->table_ticket->item(garment_row, TABLE_TICKET_QNTY));
            QTableWidgetItem *item = new QTableWidgetItem;
            item->setText("");
            if (qnty_item)
            {
                double price = qnty_item->text().toFloat() * q.value(0).toString().toFloat();
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
        else
            qDebug() << "Query is not available.";
    }
    else
        qDebug() << "Query is not Select.";
    q.clear();
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
        reset_all_contents();
    else if (button == ui->bb_save_reset->button(QDialogButtonBox::Save))
    {
        // save_ticket();
        reset_all_contents();
    }
}

void MainWindow::on_cb_client_editTextChanged(const QString &arg1)
{
    bool client_exists;
    if (arg1 != "")
    {
        db.open();
        // Fill client phone
        QSqlQuery q;
        q.exec(QString::fromStdString("SELECT tel_fijo FROM clientes WHERE nombre LIKE '" + arg1.toStdString() + "%'"));
        if (q.isSelect())
        {
            if (q.first()) {
                ui->le_phone->setText(q.value(0).toString());
                client_exists = 1;
            }
            else
            {
                qDebug() << "Client is not found in the database.";
                client_exists = 0;
            }
        }
        else
        {
            qDebug() << "Query is not Select!";
            client_exists = 0;
        }
        q.clear();
        if (client_exists) {
            // Fill client mobile
            q.exec(QString::fromStdString("SELECT movil FROM clientes WHERE nombre LIKE '" + arg1.toStdString() + "%'"));
            if (q.isSelect())
            {
                if (q.first())
                    ui->le_mobile->setText(q.value(0).toString());
                else
                    qDebug() << "Query is not available!";
            }
            else
                qDebug() << "Query is not Select!";
            q.clear();
            // Fill client address
            q.exec(QString::fromStdString("SELECT direccion FROM clientes WHERE nombre LIKE '" + arg1.toStdString() + "%'"));
            if (q.isSelect())
            {
                if (q.first())
                    ui->le_addr->setText(q.value(0).toString());
                else
                    qDebug() << "Query is not available!";
            }
            else
                qDebug() << "Query is not Select!";
            q.clear();
        }
        db.close();
    }
}

void MainWindow::on_table_ticket_cellChanged(int row, int column)
{
    QComboBox *cb_garment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
    if (column == TABLE_TICKET_QNTY || column == TABLE_TICKET_SIZE)
    {
        if (cb_garment->currentText() != "")
        {
            //qDebug() << cb_garment->currentText();
            cbGarmChanged(cb_garment->currentText());
        }
    }
    else if (column == TABLE_TICKET_PRIC)
    {
        double total_price = 0.0;
        for (int row_cnt=0; row_cnt < ui->table_ticket->rowCount(); row_cnt++)
        {
            QTableWidgetItem *price_item(ui->table_ticket->item(row_cnt, column));
            if (price_item && price_item->text() != "" && price_item->text().toFloat() != 0.0)
            {
                total_price = total_price + price_item->text().toFloat();
            }
        }
        ui->le_cost_total->setText(QString::number(total_price, 'f', 2));
    }
    /* Never accesed:
     * else if (column == TABLE_TICKET_GARM || column == TABLE_TICKET_SERV)
     * {
     * }
     */
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

/********************************************************************************************
 * FUNCTIONS FOR TASKBAR OBJECTS
 *******************************************************************************************/

void MainWindow::on_actionCerrar_triggered()
{
    QCoreApplication::quit();
}

