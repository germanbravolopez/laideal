#ifndef PAY_DIALOG_H
#define PAY_DIALOG_H

#include <QDate>
#include <QDialog>
#include <QSqlDatabase>
#include <QStringList>

#include "verifactutypes.h"

class QDateEdit;
class QLabel;
class QPushButton;
class QTableWidget;
class VerifactuIntegration;

// Partial-payment dialog. Operator picks one or more unpaid rows of a single
// ticket; on Cobrar we submit a Verifactu invoice for that subset with
// InvoiceID = "<n_recibo>-<seq>" (seq via nextVerifactuInvoiceSeq), mark the
// chosen rows pagado=SI with the matching seq, and print the partial factura.
// Remaining unpaid rows stay unpaid and can be charged in a later event with
// the next seq.
class PayDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PayDialog(QWidget *parent = nullptr);

    QSqlDatabase db;
    VerifactuIntegration *m_verifactu = nullptr;

    // Loads the ticket's unpaid rows into the table widget. Returns false if
    // the ticket is not found or every row is already paid (caller should
    // not show the dialog in that case).
    bool loadTicket(const QString &ticketNum);

private slots:
    void onCobrarClicked();
    void onSelectionChanged();
    void onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result);

private:
    void buildUi();
    void setFormEnabled(bool enabled);
    void recomputeTotal();
    void persistPayment(int seq, const VerifactuResult &result);
    void printPartialFactura(int seq, const QPixmap &qrCode);

    QString m_ticketNum;
    QString m_cliente;
    QDate   m_fechaRecepcion;

    QTableWidget *m_table       = nullptr;
    QLabel       *m_lblHeader   = nullptr;
    QLabel       *m_lblTotal    = nullptr;
    QDateEdit    *m_dePago      = nullptr;
    QPushButton  *m_btnCobrar   = nullptr;
    QPushButton  *m_btnCancel   = nullptr;
    QLabel       *m_lblStatus   = nullptr;

    // In-flight submission state - non-empty only between submit and reply.
    QString     m_pendingReqId;
    int         m_pendingSeq = -1;
    QStringList m_pendingHashes;
    QDate       m_pendingFechaPago;
};

#endif // PAY_DIALOG_H
