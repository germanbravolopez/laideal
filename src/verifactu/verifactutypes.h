#ifndef VERIFACTUTYPES_H
#define VERIFACTUTYPES_H

#include <cmath>
#include <QDebug>
#include <QPixmap>
#include <QString>

// Persisted value of the verifactu_estado column in the ingresos table.
enum class VerifactuEstado {
    Unpaid,        // "SIN COBRAR"  - unpaid, so there is no invoice to submit yet
    NotSubmitted,  // "PENDIENTE"   - paid and due at AEAT: submitted and awaiting a reply, or not yet sent
    Enviada,       // "ENVIADA"     - successfully submitted to AEAT
    Anulada,       // "ANULADA"     - cancelled via AEAT
    Rectificada,   // "RECTIFICADA" - superseded by a substitution rectificativa (R1-R5 with S)
    Error,         // "ERROR"       - submission or cancellation failed
    Unknown        // any other stored text: never resent, cancelled or rectified
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
    case VerifactuEstado::Unknown:      return QStringLiteral("DESCONOCIDO");
    }
    return QStringLiteral("PENDIENTE");
}

// Case-insensitive on purpose (a mis-cased "Error" from older versions); writers still
// emit the canonical form, the SQL filters compare case-sensitively.
inline VerifactuEstado verifactuEstadoFromString(const QString &s)
{
    const QString u = s.toUpper();
    if (u == QLatin1String("SIN COBRAR"))  return VerifactuEstado::Unpaid;
    if (u == QLatin1String("ENVIADA"))     return VerifactuEstado::Enviada;
    if (u == QLatin1String("ANULADA"))     return VerifactuEstado::Anulada;
    if (u == QLatin1String("RECTIFICADA")) return VerifactuEstado::Rectificada;
    if (u == QLatin1String("ERROR"))       return VerifactuEstado::Error;
    if (u.isEmpty() || u == QLatin1String("PENDIENTE"))   // legacy empty / NULL too
        return VerifactuEstado::NotSubmitted;
    // Read as PENDIENTE it would be resent: an invoice AEAT may hold, sent twice.
    qWarning() << "verifactuEstadoFromString: unknown verifactu_estado" << s << "- treated as not resendable";
    return VerifactuEstado::Unknown;
}

// The estado as the operator should read it: an unknown value is shown as stored.
inline QString verifactuEstadoDisplay(const QString &stored)
{
    const VerifactuEstado e = verifactuEstadoFromString(stored);
    return e == VerifactuEstado::Unknown ? stored : verifactuEstadoToString(e);
}

// True for the two states that mean "AEAT has not accepted this row (yet)": paying a
// garment does not rewrite verifactu_estado, so a paid row can still be Unpaid.
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
    QString rawXml; // AEAT-style XML payload (Return.Xml from Irene Solutions /Create reply)
    // 64-char hex SHA-256 chained hash extracted from <sum1:Huella> in rawXml
    // (AEAT regulatory term: "Huella"). Art. 12 RD 1007/2023.
    QString rawHash;

    VerifactuResult() : status(INVALID_CONFIG) {}

    bool isSuccess() const { return status == SUCCESS; }
};

// One invoice record as the gateway holds it (GetFilteredList), to reconcile a local
// row. Nothing may be written on the strength of it alone: it must pass
// verifactuRemoteMatches() and carry a non-empty csv.
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
    bool    isRejected = false; // AEAT's own IsRejected flag on the record
    QString errorCode;          // non-empty when AEAT recorded a rejection
    // Records returned for this InvoiceID (every attempt is kept); the fields above come
    // from the accepted one.
    int     recordCount = 0;
    QString raw;                // the whole JSON payload, always kept

    // Safe to reconcile from only when AEAT really has it, accepted, with a CSV.
    bool hasUsableCsv() const
    {
        return found && parsed && !csv.isEmpty() && !isRejected && errorCode.isEmpty();
    }
};

// Which estado an AEAT reply must be recorded as. Only a definitive AEAT
// rejection (ERROR) may become Error. A timeout or transport failure leaves the
// outcome UNKNOWN - AEAT may well have registered the invoice - so it becomes
// PENDIENTE and is routed to the startup recovery dialog. Recording it as Error
// instead offers a "Reintentar" that can only ever come back "already exists".
inline VerifactuEstado verifactuEstadoForResult(VerifactuResult::Status s)
{
    switch (s) {
    case VerifactuResult::SUCCESS: return VerifactuEstado::Enviada;
    case VerifactuResult::ERROR:   return VerifactuEstado::Error;
    // NETWORK_ERROR / PENDING / INVALID_CONFIG: unknown or never sent.
    default:                       return VerifactuEstado::NotSubmitted;
    }
}

// A gross amount (IVA included) split the way it is invoiced and printed: the base rounded
// to cents (half away from zero) and the tax the rest, so base + tax is always the gross.
struct GrossSplit {
    double base = 0.0;
    double tax = 0.0;
};
inline GrossSplit splitGross(double gross, double ratePercent)
{
    const auto cents = [](double v) {
        const double c = v * 100.0;
        return std::round(c + (c >= 0.0 ? 1e-6 : -1e-6)) / 100.0;
    };
    const double g = cents(gross);
    const double base = cents(g / (1.0 + ratePercent / 100.0));
    return { base, cents(g - base) };
}

#endif // VERIFACTUTYPES_H
