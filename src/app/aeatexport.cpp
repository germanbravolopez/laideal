#include "aeatexport.h"

#include <QDateTime>
#include <QIODevice>
#include <QRegularExpression>
#include <QXmlStreamWriter>

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
    // Writes the payload's raw bytes inside the open element, closing its start tag first.
    const auto writePayload = [&w, out](QString payload) {
        payload.remove(xmlDeclRx);
        if (payload.isEmpty())
            w.writeAttribute("sinPayload", "1");   // known to AEAT, payload not stored
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
            writePayload(r.xml);
            w.writeEndElement(); // Registro
            ++written;
        }
        // A cancellation is AEAT's own record, dated when it happened (a substituted
        // invoice has none: its rectificativa is a Registro of its own).
        if (r.estado == QLatin1String("ANULADA") && inRange(r.fechaAnulacion)) {
            w.writeStartElement("Anulacion");
            w.writeAttribute("nRecibo",        r.nRecibo);
            w.writeAttribute("invoiceId",      r.invoiceId);
            w.writeAttribute("fechaPago",      r.fechaPago);
            w.writeAttribute("fechaAnulacion", r.fechaAnulacion);
            writePayload(r.cancelXml);
            w.writeEndElement(); // Anulacion
            ++written;
        }
    }

    w.writeEndElement(); // RegistrosFacturacionLaIdeal
    w.writeEndDocument();
    return written;
}
