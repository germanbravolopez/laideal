#ifndef PENDINGSUBMITSDIALOG_H
#define PENDINGSUBMITSDIALOG_H

#include <QDate>
#include <QDialog>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

class QTableWidget;
class QPushButton;

// Startup recovery for the durability gap left by the async Verifactu submit
// path: a row stays at verifactu_estado='PENDIENTE' when the app is closed or
// crashes mid-submission, with no automatic recovery (the in-memory
// MainWindow::m_pendingSubmits map is gone after the restart).
//
// Strategy: surface every still-PENDIENTE n_recibo at app start, let the
// operator decide per ticket whether to Reintentar (re-fire the AEAT submit),
// Marcar como error (set verifactu_estado='Error' + error explanation), or
// Posponer (no-op, surfaces again next launch). No blind auto-resubmit -
// AEAT may already hold a registered invoice under the same InvoiceID and a
// naive resubmit would get a duplicate-InvoiceID rejection.
class PendingSubmitsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PendingSubmitsDialog(QSqlDatabase &db, QWidget *parent = nullptr);

    struct Entry {
        QString ticketNum;
        QDate   fechaRecepcion;
        QString client;
        double  importe = 0.0;
    };

    // Loads all distinct n_recibo with at least one PENDIENTE row.
    // Returns false (and does not populate the UI) when there are none.
    bool loadPending();

signals:
    // Emitted when the operator presses Reintentar. The receiver (MainWindow)
    // re-fires verifactuSubmitInvoice() with these args, which registers the
    // reqId in m_pendingSubmits so onVerifactuRequestFinished patches the
    // row on AEAT reply.
    void retryRequested(const QString &ticketNum, const QDate &invoiceDate, double totalAmount);

private slots:
    void onRetryClicked(int row);
    void onMarkErrorClicked(int row);
    void onPostponeClicked(int row);

private:
    QSqlDatabase   &db;
    QTableWidget   *m_table = nullptr;
    QVector<Entry>  m_entries;

    void buildUi();
    void populateRow(int row, const Entry &e);
    void removeRowAt(int row);
};

#endif // PENDINGSUBMITSDIALOG_H
