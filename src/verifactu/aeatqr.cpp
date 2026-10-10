#include "aeatqr.h"

#include "qrcodegen.hpp"

#include <QPainter>

namespace AeatQr {

namespace {

const int kQuietZone = 4;

qrcodegen::QrCode encode(const QString &text)
{
    return qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
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
