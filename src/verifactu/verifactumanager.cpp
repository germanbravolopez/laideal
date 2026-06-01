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
#include <QEventLoop>
#include <QRegularExpression>
#include <QPointer>

VerifactuManager::VerifactuManager(QObject *parent)
    : QObject(parent), m_config(nullptr), m_networkManager(nullptr), m_requestCounter(0)
{
    m_config = new VerifactuConfig();
    m_networkManager = new QNetworkAccessManager(this);
}

VerifactuManager::~VerifactuManager()
{
}

QString VerifactuManager::nextRequestId()
{
    return QStringLiteral("req-%1").arg(++m_requestCounter);
}

void VerifactuManager::emitErrorAsync(const QString &reqId, VerifactuResult::Status status, const QString &description)
{
    VerifactuResult r;
    r.status = status;
    r.errorDescription = description;
    m_lastError = description;
    QMetaObject::invokeMethod(this, [this, reqId, r]() {
        emit requestFinished(reqId, r);
    }, Qt::QueuedConnection);
}

void VerifactuManager::wireReply(const QString &reqId, QNetworkReply *reply, bool isQrRequest)
{
    connect(reply, &QNetworkReply::finished, this, [this, reqId, reply, isQrRequest]() {
        VerifactuResult result;
        if (reply->error() != QNetworkReply::NoError) {
            result.status = VerifactuResult::NETWORK_ERROR;
            result.errorDescription = "Error de conexión: " + reply->errorString();
            m_lastError = result.errorDescription;
            qWarning() << "Verifactu network error (" << reqId << "):" << m_lastError;
        } else {
            QByteArray responseData = reply->readAll();
            logResponse(responseData);
            result = processResponse(responseData, isQrRequest);
        }
        reply->deleteLater();
        emit requestFinished(reqId, result);
    });
}

QString VerifactuManager::submitInvoiceAsync(const VerifactuInvoice &invoice)
{
    const QString reqId = nextRequestId();

    if (!validateConfiguration()) {
        emitErrorAsync(reqId, VerifactuResult::INVALID_CONFIG, getLastError());
        return reqId;
    }
    if (!invoice.isValid()) {
        emitErrorAsync(reqId, VerifactuResult::ERROR, invoice.getValidationError());
        return reqId;
    }

    QJsonObject invoiceJson = invoice.toJson();
    invoiceJson["ServiceKey"] = m_config->getServiceKey();
    QJsonDocument doc(invoiceJson);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest request = createNetworkRequest(m_config->getEndpointUrl() + "/Create");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qDebug() << "Submitting invoice" << invoice.getInvoiceNumber() << "(" << reqId << ")";
    // Redact ServiceKey before logging - the JSON ends up in ~/.laideal.log and the key
    // is the AEAT API credential.
    QJsonObject loggable = invoiceJson;
    loggable["ServiceKey"] = QStringLiteral("[redacted, %1 chars]")
        .arg(invoiceJson.value("ServiceKey").toString().length());
    qDebug().noquote() << "Payload:" << QString::fromUtf8(QJsonDocument(loggable).toJson(QJsonDocument::Indented));
    qDebug() << "Endpoint:" << (m_config->getEndpointUrl() + "/Create");

    QNetworkReply *reply = m_networkManager->post(request, payload);
    wireReply(reqId, reply, false);
    return reqId;
}

QString VerifactuManager::cancelInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate)
{
    const QString reqId = nextRequestId();

    if (!validateConfiguration()) {
        emitErrorAsync(reqId, VerifactuResult::INVALID_CONFIG, getLastError());
        return reqId;
    }
    if (invoiceNumber.isEmpty() || invoiceDate.isNull()) {
        emitErrorAsync(reqId, VerifactuResult::ERROR, "Datos de anulación incompletos");
        return reqId;
    }

    QJsonObject cancelJson;
    cancelJson["InvoiceID"]   = invoiceNumber;
    cancelJson["InvoiceDate"] = invoiceDate.toString(Qt::ISODate);
    cancelJson["SellerID"]    = m_config->getEmitterNIF();
    cancelJson["CompanyName"] = m_config->getEmitterName();
    cancelJson["ServiceKey"]  = m_config->getServiceKey();

    QJsonDocument doc(cancelJson);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest request = createNetworkRequest(m_config->getEndpointUrl() + "/Cancel");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qDebug() << "Cancelling invoice" << invoiceNumber << "(" << reqId << ")";

    QNetworkReply *reply = m_networkManager->post(request, payload);
    wireReply(reqId, reply, false);
    return reqId;
}

QString VerifactuManager::generateQRAsync(const VerifactuInvoice &invoice)
{
    const QString reqId = nextRequestId();

    if (!validateConfiguration()) {
        emitErrorAsync(reqId, VerifactuResult::INVALID_CONFIG, getLastError());
        return reqId;
    }
    if (!invoice.isValid()) {
        emitErrorAsync(reqId, VerifactuResult::ERROR, invoice.getValidationError());
        return reqId;
    }

    QJsonObject qrJson = invoice.toJson();
    qrJson["ServiceKey"] = m_config->getServiceKey();
    QJsonDocument doc(qrJson);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest request = createNetworkRequest(m_config->getQrUrl());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qDebug() << "Generating QR for invoice" << invoice.getInvoiceNumber() << "(" << reqId << ")";

    QNetworkReply *reply = m_networkManager->post(request, payload);

    // QR path also needs validationUrl assembled locally - use a dedicated lambda
    // instead of wireReply so we can stamp it before emitting.
    const QString validationUrl = getValidationUrl(invoice);
    connect(reply, &QNetworkReply::finished, this, [this, reqId, reply, validationUrl]() {
        VerifactuResult result;
        if (reply->error() != QNetworkReply::NoError) {
            result.status = VerifactuResult::NETWORK_ERROR;
            result.errorDescription = "Error de conexión: " + reply->errorString();
            m_lastError = result.errorDescription;
            qWarning() << "Verifactu QR network error (" << reqId << "):" << m_lastError;
        } else {
            QByteArray responseData = reply->readAll();
            logResponse(responseData);
            result = processResponse(responseData, true);
            result.validationUrl = validationUrl;
        }
        reply->deleteLater();
        emit requestFinished(reqId, result);
    });
    return reqId;
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

VerifactuResult VerifactuManager::testConnection()
{
    VerifactuResult result;

    if (!validateConfiguration()) {
        result.status = VerifactuResult::INVALID_CONFIG;
        result.errorDescription = getLastError();
        return result;
    }

    // POST a stub payload to /Create. The server rejects it (missing fields), but ANY
    // HTTP response proves connectivity + auth. GET on the base URL hangs without ever
    // responding, so POST to /Create is the only reliable probe.
    QJsonObject probe;
    probe["ServiceKey"] = m_config->getServiceKey();
    QByteArray payload = QJsonDocument(probe).toJson(QJsonDocument::Compact);

    QNetworkRequest request = createNetworkRequest(m_config->getEndpointUrl() + "/Create");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(10000);
    QPointer<QNetworkReply> reply = m_networkManager->post(request, payload);

    qDebug() << "Testing Verifactu connection to" << (m_config->getEndpointUrl() + "/Create");

    QEventLoop loop;
    connect(reply.data(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    // The dialog can be closed while the loop is running and the QNetworkAccessManager
    // can deleteLater the reply out from under us. QPointer keeps the access safe.
    if (reply.isNull()) {
        result.status = VerifactuResult::NETWORK_ERROR;
        result.errorDescription = "Sin respuesta (cancelada)";
        m_lastError = result.errorDescription;
        return result;
    }

    QVariant httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    QString errStr = reply->errorString();
    reply->deleteLater();

    if (!httpStatus.isValid()) {
        // No HTTP response - DNS failure, connection refused, or timeout
        result.status = VerifactuResult::NETWORK_ERROR;
        result.errorDescription = "Error de red: " + errStr;
        m_lastError = result.errorDescription;
        qWarning() << "testConnection: network error" << errStr;
        return result;
    }

    result.status = VerifactuResult::SUCCESS;
    result.errorDescription = QString("HTTP %1").arg(httpStatus.toInt());
    qDebug() << "testConnection: reachable, HTTP" << httpStatus.toInt();
    return result;
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
    request.setHeader(QNetworkRequest::UserAgentHeader, "LAIDEAL/" + QString(PROJECT_VERSION));
    return request;
}

void VerifactuManager::logResponse(const QByteArray &response) const
{
    QJsonDocument doc = QJsonDocument::fromJson(response);
    if (!doc.isObject()) {
        qDebug().noquote() << "Server response:" << QString::fromUtf8(response);
        return;
    }

    QJsonObject obj = doc.object();

    // Truncate the base64 BMP that floods the log. Two response shapes to handle:
    // - /Create:     Return is an object with a "QrCode" string field
    // - /GetQrCode:  Return is itself the base64 BMP string (no wrapping object)
    if (obj.contains("Return") && obj["Return"].isObject()) {
        QJsonObject ret = obj["Return"].toObject();
        if (ret.contains("QrCode") && !ret["QrCode"].isNull()) {
            int len = ret["QrCode"].toString().length();
            ret["QrCode"] = QString("[base64 BMP, %1 chars]").arg(len);
            obj["Return"] = ret;
        }
    } else if (obj.contains("Return") && obj["Return"].isString()) {
        int len = obj["Return"].toString().length();
        obj["Return"] = QString("[base64 BMP, %1 chars]").arg(len);
    }

    qDebug().noquote() << "Server response:\n" << QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

VerifactuResult VerifactuManager::processResponse(const QByteArray &response, bool isQrRequest)
{
    VerifactuResult result;

    QJsonDocument doc = QJsonDocument::fromJson(response);

    if (!doc.isObject()) {
        result.status = VerifactuResult::ERROR;
        result.errorDescription = "Respuesta inválida del servidor";
        return result;
    }

    QJsonObject obj = doc.object();
    result.rawResponse = QString::fromUtf8(response);

    int resultCode = obj.value("ResultCode").toInt(-1);

    if (resultCode == 0) {
        if (isQrRequest && obj.contains("Return")) {
            result.status = VerifactuResult::SUCCESS;
            result.qrCode = decodeImageFromBase64(obj.value("Return").toString());
        } else if (obj.contains("Return")) {
            QJsonObject ret = obj.value("Return").toObject();
            // ResultCode 0 can still carry an invoice-level error in Return.ErrorCode
            QString invoiceError = ret.value("ErrorCode").toString();
            if (!invoiceError.isEmpty()) {
                result.status = VerifactuResult::ERROR;
                result.errorCode = invoiceError;
                result.errorDescription = ret.value("ErrorDescription").toString("Error desconocido");
            } else {
                result.status = VerifactuResult::SUCCESS;
                result.csv           = ret.value("CSV").toString();
                result.validationUrl = ret.value("ValidationUrl").toString();
                result.rawXml        = ret.value("Xml").toString();
                // Extract <sum1:Huella> (or any prefix) from the AEAT XML so we
                // can persist it for local tamper-detection - Art. 12 RD 1007/2023.
                // "Huella" is the regulatory term for the chained SHA-256 hash.
                // The element name must be exactly Huella (optional namespace prefix);
                // not a substring like HuellaPrevia which would also have matched a
                // looser pattern.
                static const QRegularExpression hashRx(
                    QStringLiteral("<(?:\\w+:)?Huella(?:\\s[^>]*)?>\\s*([0-9A-Fa-f]+)\\s*</(?:\\w+:)?Huella>"));
                QRegularExpressionMatch m = hashRx.match(result.rawXml);
                if (m.hasMatch())
                    result.rawHash = m.captured(1).trimmed().toUpper();
                QString qrBase64     = ret.value("QrCode").toString();
                if (!qrBase64.isEmpty())
                    result.qrCode = decodeImageFromBase64(qrBase64);
            }
        } else {
            result.status = VerifactuResult::SUCCESS;
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

QPixmap VerifactuManager::decodeImageFromBase64(const QString &data) const
{
    QByteArray byteArray = QByteArray::fromBase64(data.toLatin1());
    QPixmap pixmap;
    pixmap.loadFromData(byteArray);
    return pixmap;
}
