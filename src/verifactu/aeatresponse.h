#ifndef AEATRESPONSE_H
#define AEATRESPONSE_H

// Replies of the AEAT VERI*FACTU web service to the direct client (AEAT
// RespuestaSuministro.xsd / RespuestaConsultaLR.xsd, inside a SOAP envelope, or a
// SOAP Fault when the request itself is refused), and how each maps onto the app's
// VerifactuResult / VerifactuRemoteRecord. Elements are read by local name, so the
// namespace prefixes AEAT happens to use do not matter. Pure functions.

#include <QByteArray>
#include <QList>
#include <QString>

#include "verifactutypes.h"

namespace AeatResponse {

// One record's outcome in a submission reply (RespuestaLinea).
struct Line {
    QString issuerNif;
    QString invoiceNumber;
    QString issueDate;            // dd-MM-yyyy as AEAT returns it
    QString operation;            // Alta / Anulacion
    QString state;                // Correcto / AceptadoConErrores / Incorrecto
    QString errorCode;
    QString errorDescription;
    bool    duplicate = false;    // RegistroDuplicado: AEAT already holds this record
    QString duplicateRequestId;   // IdPeticionRegistroDuplicado
    QString duplicateState;       // Correcta / AceptadaConErrores / Anulada
};

// A submission reply (RespuestaRegFactuSistemaFacturacion).
struct Submission {
    bool    parsed = false;       // a reply or a fault was understood
    bool    fault = false;        // SOAP Fault: the whole request was refused
    QString faultCode;
    QString faultText;
    QString csv;                  // the submission's CSV (absent when nothing was accepted)
    QString presentedAt;          // TimestampPresentacion
    int     waitSeconds = 60;     // TiempoEsperaEnvio before the next submission
    QString sendState;            // Correcto / ParcialmenteCorrecto / Incorrecto
    QList<Line> lines;

    // The line of one invoice and operation, or nullptr.
    const Line *lineFor(const QString &invoiceNumber, const QString &operation) const;
};

// One record of a query reply (RegistroRespuestaConsultaFactuSistemaFacturacion).
struct QueryRecord {
    QString issuerNif;
    QString invoiceNumber;
    QString issueDate;            // dd-MM-yyyy
    QString invoiceType;
    QString totalAmount;          // ImporteTotal as written
    QString hash;                 // the record's own Huella
    QString state;                // Correcto / AceptadoConErrores / Anulado
    QString errorCode;
    QString errorDescription;
    QString requestId;            // IdPeticion of the submission that registered it
    QString presentedAt;
};

struct Query {
    bool    parsed = false;
    bool    fault = false;
    QString faultText;
    bool    hasData = false;      // ResultadoConsulta = ConDatos
    bool    morePages = false;    // IndicadorPaginacion = S
    QList<QueryRecord> records;
};

Submission parseSubmission(const QByteArray &body);
Query parseQuery(const QByteArray &body);

// The app's result for one record of a submission. Correcto and AceptadoConErrores
// are accepted (the warning text is kept in errorDescription); a duplicate whose
// earlier copy AEAT accepted is accepted too, identified by the earlier request
// (AEAT gives no new CSV for it); Incorrecto is a definitive rejection. A fault, or a
// reply without a line for the record, is an ERROR carrying the fault text.
VerifactuResult resultFor(const Submission &reply, const QString &invoiceNumber, const QString &operation);

// What AEAT holds for an invoice number, as the reconciliation dialogs read it. The
// query returns no CSV, so `csv` carries the IdPeticion of the accepted submission.
VerifactuRemoteRecord remoteRecordFor(const Query &reply, const QString &invoiceNumber);

} // namespace AeatResponse

#endif // AEATRESPONSE_H
