#include "verifacturesponse.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

QPixmap decodeVerifactuImageBase64(const QString &base64)
{
    QByteArray bytes = QByteArray::fromBase64(base64.toLatin1());
    QPixmap pixmap;
    pixmap.loadFromData(bytes);
    return pixmap;
}

VerifactuResult parseVerifactuResponse(const QByteArray &response, bool isQrRequest)
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
            result.qrCode = decodeVerifactuImageBase64(obj.value("Return").toString());
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
                    result.qrCode = decodeVerifactuImageBase64(qrBase64);
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
