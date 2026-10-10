#include "aeatresponse.h"

#include <QDateTime>
#include <QSet>
#include <QStringList>
#include <QXmlStreamReader>

namespace AeatResponse {

namespace {

// Walks the document calling `onText(path, text)` for every leaf, where path is the
// list of local names from the root, and `onStart` / `onEnd` with the path of each
// element. Returns false when the document is not well-formed.
template <typename Start, typename Text, typename End>
bool walk(const QByteArray &body, Start onStart, Text onText, End onEnd)
{
    QXmlStreamReader r(body);
    QStringList path;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            path << r.name().toString();
            onStart(path);
        } else if (r.isEndElement()) {
            onEnd(path);
            path.removeLast();
        } else if (r.isCharacters() && !r.isWhitespace() && !path.isEmpty()) {
            onText(path, r.text().toString().trimmed());
        }
    }
    return !r.hasError();
}

bool under(const QStringList &path, const QString &parent)
{
    return path.size() >= 2 && path.contains(parent);
}

} // namespace

const Line *Submission::lineFor(const QString &invoiceNumber, const QString &operation) const
{
    for (const Line &line : lines) {
        if (line.invoiceNumber == invoiceNumber.trimmed()
            && line.operation.compare(operation, Qt::CaseInsensitive) == 0)
            return &line;
    }
    return nullptr;
}

Submission parseSubmission(const QByteArray &body)
{
    Submission reply;
    bool sawReply = false;
    Line line;
    const bool wellFormed = walk(body,
        [&](const QStringList &path) {
            if (path.last() == QLatin1String("RespuestaRegFactuSistemaFacturacion"))
                sawReply = true;
            else if (path.last() == QLatin1String("RespuestaLinea"))
                line = Line();
            else if (path.last() == QLatin1String("Fault"))
                reply.fault = true;
        },
        [&](const QStringList &path, const QString &text) {
            const QString &name = path.last();
            if (reply.fault) {
                if (name == QLatin1String("faultcode"))   reply.faultCode = text;
                if (name == QLatin1String("faultstring")) reply.faultText = text;
                return;
            }
            if (under(path, QStringLiteral("RespuestaLinea"))) {
                if (under(path, QStringLiteral("RegistroDuplicado"))) {
                    line.duplicate = true;
                    if (name == QLatin1String("IdPeticionRegistroDuplicado")) line.duplicateRequestId = text;
                    if (name == QLatin1String("EstadoRegistroDuplicado"))     line.duplicateState = text;
                    return;
                }
                if (name == QLatin1String("IDEmisorFactura"))          line.issuerNif = text;
                else if (name == QLatin1String("NumSerieFactura"))     line.invoiceNumber = text;
                else if (name == QLatin1String("FechaExpedicionFactura")) line.issueDate = text;
                else if (name == QLatin1String("TipoOperacion"))       line.operation = text;
                else if (name == QLatin1String("EstadoRegistro"))      line.state = text;
                else if (name == QLatin1String("CodigoErrorRegistro")) line.errorCode = text;
                else if (name == QLatin1String("DescripcionErrorRegistro")) line.errorDescription = text;
                return;
            }
            if (name == QLatin1String("CSV"))                        reply.csv = text;
            else if (name == QLatin1String("TimestampPresentacion")) reply.presentedAt = text;
            else if (name == QLatin1String("TiempoEsperaEnvio"))     reply.waitSeconds = text.toInt();
            else if (name == QLatin1String("EstadoEnvio"))           reply.sendState = text;
        },
        [&](const QStringList &path) {
            if (path.last() == QLatin1String("RespuestaLinea"))
                reply.lines << line;
        });
    reply.parsed = wellFormed && (sawReply || reply.fault);
    return reply;
}

Query parseQuery(const QByteArray &body)
{
    Query reply;
    bool sawReply = false;
    QueryRecord record;
    const bool wellFormed = walk(body,
        [&](const QStringList &path) {
            if (path.last() == QLatin1String("RespuestaConsultaFactuSistemaFacturacion"))
                sawReply = true;
            else if (path.last() == QLatin1String("RegistroRespuestaConsultaFactuSistemaFacturacion"))
                record = QueryRecord();
            else if (path.last() == QLatin1String("Fault"))
                reply.fault = true;
        },
        [&](const QStringList &path, const QString &text) {
            const QString &name = path.last();
            if (reply.fault) {
                if (name == QLatin1String("faultstring")) reply.faultText = text;
                return;
            }
            if (!under(path, QStringLiteral("RegistroRespuestaConsultaFactuSistemaFacturacion"))) {
                if (name == QLatin1String("ResultadoConsulta"))   reply.hasData = (text == QLatin1String("ConDatos"));
                if (name == QLatin1String("IndicadorPaginacion")) reply.morePages = (text == QLatin1String("S"));
                return;
            }
            // The chain block repeats IDEmisorFactura / NumSerieFactura / Huella of the
            // previous record: only its hash is kept, as the link.
            if (under(path, QStringLiteral("Encadenamiento"))) {
                if (name == QLatin1String("Huella"))
                    record.previousHash = text;
                return;
            }
            const bool inId = path.size() >= 2 && path.at(path.size() - 2) == QLatin1String("IDFactura");
            if (inId && name == QLatin1String("IDEmisorFactura"))        record.issuerNif = text;
            else if (inId && name == QLatin1String("NumSerieFactura"))   record.invoiceNumber = text;
            else if (inId && name == QLatin1String("FechaExpedicionFactura")) record.issueDate = text;
            else if (name == QLatin1String("TipoFactura"))               record.invoiceType = text;
            else if (name == QLatin1String("ImporteTotal"))              record.totalAmount = text;
            else if (name == QLatin1String("Huella"))                    record.hash = text;
            else if (name == QLatin1String("FechaHoraHusoGenRegistro"))  record.generatedAt = text;
            else if (name == QLatin1String("IdPeticion"))                record.requestId = text;
            else if (name == QLatin1String("TimestampPresentacion"))     record.presentedAt = text;
            else if (name == QLatin1String("EstadoRegistro"))            record.state = text;
            else if (name == QLatin1String("CodigoErrorRegistro"))       record.errorCode = text;
            else if (name == QLatin1String("DescripcionErrorRegistro"))  record.errorDescription = text;
        },
        [&](const QStringList &path) {
            if (path.last() == QLatin1String("RegistroRespuestaConsultaFactuSistemaFacturacion"))
                reply.records << record;
        });
    reply.parsed = wellFormed && (sawReply || reply.fault);
    return reply;
}

RecordSummary summarizeRecord(const QString &xml)
{
    RecordSummary out;
    walk(xml.toUtf8(),
        [&](const QStringList &path) {
            const QString &name = path.last();
            if (out.operation.isEmpty() && name == QLatin1String("RegistroAlta"))
                out.operation = QStringLiteral("Alta");
            else if (out.operation.isEmpty() && name == QLatin1String("RegistroAnulacion"))
                out.operation = QStringLiteral("Anulacion");
        },
        [&](const QStringList &path, const QString &text) {
            if (out.operation.isEmpty())
                return;
            const QString &name = path.last();
            if (path.contains(QStringLiteral("Encadenamiento"))) {
                if (name == QLatin1String("Huella") && out.previousHash.isEmpty())
                    out.previousHash = text;
                return;
            }
            if (path.contains(QStringLiteral("SistemaInformatico")) || path.contains(QStringLiteral("FacturasRectificadas")))
                return;
            if ((name == QLatin1String("IDEmisorFactura") || name == QLatin1String("IDEmisorFacturaAnulada")) && out.issuerNif.isEmpty())
                out.issuerNif = text;
            else if ((name == QLatin1String("NumSerieFactura") || name == QLatin1String("NumSerieFacturaAnulada")) && out.invoiceNumber.isEmpty())
                out.invoiceNumber = text;
            else if ((name == QLatin1String("FechaExpedicionFactura") || name == QLatin1String("FechaExpedicionFacturaAnulada")) && out.issueDate.isEmpty())
                out.issueDate = text;
            else if (name == QLatin1String("Huella") && out.hash.isEmpty())
                out.hash = text;
            else if (name == QLatin1String("FechaHoraHusoGenRegistro") && out.generatedAt.isEmpty())
                out.generatedAt = text;
        },
        [](const QStringList &) {});
    out.valid = !out.operation.isEmpty() && !out.invoiceNumber.isEmpty() && out.hash.size() == 64 && !out.generatedAt.isEmpty();
    return out;
}

const RecordSummary *chainTip(const QList<RecordSummary> &records)
{
    QSet<QString> linked;
    for (const RecordSummary &r : records)
        linked.insert(r.previousHash);
    const RecordSummary *tip = nullptr;
    for (const RecordSummary &r : records) {
        if (linked.contains(r.hash))
            continue;
        if (!tip || QDateTime::fromString(r.generatedAt, Qt::ISODate) > QDateTime::fromString(tip->generatedAt, Qt::ISODate))
            tip = &r;
    }
    // A loop or a broken chain leaves no unlinked record: fall back to the latest generated.
    if (!tip) {
        for (const RecordSummary &r : records) {
            if (!tip || QDateTime::fromString(r.generatedAt, Qt::ISODate) > QDateTime::fromString(tip->generatedAt, Qt::ISODate))
                tip = &r;
        }
    }
    return tip;
}

VerifactuResult resultFor(const Submission &reply, const QString &invoiceNumber, const QString &operation)
{
    VerifactuResult result;
    result.status = VerifactuResult::ERROR;
    if (!reply.parsed) {
        result.errorDescription = QStringLiteral("Respuesta de la AEAT no reconocida");
        return result;
    }
    if (reply.fault) {
        result.errorCode = reply.faultCode;
        result.errorDescription = reply.faultText;
        return result;
    }
    const Line *line = reply.lineFor(invoiceNumber, operation);
    if (!line) {
        result.errorDescription = QStringLiteral("La respuesta de la AEAT no incluye el registro %1").arg(invoiceNumber);
        return result;
    }
    result.errorCode = line->errorCode;
    result.errorDescription = line->errorDescription;
    if (line->state == QLatin1String("Correcto") || line->state == QLatin1String("AceptadoConErrores")) {
        result.status = VerifactuResult::SUCCESS;
        result.csv = reply.csv;
    } else if (line->duplicate && (line->duplicateState == QLatin1String("Correcta")
                                   || line->duplicateState == QLatin1String("AceptadaConErrores"))) {
        // AEAT already registered this very record (e.g. the reply to an earlier send was lost).
        result.status = VerifactuResult::SUCCESS;
        result.csv = reply.csv.isEmpty() ? QStringLiteral("IdPeticion %1").arg(line->duplicateRequestId) : reply.csv;
    }
    return result;
}

VerifactuRemoteRecord remoteRecordFor(const Query &reply, const QString &invoiceNumber)
{
    VerifactuRemoteRecord remote;
    remote.parsed = reply.parsed && !reply.fault;
    if (!remote.parsed) {
        remote.errorCode = reply.faultText;
        return remote;
    }
    for (const QueryRecord &record : reply.records) {
        if (record.invoiceNumber != invoiceNumber.trimmed())
            continue;
        ++remote.recordCount;
        remote.found = true;
        remote.invoiceId = record.invoiceNumber;
        remote.invoiceDate = record.issueDate;
        remote.totalAmount = record.totalAmount.toDouble();
        remote.statusResponse = record.state;
        remote.errorCode = record.errorCode;
        // Only an accepted registration may be adopted; a cancelled one (Anulado) is not.
        remote.isRejected = record.state != QLatin1String("Correcto") && record.state != QLatin1String("AceptadoConErrores");
        remote.csv = record.requestId.isEmpty() ? QString() : QStringLiteral("IdPeticion %1").arg(record.requestId);
    }
    return remote;
}

} // namespace AeatResponse
