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
    db.setDatabaseName("C:/Users/GERBRA/MyDocuments/LaIdeal/qt/laideal/laideal.db");
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
    set_service_to_cb();
    // Date settings
    ui->de_date_recep->setDate(QDate::currentDate());
    set_next_ticket_number();
    populate_cb_client();
    reset_all_contents();
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

void MainWindow::reset_all_contents()
{
    ui->cb_client->clearEditText();
    ui->le_addr->clear();
    ui->le_cost_total->clear();
    ui->le_mobile->clear();
    ui->le_phone->clear();
    ui->pb_payment->setChecked(false);
    ui->table_ticket->clearContents();
    set_service_to_cb();
    set_next_ticket_number();
}

void MainWindow::on_pb_payment_toggled(bool checked)
{
    if (checked)
        ui->pb_payment->setText("SI");
    else
        ui->pb_payment->setText("NO");
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
