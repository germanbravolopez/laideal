#ifndef VERIFACTUCOORDINATOR_H
#define VERIFACTUCOORDINATOR_H

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSqlDatabase>
#include "verifactuintegration.h"

class QWidget;

// The window's Verifactu orchestration without any widget of the ticket form: owns the
// integration, the submissions waiting for AEAT's reply, the bounded wait after a save and the
// connection tests of Configuración. The window reacts to its signals (status bar, result
// panel, disabling the form during the wait). User-visible strings keep the MainWindow context.
class VerifactuCoordinator : public QObject
{
    Q_OBJECT

public:
    struct AeatReply { QPixmap qrCode; bool success = false; QString refusal; };
    struct Notice { QString text; bool visible = false; };

    VerifactuCoordinator(const QSqlDatabase &db, QObject *parent = nullptr);

    // Null until the first initialize().
    VerifactuIntegration *integration() const { return m_integration; }
    // Creates the integration on the first call (its signals are relayed from then on) and
    // initialises it; created, if given, tells whether this call created it.
    bool initialize(bool *created = nullptr);

    // Returns the in-flight reqId, empty if Verifactu is not configured or the submit was
    // rejected. seq 0 = save-time / full ticket (InvoiceID = bare n_recibo), >0 = a partial-pay
    // event being recovered (InvoiceID "<ticketNum>-<seq>").
    QString submitInvoice(const QString &ticketNum, const QDate &invoiceDate, double totalAmount, int seq = 0);
    // The QR of what submitInvoice sent when it can be printed before AEAT answers (direct
    // client); null with the gateway.
    QPixmap qrBeforeReply(const QString &ticketNum, const QDate &invoiceDate, double totalAmount, int seq = 0) const;
    // Waits (up to 3 s) for AEAT's reply to reqId; the QR of an accepted one.
    AeatReply waitForReply(const QString &reqId, const QString &ticketNum);
    bool isWaitingForAeat() const { return m_waitingForAeat; }
    // The wait expired and a QR-less recibo was printed: a late success says the factura is printable.
    void markPrintedWithoutQr(const QString &reqId);
    bool hasPendingSubmit(const QString &reqId) const { return m_pendingSubmits.contains(reqId); }

    // An AEAT refusal as panel HTML (empty for any other outcome).
    QString rejectionHtml(const QString &invoiceId, const VerifactuResult &result) const;
    // The status bar notices: the direct client's AEAT environment, the records left to send.
    Notice aeatEnvironmentNotice() const;
    // Its text: decided by the environment the client sends to, never by whether the start day is known.
    static QString aeatEnvironmentText(const QString &environmentName, const QDate &productionStart);
    Notice unsentRecordsNotice() const;

    // Configuración's tests. Each takes the dialog as parent; none touches the ticket form.
    QString checkAeatProduction(QObject *context, const QString &nif, const QString &name, const QString &thumbprint,
                                const QString &certificateFile, const QString &certificatePassword);
    void testGatewayConnection(QWidget *parent, const QString &nif, const QString &name,
                               const QString &serviceKey, bool production);
    void runAeatSelfTest(QWidget *parent, const QString &nif, const QString &name, const QString &thumbprint,
                         const QString &certificateFile, const QString &certificatePassword);
    void runAeatConnectionTest(QWidget *parent, const QString &nif, const QString &name, const QString &thumbprint,
                               const QString &certificateFile, const QString &certificatePassword, bool production);

signals:
    void statusMessage(const QString &text, int timeoutMs);
    // A submit's reply was refused: show it (it can come after the factura was printed).
    void submitRefused(const QString &invoiceId, const VerifactuResult &result);
    // A submit's reply was handled: the count of refused invoices may have changed.
    void submitSettled();
    // The wait inside waitForReply: the window disables the form meanwhile.
    void aeatWaitStarted();
    void aeatWaitFinished();
    // The integration's (or the self-test's) outcomes, relayed.
    void recordSettled(const QString &invoiceId, bool cancellation, const VerifactuResult &result);
    void unsentRecordsChanged();
    void chainChecked(bool ok);

private:
    // Async submit tracking: reqId -> (ticket number, verifactu_invoice_seq), so the reply
    // patches exactly the rows of that submission event. printedWithoutQr: see markPrintedWithoutQr.
    struct PendingSubmit { QString ticketNum; int seq = 0; bool printedWithoutQr = false; };

    void onRequestFinished(const QString &requestId, const VerifactuResult &result);

    QSqlDatabase m_db;
    VerifactuIntegration *m_integration = nullptr;
    bool m_waitingForAeat = false;   // inside waitForReply's nested loop
    QHash<QString, PendingSubmit> m_pendingSubmits;
};

#endif // VERIFACTUCOORDINATOR_H
