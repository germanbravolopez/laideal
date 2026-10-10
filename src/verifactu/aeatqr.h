#ifndef AEATQR_H
#define AEATQR_H

// The Verifactu QR drawn locally by the direct AEAT client (the gateway returned
// it as an image): the verification URL (AeatHash::qrValidationUrl) encoded at
// error-correction level M, as the AEAT QR specification requires, with the
// standard 4-module quiet zone. Black modules on white.

#include <QImage>
#include <QString>

namespace AeatQr {

// Modules per side of the symbol for `text` (21 for version 1, +4 per version).
int moduleCount(const QString &text);

// The symbol as an image of `pixelsPerModule` pixels per module, quiet zone included.
QImage image(const QString &text, int pixelsPerModule = 4);

} // namespace AeatQr

#endif // AEATQR_H
