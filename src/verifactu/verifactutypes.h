#ifndef VERIFACTUTYPES_H
#define VERIFACTUTYPES_H

#include <QPixmap>
#include <QString>

// Persisted value of the verifactu_estado column in the ingresos table.
enum class VerifactuEstado {
    Unpaid,        // "SIN COBRAR"  - unpaid, so there is no invoice to submit yet
    NotSubmitted,  // "PENDIENTE"   - paid and due at AEAT: submitted and awaiting a reply, or not yet sent
    Enviada,       // "ENVIADA"     - successfully submitted to AEAT
    Anulada,       // "ANULADA"     - cancelled via AEAT
    Rectificada,   // "RECTIFICADA" - superseded by a substitution rectificativa (R1-R5 with S)
    Error          // "ERROR"       - submission or cancellation failed
};

inline QString verifactuEstadoToString(VerifactuEstado e)
{
    switch (e) {
    case VerifactuEstado::Unpaid:       return QStringLiteral("SIN COBRAR");
    case VerifactuEstado::NotSubmitted: return QStringLiteral("PENDIENTE");
    case VerifactuEstado::Enviada:      return QStringLiteral("ENVIADA");
    case VerifactuEstado::Anulada:      return QStringLiteral("ANULADA");
    case VerifactuEstado::Rectificada:  return QStringLiteral("RECTIFICADA");
    case VerifactuEstado::Error:        return QStringLiteral("ERROR");
    }
    return QStringLiteral("PENDIENTE");
}

inline VerifactuEstado verifactuEstadoFromString(const QString &s)
{
    if (s == QLatin1String("SIN COBRAR"))  return VerifactuEstado::Unpaid;
    if (s == QLatin1String("ENVIADA"))     return VerifactuEstado::Enviada;
    if (s == QLatin1String("ANULADA"))     return VerifactuEstado::Anulada;
    if (s == QLatin1String("RECTIFICADA")) return VerifactuEstado::Rectificada;
    if (s == QLatin1String("ERROR"))       return VerifactuEstado::Error;
    return VerifactuEstado::NotSubmitted; // covers "PENDIENTE" and legacy empty/NULL
}

// True for the two states that mean "AEAT has not accepted this row (yet)".
// Every gate that used to test `== NotSubmitted` must use this instead: paying a
// garment does not rewrite verifactu_estado, so a row is still Unpaid at the
// moment RecogPrendas decides whether to submit it - testing NotSubmitted alone
// would silently stop paid garments from reaching AEAT.
inline bool verifactuEstadoIsUnsubmitted(VerifactuEstado e)
{
    return e == VerifactuEstado::Unpaid || e == VerifactuEstado::NotSubmitted;
}

struct VerifactuResult
{
    enum Status {
        SUCCESS,
        PENDING,
        ERROR,
        NETWORK_ERROR,
        INVALID_CONFIG
    };

    Status status;
    QString csv;
    QString errorCode;
    QString errorDescription;
    QString validationUrl;
    QPixmap qrCode;
    QString rawResponse;
    QString rawXml; // AEAT-style XML payload (Return.Xml from Irene Solutions /Create reply)
    // 64-char hex SHA-256 chained hash extracted from <sum1:Huella> in rawXml
    // (AEAT regulatory term: "Huella"). Art. 12 RD 1007/2023.
    QString rawHash;

    VerifactuResult() : status(INVALID_CONFIG) {}

    bool isSuccess() const { return status == SUCCESS; }
    bool isError() const { return status == ERROR || status == NETWORK_ERROR || status == INVALID_CONFIG; }
};

// Which estado an AEAT reply must be recorded as. Only a definitive AEAT
// rejection (ERROR) may become Error. A timeout or transport failure leaves the
// outcome UNKNOWN - AEAT may well have registered the invoice - so it becomes
// PENDIENTE and is routed to the startup recovery dialog. Recording it as Error
// instead offers a "Reintentar" that can only ever come back "already exists".
// One invoice record as AEAT/Irene Solutions holds it, from a GetFilteredList
// query. Used to reconcile a local row that says ERROR (typically "already
// exists" after a retry) against what the AEAT side actually registered.
//
// The response schema of GetFilteredList is not published - the vendor's own
// client parses it untyped - so the parser is deliberately tolerant about field
// names and `raw` always keeps the payload for diagnosis. Nothing may be written
// to the DB on the strength of this record alone: it must first pass
// verifactuRemoteMatches() AND carry a non-empty csv.
struct VerifactuRemoteRecord
{
    bool    found = false;      // the query returned a record for this InvoiceID
    bool    parsed = false;     // the payload was understood (a record was decoded)
    QString invoiceId;
    QString invoiceDate;        // as returned, normalised to dd-MM-yyyy when possible
    double  totalAmount = 0.0;
    QString csv;
    QString statusResponse;
    QString validationUrl;
    QString raw;                // the whole JSON payload, always kept

    // Safe to reconcile from only when AEAT really has it with a CSV.
    bool hasUsableCsv() const { return found && parsed && !csv.isEmpty(); }
};

inline VerifactuEstado verifactuEstadoForResult(VerifactuResult::Status s)
{
    switch (s) {
    case VerifactuResult::SUCCESS: return VerifactuEstado::Enviada;
    case VerifactuResult::ERROR:   return VerifactuEstado::Error;
    // NETWORK_ERROR / PENDING / INVALID_CONFIG: unknown or never sent.
    default:                       return VerifactuEstado::NotSubmitted;
    }
}

#endif // VERIFACTUTYPES_H
