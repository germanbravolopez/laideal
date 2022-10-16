#include "mainwindow.h"
#include "./ui_mainwindow.h"

#define TABLE_TICKET_CANT   0
#define TABLE_TICKET_PREN   1
#define TABLE_TICKET_TAMA   2
#define TABLE_TICKET_SERV   3
#define TABLE_TICKET_IMPO   4

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
    ui->table_ticket->setColumnWidth(TABLE_TICKET_CANT, 60);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_PREN, 400);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_SERV, 110);
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
    set_service_to_cb();
    set_garment_to_cb_and_populate();
    ui->le_addr->clear();
    ui->le_cost_total->clear();
    ui->le_mobile->clear();
    ui->le_phone->clear();
    ui->pb_payment->setChecked(false);
}

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

void MainWindow::set_service_to_cb()
{
    for (int row=0; row < ui->table_ticket->rowCount(); row++)
    {
        QComboBox *comBox = new QComboBox();
        comBox->addItem("Limpieza");
        comBox->addItem("Plancha");
        ui->table_ticket->setCellWidget(row, TABLE_TICKET_SERV, comBox);
    }
}

void MainWindow::set_garment_to_cb_and_populate()
{
    // get garment from db to a string list
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
    // create cb for all garment and populate the list
    for (int row=0; row < ui->table_ticket->rowCount(); row++)
    {
        QComboBox *comBoxPrenda = new QComboBox();
        comBoxPrenda->setAutoFillBackground(true);
        comBoxPrenda->setEditable(true);
        comBoxPrenda->addItems(garment_list);
        comBoxPrenda->setCurrentText("");
        comBoxPrenda->setObjectName("cb_prenda_" + QString::number(row));
        ui->table_ticket->setCellWidget(row, TABLE_TICKET_PREN, comBoxPrenda);
        // connect each ComboBox to a different function
        if (row == 0) {
            connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(0, TABLE_TICKET_PREN)),
                    SIGNAL(currentTextChanged(QString)),
                    this, SLOT(textChanged_0(QString)));
        } else if (row == 1) {
            connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(1, TABLE_TICKET_PREN)),
                    SIGNAL(currentTextChanged(QString)),
                    this, SLOT(textChanged_1(QString)));
        } else if (row == 2) {
            connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(2, TABLE_TICKET_PREN)),
                    SIGNAL(currentTextChanged(QString)),
                    this, SLOT(textChanged_2(QString)));
        } else if (row == 3) {
            connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(3, TABLE_TICKET_PREN)),
                    SIGNAL(currentTextChanged(QString)),
                    this, SLOT(textChanged_3(QString)));
        } else if (row == 4) {
            connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(4, TABLE_TICKET_PREN)),
                    SIGNAL(currentTextChanged(QString)),
                    this, SLOT(textChanged_4(QString)));
        } else if (row == 5) {
            connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(5, TABLE_TICKET_PREN)),
                    SIGNAL(currentTextChanged(QString)),
                    this, SLOT(textChanged_5(QString)));
        } else if (row == 6) {
            connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(6, TABLE_TICKET_PREN)),
                    SIGNAL(currentTextChanged(QString)),
                    this, SLOT(textChanged_6(QString)));
        } else if (row == 7) {
            connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(7, TABLE_TICKET_PREN)),
                    SIGNAL(currentTextChanged(QString)),
                    this, SLOT(textChanged_7(QString)));
        } else if (row == 8) {
            connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(8, TABLE_TICKET_PREN)),
                    SIGNAL(currentTextChanged(QString)),
                    this, SLOT(textChanged_8(QString)));
        } else if (row == 9) {
            connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(9, TABLE_TICKET_PREN)),
                    SIGNAL(currentTextChanged(QString)),
                    this, SLOT(textChanged_9(QString)));
        }
    }
}

void MainWindow::get_garment_price()
{

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
        // fill client phone
        QSqlQuery q;
        q.exec(QString::fromStdString("SELECT tel_fijo FROM clientes WHERE nombre LIKE '" + arg1.toStdString() + "%'"));
        if (q.isSelect())
        {
            if (q.first()) {
                ui->le_phone->setText(q.value(0).toString());
                client_exists = 1;
            } else {
                qDebug() << "Client is not found in the database.";
                client_exists = 0;
            }
        }
        else {
            qDebug() << "Query is not Select!";
            client_exists = 0;
        }
        q.clear();
        if (client_exists) {
            // fill client mobile
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
            // fill client address
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
    switch (column)
    {
        case TABLE_TICKET_CANT:
            break;
        case TABLE_TICKET_PREN:
            // textChanged_X are created, this case is never accessed
            break;
        case TABLE_TICKET_TAMA:
            break;
        case TABLE_TICKET_SERV:
            break;
        case TABLE_TICKET_IMPO:
            break;
    }
}

void MainWindow::textChanged_0(const QString &text)
{
    int garment_row = 0;
    QComboBox *cb_service = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(garment_row, TABLE_TICKET_SERV));
    QComboBox *cb_garment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(garment_row, TABLE_TICKET_PREN));
    // check garment is included in combobox
    if (cb_garment->findText(text, Qt::MatchExactly) != -1)
    {
        // look the name of the garment in the db
        db.open();
        QSqlQuery q;
        if (cb_service->currentText() == "Limpieza")
        {
            qDebug() << text;
            q.exec(QString::fromStdString("SELECT precio_limpieza FROM prendas WHERE nombre LIKE '" + text.toStdString() + "'"));
        } else if (cb_service->currentText() == "Plancha")
        {
            q.exec(QString::fromStdString("SELECT precio_plancha FROM prendas WHERE nombre LIKE '" + text.toStdString() + "'"));
        }
        if (q.isSelect())
        {
            if (q.first()) {
                //qDebug() << ui->table_ticket->item(garment_row, TABLE_TICKET_CANT)->text();
                QString price = q.value(0).toString();
                QTableWidgetItem *item = new QTableWidgetItem;
                item->setText(price);
                ui->table_ticket->setItem(garment_row, TABLE_TICKET_IMPO, item);
            } else {
                qDebug() << "Query is not available.";
            }
        }
        else {
            qDebug() << "Query is not Select!";
        }
        q.clear();
    }
}
void MainWindow::textChanged_1(const QString &text)
{
    // Do something here on ComboBox index change
    QComboBox *myCB = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(1, TABLE_TICKET_PREN));
    qDebug() << myCB->currentText();
    qDebug() << text;
}
void MainWindow::textChanged_2(const QString &text)
{
    // Do something here on ComboBox index change
    QComboBox *myCB = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(2, TABLE_TICKET_PREN));
    qDebug() << myCB->currentText();
    qDebug() << text;
}
void MainWindow::textChanged_3(const QString &text)
{
    // Do something here on ComboBox index change
    QComboBox *myCB = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(3, TABLE_TICKET_PREN));
    qDebug() << myCB->currentText();
    qDebug() << text;
}
void MainWindow::textChanged_4(const QString &text)
{
    // Do something here on ComboBox index change
    QComboBox *myCB = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(4, TABLE_TICKET_PREN));
    qDebug() << myCB->currentText();
    qDebug() << text;
}
void MainWindow::textChanged_5(const QString &text)
{
    // Do something here on ComboBox index change
    QComboBox *myCB = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(5, TABLE_TICKET_PREN));
    qDebug() << myCB->currentText();
    qDebug() << text;
}
void MainWindow::textChanged_6(const QString &text)
{
    // Do something here on ComboBox index change
    QComboBox *myCB = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(6, TABLE_TICKET_PREN));
    qDebug() << myCB->currentText();
    qDebug() << text;
}
void MainWindow::textChanged_7(const QString &text)
{
    // Do something here on ComboBox index change
    QComboBox *myCB = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(7, TABLE_TICKET_PREN));
    qDebug() << myCB->currentText();
    qDebug() << text;
}
void MainWindow::textChanged_8(const QString &text)
{
    // Do something here on ComboBox index change
    QComboBox *myCB = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(8, TABLE_TICKET_PREN));
    qDebug() << myCB->currentText();
    qDebug() << text;
}
void MainWindow::textChanged_9(const QString &text)
{
    // Do something here on ComboBox index change
    QComboBox *myCB = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(9, TABLE_TICKET_PREN));
    qDebug() << myCB->currentText();
    qDebug() << text;
}
