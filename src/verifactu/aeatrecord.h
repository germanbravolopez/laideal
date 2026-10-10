#ifndef AEATRECORD_H
#define AEATRECORD_H

// The XML a direct AEAT client sends (AEAT SuministroLR.xsd / SuministroInformacion.xsd /
// ConsultaLR.xsd, VERI*FACTU mode: records are not signed): the registration
// (RegistroAlta) and cancellation (RegistroAnulacion) records with their chained
// hash, the SOAP envelope that carries them (RegFactuSistemaFacturacion), and the
// query envelope (ConsultaFactuSistemaFacturacion). Pure functions: no network, no DB.
// See docs/modules/verifactu/aeat-direct-investigation.md.

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace AeatRecord {

// The invoicing system block every record carries (SistemaInformatico).
struct SystemInfo {
    QString producerName;          // NombreRazon of the producer (declaración responsable)
    QString producerNif;           // NIF of the producer
    QString systemName;            // NombreSistemaInformatico, max 30
    QString systemId;              // IdSistemaInformatico, max 2
    QString version;               // Version, max 50
    QString installationNumber;    // NumeroInstalacion, max 100, one per installation
};

// One invoice as AEAT identifies it.
struct InvoiceRef {
    QString issuerNif;
    QString invoiceNumber;
    QDate   issueDate;
};

// The issuer's previous record in the chain; an empty hash means this is the first.
struct PreviousRecord {
    InvoiceRef invoice;
    QString    hash;
    bool isFirst() const { return hash.isEmpty(); }
};

// One VAT line of the breakdown (Desglose / DetalleDesglose).
struct TaxLine {
    double rate = 0.0;             // TipoImpositivo, percent
    double base = 0.0;             // BaseImponibleOimporteNoSujeto (signed)
    double tax  = 0.0;             // CuotaRepercutida (signed)
};

// A registration record (RegistroAlta).
struct Registration {
    InvoiceRef        invoice;
    QString           issuerName;               // NombreRazonEmisor
    QString           invoiceType;              // F2 simplified; R5 rectification of a simplified one
    QString           rectificationType;        // R types: "S" substitution, "I" differences
    QList<InvoiceRef> rectifiedInvoices;        // FacturasRectificadas (R types)
    bool              hasRectifiedAmounts = false;   // ImporteRectificacion (substitution)
    double            rectifiedBase = 0.0;
    double            rectifiedTax  = 0.0;
    QString           description;              // DescripcionOperacion, max 500
    QList<TaxLine>    taxLines;
    // Sent again after AEAT rejected an earlier record of the same invoice
    // (Subsanacion = S, RechazoPrevio = S).
    bool              afterRejection = false;
    PreviousRecord    previous;
    QDateTime         generatedAt;              // FechaHoraHusoGenRegistro

    double totalTax() const;
    double totalAmount() const;
};

// A cancellation record (RegistroAnulacion).
struct Cancellation {
    InvoiceRef     invoice;
    bool           afterRejection = false;      // RechazoPrevio = S
    PreviousRecord previous;
    QDateTime      generatedAt;
};

// The record hash (huella), from AeatHash with the amounts as the record writes them.
QString hashOf(const Registration &record);
QString hashOf(const Cancellation &record);

// The record as a standalone XML element (it declares its own namespace prefix), with
// its hash. This exact text is stored and sent; a resend reuses it unchanged.
QString registrationXml(const Registration &record, const SystemInfo &system);
QString cancellationXml(const Cancellation &record, const SystemInfo &system);

// SOAP 1.1 envelope of a submission: header (the issuer) and 1-1000 records.
QString submissionEnvelope(const QString &issuerName, const QString &issuerNif, const QStringList &recordXmls);

// SOAP 1.1 envelope of a query for the issuer's records of one month, optionally one invoice.
QString queryEnvelope(const QString &issuerName, const QString &issuerNif, int year, int month,
                      const QString &invoiceNumber = QString());

// Namespaces of the AEAT schemas (exposed for the response parser and the tests).
extern const char *const kSuministroInformacionNs;
extern const char *const kSuministroLRNs;
extern const char *const kConsultaLRNs;

} // namespace AeatRecord

#endif // AEATRECORD_H
