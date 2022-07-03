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
    db.setDatabaseName("C:/MyDocuments/TintoLaIdeal/laideal/laideal.db");
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
    }
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
    if (arg1 != "")
    {
        db.open();
        // fill client phone
        QSqlQuery q;
        q.exec(QString::fromStdString("SELECT tel_fijo FROM clientes WHERE nombre LIKE '" + arg1.toStdString() + "%'"));
        if (q.isSelect())
        {
            if(q.first())
                ui->le_phone->setText(q.value(0).toString());
            else
                qDebug() << "Query is not available!";
        }
        else
            qDebug() << "Query is not Select!";
        q.clear();
        // fill client mobile
        q.exec(QString::fromStdString("SELECT movil FROM clientes WHERE nombre LIKE '" + arg1.toStdString() + "%'"));
        if (q.isSelect())
        {
            if(q.first())
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
            if(q.first())
                ui->le_addr->setText(q.value(0).toString());
            else
                qDebug() << "Query is not available!";
        }
        else
            qDebug() << "Query is not Select!";
        q.clear();
        db.close();
    }
}

void MainWindow::on_table_ticket_cellChanged(int row, int column)
{
    QSqlQuery q;
    QString sql_query;

    qDebug() << ui->table_ticket->cellWidget(row, column + 1)->whatsThis();
    qDebug() << ui->table_ticket->findItems("cb_prenda_0", Qt::MatchCaseSensitive).data();
            //->itemText(0).toStdString();
    switch (column)
    {
        case TABLE_TICKET_CANT:
            break;
        case TABLE_TICKET_PREN:
            // look the name of the garment in the db
            db.open();
            if (ui->table_ticket->item(row, column)->text().toStdString() == "Limpieza")
            {
                sql_query = "SELECT precio_limpieza FROM prendas WHERE nombre LIKE '" + ui->table_ticket->item(row, column)->text() + "%'";
            }
            else if (ui->table_ticket->item(row, column)->text().toStdString() == "Plancha")
            {
                sql_query = "SELECT precio_plancha FROM prendas WHERE nombre LIKE '" + ui->table_ticket->item(row, column)->text() + "%'";
            }
            q.exec(sql_query);
            if (q.isSelect())
            {
                if(q.first())
                    ui->table_ticket->item(row, TABLE_TICKET_IMPO)->setText(q.value(0).toString());
                else
                    qDebug() << "Query is not available!";
            }
            else
                qDebug() << "Query is not Select!";
            q.clear();
            break;
        case TABLE_TICKET_TAMA:
            break;
        case TABLE_TICKET_SERV:
            break;
        case TABLE_TICKET_IMPO:
            break;
    }
}
