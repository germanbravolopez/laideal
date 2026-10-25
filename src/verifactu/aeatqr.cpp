#include "aeatqr.h"

#include "qrcodegen.hpp"

#include <QDebug>
#include <QPainter>

#include <optional>
#include <stdexcept>

namespace AeatQr {

namespace {

const int kQuietZone = 4;

// Empty when the text does not fit a symbol: qrcodegen throws, which would end the app.
std::optional<qrcodegen::QrCode> encode(const QString &text)
{
    try {
        // Exactly level M, as the AEAT QR specification asks (no automatic boost).
        const std::vector<qrcodegen::QrSegment> segments = qrcodegen::QrSegment::makeSegments(text.toUtf8().constData());
        return qrcodegen::QrCode::encodeSegments(segments, qrcodegen::QrCode::Ecc::MEDIUM, qrcodegen::QrCode::MIN_VERSION,
                                                 qrcodegen::QrCode::MAX_VERSION, -1, false);
    } catch (const std::exception &e) {
        qWarning() << "AeatQr: text of" << text.size() << "characters not encoded -" << e.what();
        return std::nullopt;
    }
}

} // namespace

int moduleCount(const QString &text)
{
    const std::optional<qrcodegen::QrCode> qr = encode(text);
    return qr ? qr->getSize() : 0;
}

QImage image(const QString &text, int pixelsPerModule)
{
    const std::optional<qrcodegen::QrCode> symbol = encode(text);
    if (!symbol)
        return QImage();
    const qrcodegen::QrCode &qr = *symbol;
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
