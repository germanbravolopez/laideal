#ifndef CANCELINVOICEDIALOG_H
#define CANCELINVOICEDIALOG_H

#include <QDate>
#include <QDialog>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "verifactuintegration.h"

class QLineEdit;
class QLabel;
class QPushButton;
class QTableWidget;

// Cancellation dialog. Per-event scope: a partial-pay ticket (rows of distinct
// verifactu_invoice_seq, each its own AEAT submission) shows one row per (n_recibo, seq)
// pair with an individual "Anular" button; a ticket with seq=0 only shows one row. Each
// cancellation sends to AEAT under the literal invoice_id stored at submit time and updates
// only the rows of that seq.
class CancelInvoiceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CancelInvoiceDialog(const QSqlDatabase &database, QWidget *parent = nullptr);

    VerifactuIntegration *m_verifactu = nullptr;

protected:
    // Esc, the window's X and Cerrar all end here: refused while AEAT has not answered,
    // or the outcome of an invoice AEAT may already hold would be lost.
    void done(int result) override;

private slots:
    void onSearchClicked();
    void onCancelClicked(int row);
    void onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result);

private:
    // The first press of a row's Anular says what will be cancelled; the second sends it.
    void armCancellation(int row);
    // Shows `ticketNum` as the search of its number does.
    void loadTicket(const QString &ticketNum);
    // An invoice from before production: cancelled in the books, nothing sent to AEAT.
    void cancelLocally(int row);
    void disarmCancellation();

    struct Event {
        int     seq = 0;
        QString invoiceId;   // literal AEAT InvoiceID (bare n_recibo or "<n>-<seq>")
        double  importe = 0.0;
        QString csv;
        QString estado;
        QDate   invoiceDate; // fecha_pago: the date AEAT registered the invoice under
        QString qrUrl;       // verifactu_url_qr: its host says which AEAT registered it
        bool    qrUrlRead = false;
    };

    QLineEdit    *m_leTicketNum = nullptr;
    QPushButton  *m_btnSearch = nullptr;
    QLabel       *m_lblHeader = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel       *m_lblResult = nullptr;
    int           m_armedRow = -1;          // row whose button now reads "Confirmar anulación"

    QString          m_loadedTicket;
    QDate            m_receptionDate;       // fallback for old seq-0 invoices registered under it
    QVector<Event>   m_events;
    QString          m_pendingCancelId;
    QDate            m_pendingCancelDate;   // the date the closed-quarter guard checked
    QDate            m_pendingFallbackDate; // set while a reception-date retry is still possible
    QString          m_firstRejection;      // AEAT's answer to the first attempt, kept for the operator
    int              m_pendingCancelRow = -1; // row index in m_events for the in-flight cancel
    // The ticket and invoice the in-flight cancellation was sent for: the reply applies to
    // them, whatever the window shows by then.
    QString          m_pendingTicket;
    Event            m_pendingEvent;
    QSqlDatabase     db;

    void buildUi();
    void rebuildTable();
    int actionColumnWidth() const;
    void fitTableWidth();
    void setActionsEnabled(bool enabled);
    bool canCancel(const Event &e) const;
};

#endif // CANCELINVOICEDIALOG_H
