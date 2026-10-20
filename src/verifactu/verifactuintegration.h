#ifndef VERIFACTUINTEGRATION_H
#define VERIFACTUINTEGRATION_H

#include <QString>
#include <QDate>
#include <QPixmap>
#include <QSqlDatabase>
#include <functional>
#include "verifactumanager.h"

class AeatDirectBackend;

// High-level async facade over the VerifactuBackend chosen in Configuración - the
// IreneSolutions gateway (VerifactuManager) or the direct AEAT client
// (AeatDirectBackend): submit/cancel/QR operations return a request ID immediately;
// results arrive via the requestFinished signal.
class VerifactuIntegration : public QObject
{
    Q_OBJECT

public:
    explicit VerifactuIntegration(QObject *parent = nullptr);
    ~VerifactuIntegration();

    // `db` holds the direct client's records; the app's default connection when not given.
    bool initialize(const QSqlDatabase &db = QSqlDatabase());

    // Async API. Returns a unique request ID; the caller correlates it with the
    // requestFinished signal to pick up its own result. Empty string means the
    // call was rejected synchronously (Verifactu not configured) - no signal will fire.
    QString submitSimplifiedInvoiceAsync(
        const QString &invoiceNumber,
        const QDate &invoiceDate,
        double taxBase,
        double taxRate,
        const QString &description = QString()
    );

    QString cancelInvoiceAsync(
        const QString &invoiceNumber,
        const QDate &invoiceDate
    );

    // Async submit of a rectificativa (R1-R5). Builds an F2-style simplified invoice
    // (La Ideal only issues simplified) with R1-R5 type + RectificationType S/I.
    //
    // For BY_DIFFERENCES (I): correctedTaxBase carries the DELTA (signed) and
    //   originalTaxBase/Amount are ignored.
    // For BY_SUBSTITUTION (S): correctedTaxBase/Amount carry the new totals (TaxItems),
    //   originalTaxBase/Amount are emitted as RectificationTaxBase/RectificationTaxAmount.
    //
    // The new invoice gets its own unique InvoiceID (newInvoiceNumber). The original
    // (rectifiedInvoiceNumber / Date) is sent as FacturasRectificadas by the direct
    // client; the gateway request does not carry it (no RectificationItems are sent).
    QString submitRectificationAsync(
        const QString &newInvoiceNumber,
        const QDate &invoiceDate,
        VerifactuInvoice::InvoiceType invoiceType,
        VerifactuInvoice::RectificationType rectificationType,
        double correctedTaxBase,
        double correctedTaxAmount,
        double originalTaxBase,
        double originalTaxAmount,
        double taxRate,
        const QString &description = QString(),
        // The invoice it corrects (FacturasRectificadas); the direct client sends it.
        const QString &rectifiedInvoiceNumber = QString(),
        const QDate &rectifiedInvoiceDate = QDate()
    );

    QString generateQRAsync(
        const QString &invoiceNumber,
        const QDate &invoiceDate,
        double taxBase,
        double taxRate,
        const QString &description = QString()
    );

    // The QR of the direct client's queued record for this invoice (call it after the submit,
    // same inputs), printable before AEAT answers; null otherwise and with the gateway.
    QPixmap qrBeforeReply(const QString &invoiceNumber, const QDate &invoiceDate, double taxBase, double taxRate) const;

    // Asks AEAT what it holds for an InvoiceID. Answers on queryFinished.
    // Read-only: it never changes anything at AEAT. Empty return = not configured.
    QString queryInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate = QDate());

    bool isConfigured() const;
    QString getLastError() const { return m_lastError; }
    // The issuer's highest ticket number at the real AEAT (direct client in production, after
    // its sync), else 0: pre-production never moves the shop's numbering.
    int highestNumberAtAeat() const;
    // The pre-production self-test may touch the shop's chain (marker, records): see the module doc.
    static bool selfTestSharesShopChain(bool directAeat, bool aeatDirectProduction) { return directAeat && !aeatDirectProduction; }
    // Why no number may be taken now (first chain check running or failed: a failed one is retried).
    QString numberingBlockedReason();
    // Why no invoice may be issued now: numbering blocked, or Verifactu not configured.
    QString invoicingBlockedReason();
    // The direct AEAT client when it is the chosen connection, else nullptr. Only for what
    // needs the client itself (settings self-test, AEAT export, tests); the app asks below.
    AeatDirectBackend *directBackend() const { return m_direct; }
    bool isDirect() const { return m_direct != nullptr; }

    // The direct client holds a registration / cancellation record for the invoice that is
    // generated but not answered (it goes out on its own). Always false with the gateway.
    bool holdsPendingRegistration(const QString &invoiceNumber) const;
    bool holdsPendingCancellation(const QString &invoiceNumber) const;
    // AEAT's stored answer to the invoice's latest registration; false while none or with the gateway.
    bool storedRegistrationOutcome(const QString &invoiceNumber, VerifactuResult *result) const;
    // "pruebas" / "produccion" for the direct client, empty with the gateway.
    QString environmentName() const;
    // Why the direct client cannot send now (its certificate), or empty.
    QString sendingError() const;
    QString certificateExpiryWarning() const;
    // Recomputes the direct client's stored chain; true (nothing to check) with the gateway.
    // On false `problem` is user-facing; `continuations` counts records linking to AEAT-only huellas.
    bool verifyStoredChain(QString *problem, int *continuations) const;
    // Syncs the direct client's chain now; no-op with the gateway.
    void syncDirectChain(const std::function<void(bool ok, const QString &message)> &done);
    // UI test builds: sends the direct client to a fake AEAT at this URL.
    static void setAeatEndpointOverride(const QString &url);

signals:
    void requestFinished(const QString &requestId, const VerifactuResult &result);
    void queryFinished(const QString &requestId, const VerifactuRemoteRecord &record);
    void recordSettled(const QString &invoiceId, bool cancellation, const VerifactuResult &result);
    void chainChecked(bool ok);                 // every finished chain sync of the direct client

private:
    VerifactuBackend  *m_backend = nullptr;
    VerifactuManager  *m_manager = nullptr;   // when the gateway is the connection
    AeatDirectBackend *m_direct = nullptr;    // when AEAT is reached directly
    QString m_emitterNif;
    QString m_emitterName;
    QString m_lastError;

    bool loadEmitterConfiguration();
    VerifactuInvoice simplifiedInvoice(const QString &invoiceNumber, const QDate &invoiceDate, double taxBase,
                                       double taxRate, const QString &description) const;
};

#endif // VERIFACTUINTEGRATION_H
