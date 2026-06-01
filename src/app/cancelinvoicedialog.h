#ifndef CANCELINVOICEDIALOG_H
#define CANCELINVOICEDIALOG_H

#include <QDate>
#include <QDialog>
#include <QHash>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "verifactuintegration.h"

class QLineEdit;
class QLabel;
class QPushButton;
class QTableWidget;

// Cancellation dialog. 8.6+: per-event scope so a partial-pay ticket (multiple
// rows of distinct verifactu_invoice_seq, each its own AEAT submission) shows
// one row per (n_recibo, seq) pair with an individual "Anular" button. A
// single-event legacy ticket (seq=0 only) renders the same uniform table with
// one row. Each cancellation sends to AEAT under the literal invoice_id stored
// at submit time and updates only the rows of that seq.
class CancelInvoiceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CancelInvoiceDialog(const QSqlDatabase &database, QWidget *parent = nullptr);

    VerifactuIntegration *m_verifactu = nullptr;

private slots:
    void onSearchClicked();
    void onCancelClicked(int row);
    void onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result);

private:
    struct Event {
        int     seq = 0;
        QString invoiceId;   // literal AEAT InvoiceID (bare n_recibo or "<n>-<seq>")
        double  importe = 0.0;
        QString csv;
        QString estado;
    };

    QLineEdit    *m_leTicketNum;
    QLabel       *m_lblHeader;
    QTableWidget *m_table;
    QLabel       *m_lblResult;

    QString          m_loadedTicket;
    QDate            m_loadedDate;
    QVector<Event>   m_events;
    QString          m_pendingCancelId;
    int              m_pendingCancelRow = -1; // row index in m_events for the in-flight cancel
    QSqlDatabase     db;

    void buildUi();
    void rebuildTable();
    void setActionsEnabled(bool enabled);
};

#endif // CANCELINVOICEDIALOG_H
