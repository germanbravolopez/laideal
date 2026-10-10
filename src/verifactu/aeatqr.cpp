#include "aeatqr.h"

#include "qrcodegen.hpp"

#include <QPainter>

namespace AeatQr {

namespace {

const int kQuietZone = 4;

qrcodegen::QrCode encode(const QString &text)
{
    // Exactly level M, as the AEAT QR specification asks (no automatic boost).
    const std::vector<qrcodegen::QrSegment> segments = qrcodegen::QrSegment::makeSegments(text.toUtf8().constData());
    return qrcodegen::QrCode::encodeSegments(segments, qrcodegen::QrCode::Ecc::MEDIUM, qrcodegen::QrCode::MIN_VERSION,
                                             qrcodegen::QrCode::MAX_VERSION, -1, false);
}

} // namespace

int moduleCount(const QString &text)
{
    return encode(text).getSize();
}

QImage image(const QString &text, int pixelsPerModule)
{
    const qrcodegen::QrCode qr = encode(text);
    const int side = (qr.getSize() + 2 * kQuietZone) * pixelsPerModule;
    QImage out(side, side, QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter p(&out);
    for (int y = 0; y < qr.getSize(); ++y) {
        for (int x = 0; x < qr.getSize(); ++x) {
            if (qr.getModule(x, y))
                p.fillRect((x + kQuietZone) * pixelsPerModule, (y + kQuietZone) * pixelsPerModule,
                           pixelsPerModule, pixelsPerModule, Qt::black);
        }
    }
    return out;
}

} // namespace AeatQr
