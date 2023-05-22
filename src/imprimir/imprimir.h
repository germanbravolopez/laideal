#ifndef IMPRIMIR_H
#define IMPRIMIR_H

#include <QDialog>
#include <QSqlDatabase>
#include <QMessageBox>
#include <QSqlQueryModel>
#include <QDateTime>
#include <QInputDialog>
#include <QTextDocument>

#define TABLE_TICKET     0
#define TABLE_CLIENT     1
#define TABLE_DATE_RCP   2
#define TABLE_DATE_PAY   3
#define TABLE_DATE_PKU   4
#define TABLE_PRICE      5
#define TABLE_IS_PAYED   6
#define TABLE_STATE      7
#define TABLE_QUANTITY   8
#define TABLE_GARMENT    9
#define TABLE_SIZE      10
#define TABLE_SERVICE   11
#define TABLE_OBSERV    12
#define TABLE_EDIT_LOCK 13

namespace Ui {
class Imprimir;
}

class Imprimir : public QDialog
{
    Q_OBJECT

public:
    QSqlDatabase db;
    QSqlQueryModel *sql_query_model;
    bool is_recibo, is_complete_invoice;
    explicit Imprimir(QWidget *parent = nullptr);
    ~Imprimir();

private slots:
    void get_ticket_info();
    bool check_ticket_paid();
    QString add_extra_info_to_invoice(QString title, QString request);
    QString create_html_ticket(bool copy_for_client);
    void print_ticket(QString html_text);
    void on_bb_ok_cancel_accepted();
    void on_bb_ok_cancel_rejected();

private:
    Ui::Imprimir *ui;

};

#endif // IMPRIMIR_H
