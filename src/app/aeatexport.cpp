#include "aeatexport.h"

#include <QDateTime>
#include <QDebug>
#include <QIODevice>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {

struct PayloadInfo {
    bool    wellFormed = true;
    QString invoiceDate;    // first FechaExpedicionFactura[Anulada], dd-MM-yyyy
};

// Checks the payload is a fragment that can be inlined, and reads the date AEAT
// registered the invoice under: the first FechaExpedicionFactura (later ones belong
// to the previous record of the chain or to a rectified invoice).
PayloadInfo inspectPayload(const QString &payload)
{
    PayloadInfo info;
    // Namespace-aware, so a payload with an undeclared prefix is written as text too:
    // the file must parse in any XML tool Hacienda uses.
    QXmlStreamReader r(QStringLiteral("<payload>") + payload + QStringLiteral("</payload>"));
    bool dtd = false;
    while (!r.atEnd()) {
        const QXmlStreamReader::TokenType token = r.readNext();
        // A DOCTYPE is only legal before a document's root, never inside an element,
        // although QXmlStreamReader accepts it there.
        if (token == QXmlStreamReader::DTD) {
            dtd = true;
            break;
        }
        if (token == QXmlStreamReader::StartElement && info.invoiceDate.isEmpty()) {
            if (r.name().startsWith(u"FechaExpedicionFactura")) {
                const QString text = r.readElementText();
                if (QDate::fromString(text, "dd-MM-yyyy").isValid())
                    info.invoiceDate = text;
            }
        }
    }
    info.wellFormed = !dtd && !r.hasError();
    if (!info.wellFormed)
        info.invoiceDate.clear();
    return info;
}

} // namespace

int writeAeatExportXml(QIODevice *out, const QVector<AeatExportRecord> &records,
                       const QDate &from, const QDate &to,
                       const QString &nif, const QString &issuerName)
{
    QXmlStreamWriter w(out);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement("RegistrosFacturacionLaIdeal");
    w.writeAttribute("fechaDesde", from.toString("dd-MM-yyyy"));
    w.writeAttribute("fechaHasta", to.toString("dd-MM-yyyy"));
    w.writeAttribute("generadoEl", QDateTime::currentDateTime().toString(Qt::ISODate));
    w.writeAttribute("nif",        nif);
    w.writeAttribute("emisor",     issuerName);

    // Each stored payload loses its XML declaration so the outer document stays well-formed.
    static const QRegularExpression xmlDeclRx(QStringLiteral("^\\s*<\\?xml[^?]*\\?>\\s*"));

    const auto inRange = [&from, &to](const QString &ddMMyyyy) {
        const QDate d = QDate::fromString(ddMMyyyy, "dd-MM-yyyy");
        return d.isValid() && d >= from && d <= to;
    };
    // Writes the payload inside the open element: its raw bytes when it is a
    // well-formed fragment, as escaped text otherwise so the file stays readable.
    // fechaExpedicion is added when AEAT registered the invoice under another date
    // than the payment (older invoices sent with the reception date).
    const auto writePayload = [&w, out](QString payload, const QString &fechaPago) {
        payload.remove(xmlDeclRx);
        if (payload.isEmpty()) {
            w.writeAttribute("sinPayload", "1");   // known to AEAT, payload not stored
            return;
        }
        const PayloadInfo info = inspectPayload(payload);
        if (!info.invoiceDate.isEmpty() && info.invoiceDate != fechaPago)
            w.writeAttribute("fechaExpedicion", info.invoiceDate);
        if (!info.wellFormed) {
            qWarning() << "writeAeatExportXml: payload is not well-formed XML, written as text";
            w.writeAttribute("payloadComoTexto", "1");
            w.writeCharacters(payload);
            return;
        }
        w.writeCharacters(QString());
        out->write(payload.toUtf8());
    };

    int written = 0;
    for (const AeatExportRecord &r : records) {
        if (inRange(r.fechaPago)) {
            w.writeStartElement("Registro");
            w.writeAttribute("nRecibo",   r.nRecibo);
            w.writeAttribute("invoiceId", r.invoiceId);
            w.writeAttribute("fechaPago", r.fechaPago);
            w.writeAttribute("importe",   QString::number(r.importe, 'f', 2));
            w.writeAttribute("csv",       r.csv);
            w.writeAttribute("estado",    r.estado);
            if (!r.fechaAnulacion.isEmpty())
                w.writeAttribute("fechaAnulacion", r.fechaAnulacion);
            if (!r.rectifiesNRecibo.isEmpty()) {
                w.writeAttribute("rectifica",         r.rectifiesNRecibo);
                w.writeAttribute("tipoRectificacion", r.rectificationType);
            }
            writePayload(r.xml, r.fechaPago);
            w.writeEndElement(); // Registro
            ++written;
        }
        // A cancellation is AEAT's own record, dated when it happened (a substituted
        // invoice has none: its rectificativa is a Registro of its own). One made
        // before 10.12 has no date (aeatExportRecords also blanks an unreadable one):
        // it goes with its invoice, marked sinFecha.
        const bool undatedCancel = r.fechaAnulacion.isEmpty() && inRange(r.fechaPago);
        if (r.estado == QLatin1String("ANULADA") && (inRange(r.fechaAnulacion) || undatedCancel)) {
            w.writeStartElement("Anulacion");
            w.writeAttribute("nRecibo",        r.nRecibo);
            w.writeAttribute("invoiceId",      r.invoiceId);
            w.writeAttribute("fechaPago",      r.fechaPago);
            if (undatedCancel)
                w.writeAttribute("sinFecha", "1");
            else
                w.writeAttribute("fechaAnulacion", r.fechaAnulacion);
            writePayload(r.cancelXml, r.fechaPago);
            w.writeEndElement(); // Anulacion
            ++written;
        }
    }

    w.writeEndElement(); // RegistrosFacturacionLaIdeal
    w.writeEndDocument();
    return written;
}
