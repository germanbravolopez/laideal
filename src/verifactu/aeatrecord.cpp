#include "aeatrecord.h"

#include "aeathash.h"

#include <QXmlStreamWriter>

namespace AeatRecord {

const char *const kSuministroInformacionNs =
    "https://www2.agenciatributaria.gob.es/static_files/common/internet/dep/aplicaciones/es/aeat/tike/cont/ws/SuministroInformacion.xsd";
const char *const kSuministroLRNs =
    "https://www2.agenciatributaria.gob.es/static_files/common/internet/dep/aplicaciones/es/aeat/tike/cont/ws/SuministroLR.xsd";
const char *const kConsultaLRNs =
    "https://www2.agenciatributaria.gob.es/static_files/common/internet/dep/aplicaciones/es/aeat/tike/cont/ws/ConsultaLR.xsd";

namespace {

const char *const kSoapNs = "http://schemas.xmlsoap.org/soap/envelope/";
const QString kDateFormat = QStringLiteral("dd-MM-yyyy");
const QString kVersion = QStringLiteral("1.0");

QString dateText(const QDate &date)
{
    return date.toString(kDateFormat);
}

QString rateText(double rate)
{
    return QString::number(rate, 'f', 2);
}

// Every element of a record lives in the SuministroInformacion namespace ("sf").
class RecordWriter
{
public:
    explicit RecordWriter(QString *out) : m_xml(out)
    {
        m_xml.setAutoFormatting(false);
    }
    void start(const QString &name, bool declareNamespace = false)
    {
        m_xml.writeStartElement(QStringLiteral("sf:") + name);
        if (declareNamespace)
            m_xml.writeNamespace(QString::fromLatin1(kSuministroInformacionNs), QStringLiteral("sf"));
    }
    void end() { m_xml.writeEndElement(); }
    void text(const QString &name, const QString &value)
    {
        m_xml.writeTextElement(QStringLiteral("sf:") + name, value.trimmed());
    }
    void invoiceId(const QString &element, const InvoiceRef &ref, const QString &prefix = QString(),
                   const QString &suffix = QString())
    {
        start(element);
        text(prefix + QStringLiteral("IDEmisorFactura") + suffix, ref.issuerNif);
        text(prefix + QStringLiteral("NumSerieFactura") + suffix, ref.invoiceNumber);
        text(prefix + QStringLiteral("FechaExpedicionFactura") + suffix, dateText(ref.issueDate));
        end();
    }
    void chain(const PreviousRecord &previous)
    {
        start(QStringLiteral("Encadenamiento"));
        if (previous.isFirst()) {
            text(QStringLiteral("PrimerRegistro"), QStringLiteral("S"));
        } else {
            start(QStringLiteral("RegistroAnterior"));
            text(QStringLiteral("IDEmisorFactura"), previous.invoice.issuerNif);
            text(QStringLiteral("NumSerieFactura"), previous.invoice.invoiceNumber);
            text(QStringLiteral("FechaExpedicionFactura"), dateText(previous.invoice.issueDate));
            text(QStringLiteral("Huella"), previous.hash);
            end();
        }
        end();
    }
    void system(const SystemInfo &s)
    {
        start(QStringLiteral("SistemaInformatico"));
        text(QStringLiteral("NombreRazon"), s.producerName);
        text(QStringLiteral("NIF"), s.producerNif);
        text(QStringLiteral("NombreSistemaInformatico"), s.systemName);
        text(QStringLiteral("IdSistemaInformatico"), s.systemId);
        text(QStringLiteral("Version"), s.version);
        text(QStringLiteral("NumeroInstalacion"), s.installationNumber);
        // A single-issuer, VERI*FACTU-only system.
        text(QStringLiteral("TipoUsoPosibleSoloVerifactu"), QStringLiteral("S"));
        text(QStringLiteral("TipoUsoPosibleMultiOT"), QStringLiteral("N"));
        text(QStringLiteral("IndicadorMultiplesOT"), QStringLiteral("N"));
        end();
    }
    void finishRecord(const QDateTime &generatedAt, const QString &hash)
    {
        text(QStringLiteral("FechaHoraHusoGenRegistro"), AeatHash::timestampText(generatedAt));
        text(QStringLiteral("TipoHuella"), QStringLiteral("01"));   // SHA-256
        text(QStringLiteral("Huella"), hash);
    }

private:
    QXmlStreamWriter m_xml;
};

QString escaped(const QString &text)
{
    return text.trimmed().toHtmlEscaped();
}

} // namespace

// The totals add up the line amounts as written (in cents), which is what AEAT checks
// CuotaTotal and ImporteTotal against.
static double written(double amount)
{
    return AeatHash::amountText(amount).toDouble();
}

double Registration::totalTax() const
{
    double sum = 0.0;
    for (const TaxLine &line : taxLines)
        sum += written(line.tax);
    return sum;
}

double Registration::totalAmount() const
{
    double sum = 0.0;
    for (const TaxLine &line : taxLines)
        sum += written(line.base) + written(line.tax);
    return sum;
}

QString hashOf(const Registration &r)
{
    AeatHash::RegistrationFields f;
    f.issuerNif     = r.invoice.issuerNif;
    f.invoiceNumber = r.invoice.invoiceNumber;
    f.issueDate     = r.invoice.issueDate;
    f.invoiceType   = r.invoiceType;
    f.totalTax      = AeatHash::amountText(r.totalTax());
    f.totalAmount   = AeatHash::amountText(r.totalAmount());
    f.previousHash  = r.previous.hash;
    f.generatedAt   = AeatHash::timestampText(r.generatedAt);
    return AeatHash::registrationHash(f);
}

QString hashOf(const Cancellation &c)
{
    AeatHash::CancellationFields f;
    f.issuerNif     = c.invoice.issuerNif;
    f.invoiceNumber = c.invoice.invoiceNumber;
    f.issueDate     = c.invoice.issueDate;
    f.previousHash  = c.previous.hash;
    f.generatedAt   = AeatHash::timestampText(c.generatedAt);
    return AeatHash::cancellationHash(f);
}

QString registrationXml(const Registration &r, const SystemInfo &system)
{
    QString out;
    RecordWriter w(&out);
    w.start(QStringLiteral("RegistroAlta"), true);
    w.text(QStringLiteral("IDVersion"), kVersion);
    w.invoiceId(QStringLiteral("IDFactura"), r.invoice);
    w.text(QStringLiteral("NombreRazonEmisor"), r.issuerName);
    if (r.afterRejection) {
        w.text(QStringLiteral("Subsanacion"), QStringLiteral("S"));
        w.text(QStringLiteral("RechazoPrevio"), QStringLiteral("S"));
    }
    w.text(QStringLiteral("TipoFactura"), r.invoiceType);
    if (!r.rectificationType.isEmpty()) {
        w.text(QStringLiteral("TipoRectificativa"), r.rectificationType);
        if (!r.rectifiedInvoices.isEmpty()) {
            w.start(QStringLiteral("FacturasRectificadas"));
            for (const InvoiceRef &ref : r.rectifiedInvoices)
                w.invoiceId(QStringLiteral("IDFacturaRectificada"), ref);
            w.end();
        }
        if (r.hasRectifiedAmounts) {
            w.start(QStringLiteral("ImporteRectificacion"));
            w.text(QStringLiteral("BaseRectificada"), AeatHash::amountText(r.rectifiedBase));
            w.text(QStringLiteral("CuotaRectificada"), AeatHash::amountText(r.rectifiedTax));
            w.end();
        }
    }
    w.text(QStringLiteral("DescripcionOperacion"), r.description.left(500));
    w.start(QStringLiteral("Desglose"));
    for (const TaxLine &line : r.taxLines) {
        w.start(QStringLiteral("DetalleDesglose"));
        w.text(QStringLiteral("Impuesto"), QStringLiteral("01"));                 // IVA
        w.text(QStringLiteral("ClaveRegimen"), QStringLiteral("01"));             // general regime
        w.text(QStringLiteral("CalificacionOperacion"), QStringLiteral("S1"));    // subject, not exempt
        w.text(QStringLiteral("TipoImpositivo"), rateText(line.rate));
        w.text(QStringLiteral("BaseImponibleOimporteNoSujeto"), AeatHash::amountText(line.base));
        w.text(QStringLiteral("CuotaRepercutida"), AeatHash::amountText(line.tax));
        w.end();
    }
    w.end();
    w.text(QStringLiteral("CuotaTotal"), AeatHash::amountText(r.totalTax()));
    w.text(QStringLiteral("ImporteTotal"), AeatHash::amountText(r.totalAmount()));
    w.chain(r.previous);
    w.system(system);
    w.finishRecord(r.generatedAt, hashOf(r));
    w.end();
    return out;
}

QString cancellationXml(const Cancellation &c, const SystemInfo &system)
{
    QString out;
    RecordWriter w(&out);
    w.start(QStringLiteral("RegistroAnulacion"), true);
    w.text(QStringLiteral("IDVersion"), kVersion);
    w.invoiceId(QStringLiteral("IDFactura"), c.invoice, QString(), QStringLiteral("Anulada"));
    if (c.afterRejection)
        w.text(QStringLiteral("RechazoPrevio"), QStringLiteral("S"));
    w.chain(c.previous);
    w.system(system);
    w.finishRecord(c.generatedAt, hashOf(c));
    w.end();
    return out;
}

QString submissionEnvelope(const QString &issuerName, const QString &issuerNif, const QStringList &recordXmls)
{
    QString body;
    for (const QString &record : recordXmls)
        body += QStringLiteral("<sfLR:RegistroFactura>") + record + QStringLiteral("</sfLR:RegistroFactura>");
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                          "<soapenv:Envelope xmlns:soapenv=\"%1\" xmlns:sfLR=\"%2\" xmlns:sf=\"%3\">"
                          "<soapenv:Header/><soapenv:Body><sfLR:RegFactuSistemaFacturacion>"
                          "<sfLR:Cabecera><sf:ObligadoEmision><sf:NombreRazon>%4</sf:NombreRazon>"
                          "<sf:NIF>%5</sf:NIF></sf:ObligadoEmision></sfLR:Cabecera>%6"
                          "</sfLR:RegFactuSistemaFacturacion></soapenv:Body></soapenv:Envelope>")
        .arg(QString::fromLatin1(kSoapNs), QString::fromLatin1(kSuministroLRNs),
             QString::fromLatin1(kSuministroInformacionNs), escaped(issuerName), escaped(issuerNif), body);
}

QString queryEnvelope(const QString &issuerName, const QString &issuerNif, int year, int month,
                      const QString &invoiceNumber, const InvoiceRef &pageAfter)
{
    QString number = invoiceNumber.trimmed().isEmpty()
        ? QString()
        : QStringLiteral("<sfLRC:NumSerieFactura>%1</sfLRC:NumSerieFactura>").arg(escaped(invoiceNumber));
    // Next page: the last record of the previous one (ClavePaginacion).
    if (!pageAfter.invoiceNumber.isEmpty())
        number += QStringLiteral("<sfLRC:ClavePaginacion><sf:IDEmisorFactura>%1</sf:IDEmisorFactura>"
                                 "<sf:NumSerieFactura>%2</sf:NumSerieFactura><sf:FechaExpedicionFactura>%3"
                                 "</sf:FechaExpedicionFactura></sfLRC:ClavePaginacion>")
                      .arg(escaped(pageAfter.issuerNif), escaped(pageAfter.invoiceNumber), dateText(pageAfter.issueDate));
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                          "<soapenv:Envelope xmlns:soapenv=\"%1\" xmlns:sfLRC=\"%2\" xmlns:sf=\"%3\">"
                          "<soapenv:Header/><soapenv:Body><sfLRC:ConsultaFactuSistemaFacturacion>"
                          "<sfLRC:Cabecera><sf:IDVersion>1.0</sf:IDVersion><sf:ObligadoEmision>"
                          "<sf:NombreRazon>%4</sf:NombreRazon><sf:NIF>%5</sf:NIF></sf:ObligadoEmision>"
                          "</sfLRC:Cabecera><sfLRC:FiltroConsulta><sfLRC:PeriodoImputacion>"
                          "<sf:Ejercicio>%6</sf:Ejercicio><sf:Periodo>%7</sf:Periodo>"
                          "</sfLRC:PeriodoImputacion>%8</sfLRC:FiltroConsulta>"
                          "</sfLRC:ConsultaFactuSistemaFacturacion></soapenv:Body></soapenv:Envelope>")
        .arg(QString::fromLatin1(kSoapNs), QString::fromLatin1(kConsultaLRNs),
             QString::fromLatin1(kSuministroInformacionNs), escaped(issuerName), escaped(issuerNif),
             QString::number(year), QStringLiteral("%1").arg(month, 2, 10, QLatin1Char('0')), number);
}

} // namespace AeatRecord
