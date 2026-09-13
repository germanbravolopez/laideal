#ifndef VERIFACTURESPONSE_H
#define VERIFACTURESPONSE_H

#include <QByteArray>
#include <QPixmap>
#include <QString>

#include "verifactutypes.h"

// Pure parser for an Irene Solutions / AEAT JSON reply, extracted from
// VerifactuManager so it can be unit-tested without a live network or a
// VerifactuManager instance (it depends only on its inputs - no member state).
//
// isQrRequest = true means the reply is a GetQrCode response whose "Return" is
// the base64 image directly; otherwise "Return" is the invoice result object
// (CSV / ValidationUrl / Xml / QrCode, or an invoice-level ErrorCode).
VerifactuResult parseVerifactuResponse(const QByteArray &response, bool isQrRequest = false);

// Pure parser for a GetFilteredList (invoice query) reply. The vendor does not
// publish this response schema - its own client parses it untyped - so this
// accepts the record under several plausible envelopes ("Return", "Records",
// "List", a bare array) and each field under several plausible names, and always
// keeps the payload in `raw`. A record it cannot decode comes back parsed=false
// rather than half-filled, so the caller shows the raw JSON instead of acting.
// Never write to the DB from this alone - see verifactuRemoteMatches().
VerifactuRemoteRecord parseVerifactuQueryResponse(const QByteArray &response);

// True when a queried record is beyond reasonable doubt the same invoice as the
// local row: same InvoiceID, same issue date, and the same total to the cent.
// This is the gate that lets an "already exists" rejection be reconciled - a
// mismatch on any field means we are looking at a different invoice and the
// local row must be left exactly as it is.
bool verifactuRemoteMatches(const VerifactuRemoteRecord &remote,
                            const QString &localInvoiceId,
                            const QString &localDateDdMmYyyy,
                            double localTotalAmount);

// True when an AEAT rejection means "this invoice is already registered". That
// rejection is good news dressed as an error: the invoice IS at AEAT, we simply
// lost the reply, so the row can be reconciled instead of retried forever.
// Matched on wording because the vendor does not publish a stable code for it.
bool verifactuErrorIsDuplicate(const QString &errorCode, const QString &errorDescription);

// Decode a base64-encoded image (a QR / GetQrCode payload) into a QPixmap.
// Constructing a QPixmap needs a QGuiApplication, so tests that exercise this
// path must use a GUI test main (offscreen platform), not a guiless one.
QPixmap decodeVerifactuImageBase64(const QString &base64);

#endif // VERIFACTURESPONSE_H
