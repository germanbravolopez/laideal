#include "verifactumanager.h"
#include "testhostguard.h"
#include "aeathash.h"
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
#include <QPointer>

VerifactuManager::VerifactuManager(QObject *parent)
    : VerifactuBackend(parent), m_config(new VerifactuConfig), m_networkManager(new QNetworkAccessManager(this))
{
}

VerifactuManager::~VerifactuManager() = default;

QString VerifactuManager::nextRequestId()
{
    static int counter = 0;     // unique across every manager of the run (see AeatDirectBackend)
    return QStringLiteral("req-%1").arg(++counter);
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

VerifactuResult VerifactuManager::networkErrorResult(QNetworkReply *reply, bool gatewayReason)
{
    VerifactuResult result;
    result.status = VerifactuResult::NETWORK_ERROR;
    result.errorDescription = "Error de conexión: " + reply->errorString();
    if (!gatewayReason)
        return result;
    // An HTTP error still may carry the gateway's own reason; the status stays unknown-outcome.
    const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
    QString gatewayMessage = body.value("Return").toObject().value("ErrorDescription").toString();
    if (gatewayMessage.isEmpty())
        gatewayMessage = body.value("ResultMessage").toString();
    if (gatewayMessage.isEmpty())
        gatewayMessage = body.value("message").toString();
    if (!gatewayMessage.isEmpty()) {
        constexpr int kMaxGatewayMessage = 300;
        if (gatewayMessage.size() > kMaxGatewayMessage)
            gatewayMessage = gatewayMessage.left(kMaxGatewayMessage) + "...";
        result.errorDescription += " - " + gatewayMessage;
    }
    return result;
}

void VerifactuManager::wireReply(const QString &reqId, QNetworkReply *reply, VerifactuReply kind,
                                 std::function<void(VerifactuResult &)> stamp)
{
    connect(reply, &QNetworkReply::finished, this, [this, reqId, reply, kind, stamp]() {
        VerifactuResult result;
        if (reply->error() != QNetworkReply::NoError) {
            result = networkErrorResult(reply, kind != VerifactuReply::Qr);
            m_lastError = result.errorDescription;
            qWarning() << "Verifactu network error (" << reqId << "):" << m_lastError;
        } else {
            const QByteArray responseData = reply->readAll();
            logResponse(responseData);
            result = parseVerifactuResponse(responseData, kind);
            if (stamp)
                stamp(result);
        }
        reply->deleteLater();
        emit requestFinished(reqId, result);
    });
}

QString VerifactuManager::submitInvoiceAsync(const VerifactuInvoice &invoice)
{
    const QString reqId = nextRequestId();

    if (!validateConfiguration()) {
        emitErrorAsync(reqId, VerifactuResult::INVALID_CONFIG, m_lastError);
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

    qDebug() << "Submitting invoice" << invoice.getInvoiceNumber() << "(" << reqId << ") to"
             << (m_config->getEndpointUrl() + "/Create");
    // Redact ServiceKey before logging - the JSON ends up in ~/.laideal.log and the key
    // is the AEAT API credential.
    QJsonObject loggable = invoiceJson;
    loggable["ServiceKey"] = QStringLiteral("[redacted, %1 chars]")
        .arg(invoiceJson.value("ServiceKey").toString().length());
    qDebug().noquote() << "Payload:" << QString::fromUtf8(QJsonDocument(loggable).toJson(QJsonDocument::Indented));

    QNetworkReply *reply = m_networkManager->post(request, payload);
    wireReply(reqId, reply, VerifactuReply::Submit);
    return reqId;
}

QString VerifactuManager::queryInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate)
{
    Q_UNUSED(invoiceDate)   // the gateway finds the invoice by its ID alone
    const QString reqId = nextRequestId();

    const bool configured = validateConfiguration();
    if (!configured || invoiceNumber.isEmpty()) {
        VerifactuRemoteRecord rec;   // parsed=false: we know nothing, not "absent"
        rec.raw = configured ? QStringLiteral("InvoiceID vacío") : m_lastError;
        QMetaObject::invokeMethod(this, [this, reqId, rec]() {
            emit queryFinished(reqId, rec);
        }, Qt::QueuedConnection);
        return reqId;
    }

    // FilterSet shape per the vendor's own REST client (Net/Rest/List/FilterSet.cs):
    // Filters[] of {FieldName, Operator, Value}, plus paging. Count/Offset default
    // to -1 there, which the service reads as "unset".
    QJsonObject filter;
    filter["FieldName"] = QStringLiteral("InvoiceID");
    filter["Operator"]  = QStringLiteral("=");
    filter["Value"]     = invoiceNumber;

    QJsonObject filterSet;
    filterSet["Filters"]    = QJsonArray{ filter };
    filterSet["Count"]      = 10;
    filterSet["Offset"]     = -1;
    filterSet["ServiceKey"] = m_config->getServiceKey();

    const QString url = m_config->getEndpointUrl() + "/GetFilteredList";
    QNetworkRequest request = createNetworkRequest(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qDebug() << "Querying AEAT for invoice" << invoiceNumber << "(" << reqId << ") at" << url;

    QNetworkReply *reply = m_networkManager->post(
        request, QJsonDocument(filterSet).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reqId, reply, invoiceNumber]() {
        VerifactuRemoteRecord rec;
        if (reply->error() != QNetworkReply::NoError) {
            rec.raw = reply->errorString();   // parsed stays false - a failed query
            qWarning() << "Verifactu query network error for" << invoiceNumber
                       << "-" << reply->errorString();
        } else {
            const QByteArray body = reply->readAll();
            rec = parseVerifactuQueryResponse(body);
            // The schema is unpublished, so always log the payload: the first real
            // reply is what lets the candidate field names be narrowed to the truth.
            qDebug().noquote() << "Verifactu query reply for" << invoiceNumber
                               << "- parsed=" << rec.parsed << "found=" << rec.found
                               << "\n" << QString::fromUtf8(body);
        }
        reply->deleteLater();
        emit queryFinished(reqId, rec);
    });
    return reqId;
}

QString VerifactuManager::cancelInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate)
{
    const QString reqId = nextRequestId();

    if (!validateConfiguration()) {
        emitErrorAsync(reqId, VerifactuResult::INVALID_CONFIG, m_lastError);
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
    wireReply(reqId, reply, VerifactuReply::Cancel);
    return reqId;
}

QString VerifactuManager::generateQRAsync(const VerifactuInvoice &invoice)
{
    const QString reqId = nextRequestId();

    if (!validateConfiguration()) {
        emitErrorAsync(reqId, VerifactuResult::INVALID_CONFIG, m_lastError);
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
    // The validation URL is assembled locally, not read from the reply.
    const QString validationUrl = getValidationUrl(invoice);
    wireReply(reqId, reply, VerifactuReply::Qr, [validationUrl](VerifactuResult &r) { r.validationUrl = validationUrl; });
    return reqId;
}

QString VerifactuManager::getValidationUrl(const VerifactuInvoice &invoice) const
{
    QString url = m_config->getValidationUrl();
    QUrlQuery query;

    query.addQueryItem("nif", invoice.getSellerNIF());
    query.addQueryItem("numserie", invoice.getInvoiceNumber());
    query.addQueryItem("fecha", invoice.getInvoiceDate().toString(QLatin1String(AeatHash::kDateFormat)));
    query.addQueryItem("importe", QString::number(invoice.getTotalAmount(), 'f', 2));

    url += "?" + query.toString();
    return url;
}

VerifactuResult VerifactuManager::testConnection()
{
    VerifactuResult result;

    if (!validateConfiguration()) {
        result.status = VerifactuResult::INVALID_CONFIG;
        result.errorDescription = m_lastError;
        return result;
    }

    // POST a stub payload to /Create (GET on the base URL never answers). The service
    // refuses the stub for its missing fields; only a key refusal or a server error fails.
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
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (!httpStatus.isValid()) {
        // No HTTP response - DNS failure, connection refused, or timeout
        result.status = VerifactuResult::NETWORK_ERROR;
        result.errorDescription = "Error de red: " + errStr;
        m_lastError = result.errorDescription;
        qWarning() << "testConnection: network error" << errStr;
        return result;
    }

    result = connectionProbeResult(httpStatus.toInt(), body);
    if (!result.isSuccess())
        m_lastError = result.errorDescription;
    qDebug() << "testConnection: HTTP" << httpStatus.toInt() << "status" << result.status << result.errorDescription;
    return result;
}

VerifactuResult VerifactuManager::connectionProbeResult(int httpStatus, const QByteArray &body)
{
    VerifactuResult result;
    const QString http = QString("HTTP %1").arg(httpStatus);
    if (httpStatus == 401 || httpStatus == 403) {
        result.status = VerifactuResult::ERROR;
        result.errorDescription = "Clave de servicio no válida o sin permiso (" + http + ")";
        return result;
    }
    if (httpStatus >= 500 || httpStatus < 200) {
        result.status = VerifactuResult::NETWORK_ERROR;
        result.errorDescription = "El servidor respondió con un error (" + http + ")";
        return result;
    }
    const VerifactuResult parsed = parseVerifactuResponse(body, VerifactuReply::Cancel);
    if (parsed.status == VerifactuResult::NETWORK_ERROR) {
        result.status = VerifactuResult::NETWORK_ERROR;
        result.errorDescription = "Respuesta no válida del servidor (" + http + ")";
        return result;
    }
    const bool keyRefused = parsed.errorCode == QLatin1String("8001")
                            || parsed.errorDescription.contains("ServiceKey", Qt::CaseInsensitive)
                            || parsed.errorDescription.contains("clave", Qt::CaseInsensitive);
    if (parsed.status == VerifactuResult::ERROR && keyRefused) {
        result.status = VerifactuResult::ERROR;
        result.errorCode = parsed.errorCode;
        result.errorDescription = "Clave de servicio no válida: " + parsed.errorDescription;
        return result;
    }
    result.status = VerifactuResult::SUCCESS;
    result.errorDescription = http;
    return result;
}

QString VerifactuManager::configurationInfo() const
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
    // Under LAIDEAL_TEST a non-local host gets an empty URL: the request fails at once.
    QNetworkRequest request{TestHostGuard::allows(QUrl(endpoint)) ? QUrl(endpoint) : QUrl()};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "LAIDEAL/" + QString(PROJECT_VERSION));
    // A stalled connection would never answer: it ends as a NETWORK_ERROR (the row stays
    // pending and is checked), so no request waits forever.
    request.setTransferTimeout(30000);
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

bool VerifactuManager::validateConfiguration()
{
    if (!m_config->isValid()) {
        m_lastError = m_config->getValidationError();
        return false;
    }
    return true;
}
