#ifndef VERIFACTUBACKEND_H
#define VERIFACTUBACKEND_H

// What VerifactuIntegration needs from whatever talks to AEAT: the IreneSolutions
// gateway (VerifactuManager) or the direct AEAT client (AeatDirectBackend), chosen
// in Configuración. Every call returns a request ID at once; the outcome arrives on
// requestFinished (submit / cancel / QR) or queryFinished (query), always queued, so
// callers handle both backends the same way.

#include <QDate>
#include <QObject>
#include <QString>

#include "verifactuinvoice.h"
#include "verifactutypes.h"

class VerifactuBackend : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~VerifactuBackend() override = default;

    virtual QString submitInvoiceAsync(const VerifactuInvoice &invoice) = 0;
    virtual QString cancelInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate) = 0;
    virtual QString generateQRAsync(const VerifactuInvoice &invoice) = 0;
    // `invoiceDate`: the invoice's issue date when known (AEAT files records by month).
    virtual QString queryInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate = QDate()) = 0;

    virtual bool isConfigured() const = 0;
    virtual QString configurationInfo() const = 0;

signals:
    void requestFinished(const QString &requestId, const VerifactuResult &result);
    void queryFinished(const QString &requestId, const VerifactuRemoteRecord &record);
    // A definitive AEAT outcome nobody waited for (records sent at start or retried
    // on their own). `invoiceId` is the AEAT InvoiceID. Only the direct client emits it.
    void recordSettled(const QString &invoiceId, bool cancellation, const VerifactuResult &result);
};

#endif // VERIFACTUBACKEND_H
