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
    // Set ticket number
    QSqlQuery q;
    db.open();
    q.exec("SELECT MAX(n_recibo) FROM ingresos");
    if (q.isSelect())
    {
        if(q.first())
        {
            ui->le_nr_ticket->setText(QString::number(q.value(0).toInt() + 1));
        }
        else
            qDebug() << "Query is not available!";
    }
    else
        qDebug() << "Query is not Select!";
    q.clear();
    db.close();
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

void MainWindow::on_pb_payment_toggled(bool checked)
{
    if (checked)
    {
        ui->pb_payment->setText("SI");
    }
    else
    {
        ui->pb_payment->setText("NO");
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
        // save_ticket();
        reset_all_contents();
    }
}

void MainWindow::reset_all_contents()
{
    ui->cb_client->clearEditText();
    ui->le_addr->clear();
    ui->le_cost_total->clear();
    ui->le_mobile->clear();
    ui->le_phone->clear();
    //update_ticket();
    ui->pb_payment->setChecked(false);
    ui->table_ticket->clearContents();
    set_service_to_cb();
}
