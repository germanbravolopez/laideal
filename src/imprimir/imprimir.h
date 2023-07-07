#ifndef IMPRIMIR_H
#define IMPRIMIR_H

#include <QDialog>
#include <QSqlDatabase>
#include <QMessageBox>
#include <QSqlQueryModel>
#include <QDateTime>
#include <QInputDialog>
#include <QFile>
#include <QProcess>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>

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

class Imprimir : public QDialog
{
    Q_OBJECT

public:
    QSqlDatabase db;
    QSqlQueryModel *sql_query_model;
    bool is_recibo, is_complete_invoice;
    Imprimir(QWidget *parent = nullptr);

    // Public functions
    void get_ticket_info();
    void create_ticket_excel(bool copy_for_client, bool add_payed_info);
    void print_ticket();

    // Setup Dialog in code
    QFormLayout *formLayout;
    QLabel *lbl_n_ticket;
    QLineEdit *le_n_ticket;
    QDialogButtonBox *bb_ok_cancel;

private slots:
    void setupUi(QDialog *Imprimir);
    bool check_ticket_paid(int row);
    bool check_any_item_paid();
    QString add_extra_info_to_invoice(QString title, QString request);
    void on_bb_ok_cancel_accepted();
    void on_bb_ok_cancel_rejected();

private:

};

#endif // IMPRIMIR_H
