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
    QSqlQueryModel *sqlQueryModel;
    bool isRecibo, isCompleteInvoice;
    VerifactuIntegration *verifactuIntegration = nullptr;
    QPixmap qrCode;
    // -1 (default): print every row of n_recibo (legacy / full-ticket flow).
    // >=0: scope getTicketInfo to rows with verifactu_invoice_seq = invoiceSeq,
    // so partial-payment events (8.5+) print only the garments charged that time.
    int invoiceSeq = -1;
    Imprimir(const QSqlDatabase &database, QWidget *parent = nullptr);

    // Public functions
    void getTicketInfo();
    void createTicketExcel(bool copyForClient, bool addPayedInfo);
    void printTicket();
    QPixmap resolveQrCode();
    // Literal AEAT InvoiceID for the loaded rows: first non-empty
    // verifactu_invoice_id, else bare n_recibo (legacy / never submitted).
    QString displayInvoiceId() const;

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
    QSqlDatabase db;
};

#endif // IMPRIMIR_H
