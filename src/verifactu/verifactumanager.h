#ifndef VERIFACTUMANAGER_H
#define VERIFACTUMANAGER_H

#include <QString>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "verifactuconfig.h"
#include "verifactuinvoice.h"
#include "verifactutypes.h"

// Async REST client for the Verifactu AEAT API. All invoice operations return
// immediately with a request ID; the result arrives via the requestFinished signal
// so the UI never blocks on AEAT latency.
class VerifactuManager : public QObject
{
    Q_OBJECT

public:
    explicit VerifactuManager(const QString &configPath = QString(), QObject *parent = nullptr);
    ~VerifactuManager();

    VerifactuConfig *getConfig() const { return m_config; }

    // Async API. Each call returns a unique request ID; the caller correlates it with
    // the requestFinished signal to pick up its own result. Validation errors and
    // invalid-config cases also arrive via the signal (queued) so callers have a
    // uniform handling path.
    QString submitInvoiceAsync(const VerifactuInvoice &invoice);
    QString cancelInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate);
    QString generateQRAsync(const VerifactuInvoice &invoice);

    QString getValidationUrl(const VerifactuInvoice &invoice) const;
    QString getLastError() const { return m_lastError; }
    QString getConfigurationInfo() const;

    // Synchronous diagnostic (interactive button in SettingsDialog). POSTs a stub to
    // /Create with a 10s setTransferTimeout - bounded wait, never blocks indefinitely.
    VerifactuResult testConnection();

signals:
    void requestFinished(const QString &requestId, const VerifactuResult &result);

private:
    VerifactuConfig *m_config;
    QNetworkAccessManager *m_networkManager;
    QString m_lastError;
    int m_requestCounter;

    QString nextRequestId();
    QNetworkRequest createNetworkRequest(const QString &endpoint) const;
    void logResponse(const QByteArray &response) const;
    VerifactuResult processResponse(const QByteArray &response, bool isQrRequest = false);
    bool validateConfiguration();
    QPixmap decodeImageFromBase64(const QString &data) const;

    // Queues an error so callers see it via requestFinished, same as a real reply.
    void emitErrorAsync(const QString &reqId, VerifactuResult::Status status, const QString &description);
    // Connects reply finished to a lambda that processes the response and emits requestFinished.
    void wireReply(const QString &reqId, QNetworkReply *reply, bool isQrRequest);
};

#endif // VERIFACTUMANAGER_H
