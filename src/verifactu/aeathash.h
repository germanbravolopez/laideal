#ifndef AEATHASH_H
#define AEATHASH_H

// Pieces of a VERI*FACTU record the app computes itself when it talks to AEAT
// directly (see docs/modules/verifactu/aeat-direct-investigation.md): the chained
// record hash ("huella", AEAT "Especificaciones técnicas para la generación de la
// huella o hash" v0.1.2) and the QR verification URL (AEAT QR specification v0.5.0).
// Pure functions: no network, no database.

#include <QDate>
#include <QDateTime>
#include <QString>

namespace AeatHash {

// Fields of a registration record (RegistroAlta) that enter its hash.
struct RegistrationFields {
    QString issuerNif;        // IDEmisorFactura
    QString invoiceNumber;    // NumSerieFactura
    QDate   issueDate;        // FechaExpedicionFactura
    QString invoiceType;      // TipoFactura: F2 simplified, R5 rectification of a simplified one
    QString totalTax;         // CuotaTotal, as written in the record (amountText)
    QString totalAmount;      // ImporteTotal, as written in the record (amountText)
    QString previousHash;     // Huella of the previous record of the issuer; empty for the first
    QString generatedAt;      // FechaHoraHusoGenRegistro (timestampText)
};

// Fields of a cancellation record (RegistroAnulacion) that enter its hash.
struct CancellationFields {
    QString issuerNif;        // IDEmisorFacturaAnulada
    QString invoiceNumber;    // NumSerieFacturaAnulada
    QDate   issueDate;        // FechaExpedicionFacturaAnulada
    QString previousHash;     // Huella of the previous record of the issuer
    QString generatedAt;      // FechaHoraHusoGenRegistro (timestampText)
};

// The text that is hashed: name=value pairs joined by '&', each value trimmed, an
// empty value still written ("Huella="), dates dd-MM-yyyy, nothing URL-encoded.
QString registrationHashInput(const RegistrationFields &fields);
QString cancellationHashInput(const CancellationFields &fields);

// SHA-256 of the UTF-8 hash input, upper-case hexadecimal (64 characters).
QString registrationHash(const RegistrationFields &fields);
QString cancellationHash(const CancellationFields &fields);

// Amount as the record writes it: two decimals, '.' separator ("123.40", never "123.4").
QString amountText(double amount);

// Generation time with its UTC offset, "yyyy-MM-ddTHH:mm:ss+hh:mm" (never "Z").
QString timestampText(const QDateTime &moment);

// QR verification URL of a VERI*FACTU invoice: nif, numserie, fecha (dd-MM-yyyy)
// and importe, each value percent-encoded in UTF-8; the AEAT test host when
// `testEnvironment`.
QString qrValidationUrl(const QString &issuerNif, const QString &invoiceNumber, const QDate &issueDate,
                        const QString &totalAmount, bool testEnvironment);

} // namespace AeatHash

#endif // AEATHASH_H
