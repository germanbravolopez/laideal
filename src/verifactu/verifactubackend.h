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
    virtual QString queryInvoiceAsync(const QString &invoiceNumber) = 0;

    virtual bool isConfigured() const = 0;
    virtual QString configurationInfo() const = 0;

signals:
    void requestFinished(const QString &requestId, const VerifactuResult &result);
    void queryFinished(const QString &requestId, const VerifactuRemoteRecord &record);
};

#endif // VERIFACTUBACKEND_H
