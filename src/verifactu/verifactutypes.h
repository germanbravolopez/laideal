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

#endif // VERIFACTUTYPES_H
