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
#include <QPixmap>

#include "sql_lite.h"
#include "verifactuintegration.h"

// `ingresos` column indices come from sql_lite.h (INGRESOS_COL_*).

class Imprimir : public QDialog
{
    Q_OBJECT

public:
    QSqlDatabase db;
    QSqlQueryModel *sqlQueryModel;
    bool isRecibo, isCompleteInvoice;
    VerifactuIntegration *verifactuIntegration = nullptr;
    QPixmap qrCode;
    Imprimir(QWidget *parent = nullptr);

    // Public functions
    void getTicketInfo();
    void createTicketExcel(bool copyForClient, bool addPayedInfo);
    void printTicket();
    QPixmap resolveQrCode();

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
