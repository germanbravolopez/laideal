#include "verifacturesponse.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

// First non-empty value among several candidate key spellings. The
// GetFilteredList schema is unpublished, so we probe the names the rest of the
// vendor's API uses rather than committing to one.
QString firstString(const QJsonObject &o, std::initializer_list<const char *> keys)
{
    for (const char *k : keys) {
        const QJsonValue v = o.value(QLatin1String(k));
        if (v.isString() && !v.toString().isEmpty())
            return v.toString();
        if (v.isDouble())
            return QString::number(v.toDouble(), 'f', 2);
    }
    return QString();
}

double firstDouble(const QJsonObject &o, std::initializer_list<const char *> keys)
{
    for (const char *k : keys) {
        const QJsonValue v = o.value(QLatin1String(k));
        if (v.isDouble()) return v.toDouble();
        if (v.isString()) {
            bool ok = false;
            // The API is inconsistent about decimal separators across fields.
            const double d = QString(v.toString()).replace(',', '.').toDouble(&ok);
            if (ok) return d;
        }
    }
    return 0.0;
}

// AEAT/vendor dates come back as dd-MM-yyyy, yyyy-MM-dd or ISO-8601. Normalise
// to the dd-MM-yyyy the ingresos table stores so the match is a plain compare.
QString normaliseDate(const QString &s)
{
    if (s.isEmpty()) return s;
    static const char *formats[] = { "dd-MM-yyyy", "yyyy-MM-dd", "dd/MM/yyyy", "yyyy/MM/dd" };
    for (const char *f : formats) {
        const QDate d = QDate::fromString(s, QLatin1String(f));
        if (d.isValid()) return d.toString(QStringLiteral("dd-MM-yyyy"));
    }
    const QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    if (dt.isValid()) return dt.date().toString(QStringLiteral("dd-MM-yyyy"));
    return s;
}

// The record list can plausibly arrive under any of these; also accept a bare
// array or a single bare object.
QJsonObject firstRecord(const QJsonObject &root, bool *found)
{
    *found = false;
    for (const char *k : { "Return", "Records", "List", "Items", "Result", "Invoices" }) {
        const QJsonValue v = root.value(QLatin1String(k));
        if (v.isArray()) {
            const QJsonArray a = v.toArray();
            if (a.isEmpty()) return {};          // queried fine, simply not there
            *found = true;
            return a.first().toObject();
        }
        if (v.isObject()) {
            *found = true;
            return v.toObject();
        }
    }
    return {};
}

} // namespace

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

VerifactuRemoteRecord parseVerifactuQueryResponse(const QByteArray &response)
{
    VerifactuRemoteRecord rec;
    rec.raw = QString::fromUtf8(response);

    const QJsonDocument doc = QJsonDocument::fromJson(response);

    // A bare array of records is as plausible as an enveloped one.
    QJsonObject obj;
    if (doc.isArray()) {
        const QJsonArray a = doc.array();
        if (a.isEmpty()) {
            rec.parsed = true;    // understood, and AEAT simply has no such invoice
            return rec;
        }
        obj = a.first().toObject();
        rec.found = true;
    } else if (doc.isObject()) {
        const QJsonObject root = doc.object();
        // A non-zero ResultCode is a failed query, not an absent invoice: leave
        // parsed=false so the caller surfaces the payload instead of concluding
        // "AEAT does not have it".
        if (root.contains(QLatin1String("ResultCode"))
                && root.value(QLatin1String("ResultCode")).toInt(-1) != 0)
            return rec;
        bool found = false;
        obj = firstRecord(root, &found);
        rec.found = found;
        if (!found) {
            // Envelope understood but empty -> genuinely not registered.
            rec.parsed = root.contains(QLatin1String("ResultCode"))
                         || root.contains(QLatin1String("Return"));
            return rec;
        }
    } else {
        return rec;               // not JSON at all
    }

    rec.invoiceId      = firstString(obj, { "InvoiceID", "InvoiceId", "NumSerieFactura", "ExternKey" });
    rec.invoiceDate    = normaliseDate(
                         firstString(obj, { "InvoiceDate", "FechaExpedicionFactura", "Date", "Created" }));
    rec.totalAmount    = firstDouble(obj, { "TotalAmount", "ImporteTotal", "Total", "Amount" });
    rec.csv            = firstString(obj, { "CSV", "Csv", "csv" });
    rec.statusResponse = firstString(obj, { "StatusResponse", "Status", "EstadoRegistro" });
    rec.validationUrl  = firstString(obj, { "ValidationUrl", "QrCodeUrl", "Url" });

    // Some shapes nest the AEAT detail one level down; look there before giving up.
    if (rec.csv.isEmpty() || rec.invoiceId.isEmpty()) {
        const QJsonObject inner = obj.value(QLatin1String("Return")).toObject();
        if (!inner.isEmpty()) {
            if (rec.csv.isEmpty())
                rec.csv = firstString(inner, { "CSV", "Csv", "csv" });
            if (rec.invoiceId.isEmpty())
                rec.invoiceId = firstString(inner, { "InvoiceID", "InvoiceId", "ExternKey" });
            if (rec.validationUrl.isEmpty())
                rec.validationUrl = firstString(inner, { "ValidationUrl", "QrCodeUrl", "Url" });
        }
    }

    // An InvoiceID is the minimum needed to say we understood a record at all.
    rec.parsed = !rec.invoiceId.isEmpty();
    return rec;
}

bool verifactuRemoteMatches(const VerifactuRemoteRecord &remote,
                            const QString &localInvoiceId,
                            const QString &localDateDdMmYyyy,
                            double localTotalAmount)
{
    if (!remote.found || !remote.parsed)                 return false;
    if (remote.invoiceId != localInvoiceId)              return false;
    // Every field must be positively confirmed: an absent date or a zero amount
    // is not agreement, it is missing evidence.
    if (remote.invoiceDate.isEmpty())                    return false;
    if (remote.invoiceDate != normaliseDate(localDateDdMmYyyy)) return false;
    if (localTotalAmount <= 0.0 || remote.totalAmount <= 0.0)   return false;
    return qAbs(remote.totalAmount - localTotalAmount) < 0.005;  // equal to the cent
}

bool verifactuErrorIsDuplicate(const QString &errorCode, const QString &errorDescription)
{
    const QString haystack = (errorCode + QLatin1Char(' ') + errorDescription).toLower();
    // Spanish (AEAT / vendor) and English phrasings seen for a repeated
    // registration. Accent-insensitive by matching the unaccented stems.
    static const char *needles[] = {
        "duplicad",        // "registro duplicado", "factura duplicada"
        "duplicate",
        "ya existe",       // "la factura ya existe"
        "ya registrad",    // "ya registrada en el sistema"
        "already exist",
        "already registered"
    };
    for (const char *n : needles) {
        if (haystack.contains(QLatin1String(n)))
            return true;
    }
    return false;
}
