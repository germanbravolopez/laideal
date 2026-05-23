#ifndef VERIFACTUMANAGER_H
#define VERIFACTUMANAGER_H

#include <QString>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include "verifactuconfig.h"
#include "verifactuinvoice.h"

// Persisted value of the verifactu_estado column in the ingresos table.
enum class VerifactuEstado {
    NotSubmitted,  // "PENDIENTE" — Verifactu not configured or not yet submitted
    Enviada,       // "ENVIADA"   — successfully submitted to AEAT
    Anulada,       // "ANULADA"   — cancelled via AEAT
    Error          // "ERROR"     — submission or cancellation failed
};

inline QString verifactuEstadoToString(VerifactuEstado e)
{
    switch (e) {
    case VerifactuEstado::NotSubmitted: return QStringLiteral("PENDIENTE");
    case VerifactuEstado::Enviada:      return QStringLiteral("ENVIADA");
    case VerifactuEstado::Anulada:      return QStringLiteral("ANULADA");
    case VerifactuEstado::Error:        return QStringLiteral("ERROR");
    }
    return QStringLiteral("PENDIENTE");
}

inline VerifactuEstado verifactuEstadoFromString(const QString &s)
{
    if (s == QLatin1String("ENVIADA"))   return VerifactuEstado::Enviada;
    if (s == QLatin1String("ANULADA"))   return VerifactuEstado::Anulada;
    if (s == QLatin1String("ERROR"))     return VerifactuEstado::Error;
    return VerifactuEstado::NotSubmitted; // covers "PENDIENTE" and legacy empty/NULL
}

struct VerifactuResult
{
    enum Status {
        SUCCESS,
        PENDING,
        ERROR,
        NETWORK_ERROR,
        INVALID_CONFIG
    };

    Status status;
    QString csv;
    QString errorCode;
    QString errorDescription;
    QString validationUrl;
    QPixmap qrCode;
    QString rawResponse;

    VerifactuResult() : status(INVALID_CONFIG) {}

    bool isSuccess() const { return status == SUCCESS; }
    bool isError() const { return status == ERROR || status == NETWORK_ERROR || status == INVALID_CONFIG; }
};

// Handles REST communication with the Verifactu AEAT API.
class VerifactuManager : public QObject
{
    Q_OBJECT

public:
    explicit VerifactuManager(const QString &configPath = QString(), QObject *parent = nullptr);
    ~VerifactuManager();

    VerifactuConfig *getConfig() const { return m_config; }

    VerifactuResult submitInvoice(const VerifactuInvoice &invoice);
    VerifactuResult submitInvoices(const QList<VerifactuInvoice> &invoices);
    VerifactuResult cancelInvoice(const QString &invoiceNumber, const QDate &invoiceDate, const QString &sellerNIF);
    VerifactuResult generateQRCode(const VerifactuInvoice &invoice);
    QString getValidationUrl(const VerifactuInvoice &invoice) const;

    QString getLastError() const { return m_lastError; }
    bool testConnection();
    QString getConfigurationInfo() const;

private slots:
    void onNetworkReply(QNetworkReply *reply);

private:
    VerifactuConfig *m_config;
    QNetworkAccessManager *m_networkManager;
    QString m_lastError;
    VerifactuResult m_lastResult;

    QNetworkRequest createNetworkRequest(const QString &endpoint) const;
    void logResponse(const QByteArray &response) const;
    VerifactuResult processResponse(const QByteArray &response, bool isQrRequest = false);
    bool validateConfiguration();
    QString encodeImageToBase64(const QPixmap &pixmap) const;
    QPixmap decodeImageFromBase64(const QString &data) const;
};

#endif // VERIFACTUMANAGER_H
