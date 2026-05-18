#include "verifactumanager.h"
#include <QNetworkRequest>
#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDebug>
#include <QUrlQuery>
#include <QByteArray>
#include <QBuffer>
#include <QEventLoop>

VerifactuManager::VerifactuManager(const QString &configPath, QObject *parent)
    : QObject(parent), m_config(nullptr), m_networkManager(nullptr)
{
    m_config = new VerifactuConfig(configPath);
    m_networkManager = new QNetworkAccessManager(this);
}

VerifactuManager::~VerifactuManager()
{
}

VerifactuResult VerifactuManager::submitInvoice(const VerifactuInvoice &invoice)
{
    VerifactuResult result;

    if (!validateConfiguration()) {
        result.status = VerifactuResult::INVALID_CONFIG;
        result.errorDescription = getLastError();
        return result;
    }

    if (!invoice.isValid()) {
        result.status = VerifactuResult::ERROR;
        result.errorDescription = invoice.getValidationError();
        return result;
    }

    QJsonObject invoiceJson = invoice.toJson();
    invoiceJson["ServiceKey"] = m_config->getServiceKey();

    QJsonDocument doc(invoiceJson);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest request = createNetworkRequest(m_config->getEndpointUrl() + "/Create");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qDebug() << "Submitting invoice" << invoice.getInvoiceNumber() << "to Verifactu";
    qDebug().noquote() << "Payload:" << QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    qDebug() << "Endpoint:" << (m_config->getEndpointUrl() + "/Create");

    QNetworkReply *reply = m_networkManager->post(request, payload);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.status = VerifactuResult::NETWORK_ERROR;
        result.errorDescription = "Error de conexión: " + reply->errorString();
        m_lastError = result.errorDescription;
        qWarning() << "Network error:" << m_lastError;
        reply->deleteLater();
        return result;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    qDebug().noquote() << "Server response:" << QString::fromUtf8(responseData);
    result = processResponse(responseData, false);
    return result;
}

VerifactuResult VerifactuManager::submitInvoices(const QList<VerifactuInvoice> &invoices)
{
    VerifactuResult result;

    if (!validateConfiguration()) {
        result.status = VerifactuResult::INVALID_CONFIG;
        result.errorDescription = getLastError();
        return result;
    }

    if (invoices.isEmpty()) {
        result.status = VerifactuResult::ERROR;
        result.errorDescription = "La lista de facturas no puede estar vacía";
        return result;
    }

    for (const auto &invoice : invoices) {
        if (!invoice.isValid()) {
            result.status = VerifactuResult::ERROR;
            result.errorDescription = "Factura inválida: " + invoice.getValidationError();
            return result;
        }
    }

    QJsonObject batchJson;
    QJsonArray invoicesArray;

    for (const auto &invoice : invoices) {
        invoicesArray.append(invoice.toJson());
    }

    batchJson["Invoices"] = invoicesArray;
    batchJson["ServiceKey"] = m_config->getServiceKey();

    QJsonDocument doc(batchJson);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest request = createNetworkRequest(m_config->getEndpointUrl() + "/CreateBatch");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qDebug() << "Submitting batch of" << invoices.count() << "invoices to Verifactu";

    QNetworkReply *reply = m_networkManager->post(request, payload);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.status = VerifactuResult::NETWORK_ERROR;
        result.errorDescription = "Error de conexión: " + reply->errorString();
        qWarning() << "Batch network error:" << result.errorDescription;
        reply->deleteLater();
        return result;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    result = processResponse(responseData, false);
    return result;
}

VerifactuResult VerifactuManager::cancelInvoice(const QString &invoiceNumber, const QDate &invoiceDate, const QString &sellerNIF)
{
    VerifactuResult result;

    if (!validateConfiguration()) {
        result.status = VerifactuResult::INVALID_CONFIG;
        result.errorDescription = getLastError();
        return result;
    }

    if (invoiceNumber.isEmpty() || invoiceDate.isNull() || sellerNIF.isEmpty()) {
        result.status = VerifactuResult::ERROR;
        result.errorDescription = "Datos de anulación incompletos";
        return result;
    }

    QJsonObject cancelJson;
    cancelJson["InvoiceID"] = invoiceNumber;
    cancelJson["InvoiceDate"] = invoiceDate.toString(Qt::ISODate);
    cancelJson["SellerID"] = sellerNIF;
    cancelJson["ServiceKey"] = m_config->getServiceKey();

    QJsonDocument doc(cancelJson);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest request = createNetworkRequest(m_config->getEndpointUrl() + "/Cancel");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = m_networkManager->post(request, payload);

    qDebug() << "Cancelling invoice" << invoiceNumber;

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.status = VerifactuResult::NETWORK_ERROR;
        result.errorDescription = "Error de conexión: " + reply->errorString();
        qWarning() << "Cancel network error:" << result.errorDescription;
        reply->deleteLater();
        return result;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    result = processResponse(responseData, false);
    return result;
}

VerifactuResult VerifactuManager::generateQRCode(const VerifactuInvoice &invoice)
{
    VerifactuResult result;

    if (!validateConfiguration()) {
        result.status = VerifactuResult::INVALID_CONFIG;
        result.errorDescription = getLastError();
        return result;
    }

    if (!invoice.isValid()) {
        result.status = VerifactuResult::ERROR;
        result.errorDescription = invoice.getValidationError();
        return result;
    }

    QJsonObject qrJson = invoice.toJson();
    qrJson["ServiceKey"] = m_config->getServiceKey();

    QJsonDocument doc(qrJson);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest request = createNetworkRequest(m_config->getQrUrl());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = m_networkManager->post(request, payload);

    qDebug() << "Generating QR for invoice" << invoice.getInvoiceNumber();

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.status = VerifactuResult::NETWORK_ERROR;
        result.errorDescription = "Error de conexión: " + reply->errorString();
        qWarning() << "QR network error:" << result.errorDescription;
        reply->deleteLater();
        return result;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    result = processResponse(responseData, true);
    result.validationUrl = getValidationUrl(invoice);
    return result;
}

QString VerifactuManager::getValidationUrl(const VerifactuInvoice &invoice) const
{
    QString url = m_config->getValidationUrl();
    QUrlQuery query;

    query.addQueryItem("nif", invoice.getSellerNIF());
    query.addQueryItem("numserie", invoice.getInvoiceNumber());
    query.addQueryItem("fecha", invoice.getInvoiceDate().toString("dd-MM-yyyy"));
    query.addQueryItem("importe", QString::number(invoice.getTotalAmount(), 'f', 2));

    url += "?" + query.toString();
    return url;
}

bool VerifactuManager::testConnection()
{
    if (!validateConfiguration()) {
        return false;
    }

    QNetworkRequest request = createNetworkRequest(m_config->getEndpointUrl());
    QNetworkReply *reply = m_networkManager->get(request);

    qDebug() << "Testing Verifactu connection...";
    return true;
}

QString VerifactuManager::getConfigurationInfo() const
{
    QString info = "=== Verifactu Configuration ===\n";
    info += "Environment: " + QString(m_config->getEnvironment() == VerifactuConfig::TESTING ? "TESTING" : "PRODUCTION") + "\n";
    info += "Emitter NIF: " + m_config->getEmitterNIF() + "\n";
    info += "Emitter Name: " + m_config->getEmitterName() + "\n";
    info += "System: " + m_config->getSystemName() + " v" + m_config->getSystemVersion() + "\n";
    info += "Endpoint: " + m_config->getEndpointUrl() + "\n";
    info += "Configured: " + QString(m_config->isValid() ? "Yes" : "No") + "\n";

    if (!m_config->isValid())
        info += "Error: " + m_config->getValidationError() + "\n";

    return info;
}

QNetworkRequest VerifactuManager::createNetworkRequest(const QString &endpoint) const
{
    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "LAIDEAL-Verifactu/7.1");
    return request;
}

VerifactuResult VerifactuManager::processResponse(const QByteArray &response, bool isQrRequest)
{
    VerifactuResult result;

    QJsonDocument doc = QJsonDocument::fromJson(response);

    if (!doc.isObject()) {
        result.status = VerifactuResult::ERROR;
        result.errorDescription = "Respuesta inválida del servidor";
        m_lastResult = result;
        return result;
    }

    QJsonObject obj = doc.object();
    result.rawResponse = QString::fromUtf8(response);

    int resultCode = obj.value("ResultCode").toInt(-1);

    if (resultCode == 0) {
        result.status = VerifactuResult::SUCCESS;

        if (isQrRequest && obj.contains("Return")) {
            result.qrCode = decodeImageFromBase64(obj.value("Return").toString());
        } else if (obj.contains("Return")) {
            result.csv = obj.value("Return").toObject().value("CSV").toString();
        }
    } else {
        result.status = VerifactuResult::ERROR;
        result.errorCode = QString::number(resultCode);
        result.errorDescription = obj.value("ResultMessage").toString("Error desconocido");

        if (obj.contains("Return")) {
            result.errorDescription = obj.value("Return").toObject()
                .value("ErrorDescription").toString(result.errorDescription);
        }
    }

    m_lastResult = result;
    return result;
}

bool VerifactuManager::validateConfiguration()
{
    if (!m_config->isValid()) {
        m_lastError = m_config->getValidationError();
        return false;
    }
    return true;
}

QString VerifactuManager::encodeImageToBase64(const QPixmap &pixmap) const
{
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    return QString::fromLatin1(byteArray.toBase64());
}

QPixmap VerifactuManager::decodeImageFromBase64(const QString &data) const
{
    QByteArray byteArray = QByteArray::fromBase64(data.toLatin1());
    QPixmap pixmap;
    pixmap.loadFromData(byteArray);
    return pixmap;
}

void VerifactuManager::onNetworkReply(QNetworkReply *reply)
{
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        m_lastError = reply->errorString();
        qCritical() << "Network error:" << m_lastError;
    } else {
        QByteArray responseData = reply->readAll();
        m_lastResult = processResponse(responseData);
    }

    reply->deleteLater();
}
