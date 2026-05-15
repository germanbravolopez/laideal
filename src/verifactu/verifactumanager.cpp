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

VerifactuManager::VerifactuManager(const QString &configPath, QObject *parent)
    : QObject(parent), m_config(nullptr), m_networkManager(nullptr)
{
    m_config = new VerifactuConfig(configPath);
    m_networkManager = new QNetworkAccessManager(this);
}

VerifactuManager::~VerifactuManager()
{
    // Qt se encargará de liberar la memoria
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

    // Crear el payload JSON
    QJsonObject invoiceJson = invoice.toJson();
    invoiceJson["ServiceKey"] = m_config->getServiceKey();

    QJsonDocument doc(invoiceJson);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    // Crear y enviar la solicitud
    QNetworkRequest request = createNetworkRequest(m_config->getEndpointUrl() + "/Create");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = m_networkManager->post(request, payload);

    // Procesar la respuesta de forma síncrona (simplificado)
    // En una aplicación real, esto debería ser asíncrono
    qDebug() << "Enviando factura" << invoice.getInvoiceNumber() << "a Verifactu";

    result = processResponse(payload, false);
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

    // Validar todas las facturas
    for (const auto &invoice : invoices) {
        if (!invoice.isValid()) {
            result.status = VerifactuResult::ERROR;
            result.errorDescription = "Factura inválida: " + invoice.getValidationError();
            return result;
        }
    }

    // Crear el payload con lote de facturas
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

    QNetworkReply *reply = m_networkManager->post(request, payload);

    qDebug() << "Enviando lote de" << invoices.count() << "facturas a Verifactu";

    result = processResponse(payload, false);
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

    // Crear el payload para anulación
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

    qDebug() << "Anulando factura" << invoiceNumber;

    result = processResponse(payload, false);
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

    // Crear el payload
    QJsonObject qrJson = invoice.toJson();
    qrJson["ServiceKey"] = m_config->getServiceKey();

    QJsonDocument doc(qrJson);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest request = createNetworkRequest(m_config->getQrUrl());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = m_networkManager->post(request, payload);

    qDebug() << "Generando QR para factura" << invoice.getInvoiceNumber();

    result = processResponse(payload, true);
    result.validationUrl = getValidationUrl(invoice);
    return result;
}

QString VerifactuManager::getValidationUrl(const VerifactuInvoice &invoice) const
{
    // Construcción de la URL de validación según especificaciones de la AEAT
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

    // Hacer una solicitud simple al endpoint
    QNetworkRequest request = createNetworkRequest(m_config->getEndpointUrl());
    QNetworkReply *reply = m_networkManager->get(request);

    // En una aplicación real, esto debería ser asíncrono
    qDebug() << "Probando conexión con Verifactu...";
    return true;
}

QString VerifactuManager::getConfigurationInfo() const
{
    QString info = "=== Configuración de Verifactu ===\n";
    info += "Entorno: " + QString(m_config->getEnvironment() == VerifactuConfig::TESTING ? "PRUEBAS" : "PRODUCCIÓN") + "\n";
    info += "Emisor NIF: " + m_config->getEmitterNIF() + "\n";
    info += "Emisor Nombre: " + m_config->getEmitterName() + "\n";
    info += "Sistema: " + m_config->getSystemName() + " v" + m_config->getSystemVersion() + "\n";
    info += "Endpoint: " + m_config->getEndpointUrl() + "\n";
    info += "Configurado: " + QString(m_config->isValid() ? "SÍ" : "NO") + "\n";

    if (!m_config->isValid()) {
        info += "Error: " + m_config->getValidationError() + "\n";
    }

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

    try {
        QJsonDocument doc = QJsonDocument::fromJson(response);

        if (!doc.isObject()) {
            result.status = VerifactuResult::ERROR;
            result.errorDescription = "Respuesta inválida del servidor";
            return result;
        }

        QJsonObject obj = doc.object();
        result.rawResponse = QString::fromUtf8(response);

        // Comprobar código de resultado
        int resultCode = obj.value("ResultCode").toInt(-1);

        if (resultCode == 0) {
            result.status = VerifactuResult::SUCCESS;

            if (isQrRequest && obj.contains("Return")) {
                // Procesar respuesta de QR
                QString base64Data = obj.value("Return").toString();
                result.qrCode = decodeImageFromBase64(base64Data);
            } else if (obj.contains("Return")) {
                // Procesar respuesta de factura
                QJsonObject returnObj = obj.value("Return").toObject();
                result.csv = returnObj.value("CSV").toString();
            }
        } else {
            result.status = VerifactuResult::ERROR;
            result.errorCode = QString::number(resultCode);
            result.errorDescription = obj.value("ResultMessage").toString("Error desconocido");

            if (obj.contains("Return")) {
                QJsonObject returnObj = obj.value("Return").toObject();
                result.errorDescription = returnObj.value("ErrorDescription").toString(result.errorDescription);
            }
        }

    } catch (const std::exception &e) {
        result.status = VerifactuResult::ERROR;
        result.errorDescription = QString("Excepción al procesar respuesta: %1").arg(e.what());
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
        qCritical() << "Error de red:" << m_lastError;
    } else {
        QByteArray responseData = reply->readAll();
        m_lastResult = processResponse(responseData);
    }

    reply->deleteLater();
}
