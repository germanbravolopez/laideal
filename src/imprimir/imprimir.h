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
#define TABLE_HASH      14

class Imprimir : public QDialog
{
    Q_OBJECT

public:
    QSqlDatabase db;
    QSqlQueryModel *sqlQueryModel;
    bool isRecibo, isCompleteInvoice;
    Imprimir(QWidget *parent = nullptr);

    // Public functions
    void getTicketInfo();
    void createTicketExcel(bool copyForClient, bool addPayedInfo);
    void printTicket();

    // Setup Dialog in code
    QFormLayout *formLayout;
    QLabel *lbl_n_ticket;
    QLineEdit *le_n_ticket;
    QDialogButtonBox *bb_ok_cancel;

private slots:
    void setupUi(QDialog *Imprimir);
    bool checkTicketPaid(int row);
    bool checkAnyItemPaid();
    QString addExtraInfoToInvoice(QString title, QString request);
    void on_bb_ok_cancel_accepted();
    void on_bb_ok_cancel_rejected();

private:

};

#endif // IMPRIMIR_H
