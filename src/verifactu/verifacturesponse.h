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

// Decode a base64-encoded image (a QR / GetQrCode payload) into a QPixmap.
// Constructing a QPixmap needs a QGuiApplication, so tests that exercise this
// path must use a GUI test main (offscreen platform), not a guiless one.
QPixmap decodeVerifactuImageBase64(const QString &base64);

#endif // VERIFACTURESPONSE_H
