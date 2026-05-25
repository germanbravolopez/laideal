#ifndef VERIFACTUINTEGRATION_H
#define VERIFACTUINTEGRATION_H

#include <QString>
#include <QDate>
#include "verifactumanager.h"

// High-level async facade over VerifactuManager: submit/cancel/QR operations return
// a request ID immediately; results arrive via the requestFinished signal.
class VerifactuIntegration : public QObject
{
    Q_OBJECT

public:
    explicit VerifactuIntegration(QObject *parent = nullptr);
    ~VerifactuIntegration();

    bool initialize();

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

    QString generateQRAsync(
        const QString &invoiceNumber,
        const QDate &invoiceDate,
        double taxBase,
        double taxRate,
        const QString &description = QString()
    );

    bool isConfigured() const;
    QString getLastError() const { return m_lastError; }

signals:
    void requestFinished(const QString &requestId, const VerifactuResult &result);

private:
    VerifactuManager *m_manager;
    QString m_lastError;

    bool loadEmitterConfiguration();
};

#endif // VERIFACTUINTEGRATION_H
