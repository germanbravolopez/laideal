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

    for (const AeatExportRecord &r : records) {
        QString payload = r.xml;
        payload.remove(xmlDeclRx);

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
            w.writeAttribute("rectifica",          r.rectifiesNRecibo);
            w.writeAttribute("tipoRectificacion",  r.rectificationType);
        }
        // Known to AEAT only by its CSV (recovered with "Consultar en AEAT"): no payload stored.
        if (payload.isEmpty())
            w.writeAttribute("sinPayload", "1");
        // Close the start tag before writing the payload's raw bytes to the device.
        w.writeCharacters(QString());
        out->write(payload.toUtf8());
        w.writeEndElement(); // Registro
    }

    w.writeEndElement(); // RegistrosFacturacionLaIdeal
    w.writeEndDocument();
    return records.size();
}
