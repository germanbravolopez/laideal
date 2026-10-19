#ifndef ESCPOSPREVIEW_H
#define ESCPOSPREVIEW_H

// Reads back the ESC/POS bytes TicketRenderer sends to the printer: the subset
// EscPosBuilder emits (align / bold / font / size / raster / cut / feed / text,
// PC858 decoded to Unicode) as blocks, drawn as a PNG of the thermal paper or an
// ASCII mock. Shared by test_ticket_preview and the escpos_preview tool, which
// decodes what the UI smoke test's FILE-port printer captured.

#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QStringList>
#include <QVector>

#include "escposbuilder.h"

namespace EscPosPreview {

// Inverse of EscPosBuilder::toCp858 for the glyphs the tickets use, so the
// printed bytes can be shown back as readable text.
inline QChar cp858ToUnicode(uchar b)
{
    if (b < 0x80) return QChar(b);
    static const QHash<uchar, ushort> m = {
        {0x80,0x00C7},{0x81,0x00FC},{0x82,0x00E9},{0x83,0x00E2},{0x84,0x00E4},
        {0x85,0x00E0},{0x87,0x00E7},{0x88,0x00EA},{0x89,0x00EB},{0x8A,0x00E8},
        {0x8B,0x00EF},{0x8C,0x00EE},{0x8D,0x00EC},{0x8E,0x00C4},{0x90,0x00C9},
        {0x93,0x00F4},{0x94,0x00F6},{0x95,0x00F2},{0x96,0x00FB},{0x97,0x00F9},
        {0x99,0x00D6},{0x9A,0x00DC},{0x9C,0x00A3},{0xA0,0x00E1},{0xA1,0x00ED},
        {0xA2,0x00F3},{0xA3,0x00FA},{0xA4,0x00F1},{0xA5,0x00D1},{0xA6,0x00AA},
        {0xA7,0x00BA},{0xA8,0x00BF},{0xAA,0x00AC},{0xAB,0x00BD},{0xAC,0x00BC},
        {0xAD,0x00A1},{0xAE,0x00AB},{0xAF,0x00BB},{0xB5,0x00C1},{0xB6,0x00C2},
        {0xB7,0x00C0},{0xB8,0x00A9},{0xC6,0x00E3},{0xC7,0x00C3},{0xD2,0x00CA},
        {0xD3,0x00CB},{0xD4,0x00C8},{0xD5,0x20AC},{0xD6,0x00CD},{0xD7,0x00CE},
        {0xD8,0x00CF},{0xDE,0x00CC},{0xE0,0x00D3},{0xE2,0x00D4},{0xE3,0x00D2},
        {0xE5,0x00D5},{0xE9,0x00DA},{0xEA,0x00DB},{0xEB,0x00D9},{0xED,0x00DD},
        {0xEF,0x00B4},{0xF8,0x00B0},{0xFA,0x00B7},
    };
    const auto it = m.constFind(b);
    return it != m.constEnd() ? QChar(it.value()) : QChar('?');
}

struct PreviewBlock {
    enum Kind { Text, Cut, Raster } kind = Text;
    QString text;
    EscPosBuilder::Align align = EscPosBuilder::Align::Left;
    bool bold = false;
    EscPosBuilder::Font font = EscPosBuilder::Font::A;
    bool dbl = false;
    QImage raster;
};

inline uchar at(const QByteArray &d, int i) { return i < d.size() ? static_cast<uchar>(d[i]) : 0; }

// Parse the ESC/POS subset EscPosBuilder emits into a flat list of blocks.
inline QVector<PreviewBlock> interpret(const QByteArray &d)
{
    QVector<PreviewBlock> out;
    EscPosBuilder::Align align = EscPosBuilder::Align::Left;
    bool bold = false;
    EscPosBuilder::Font font = EscPosBuilder::Font::A;
    bool dbl = false;
    QByteArray line;

    auto flush = [&](bool force) {
        if (line.isEmpty() && !force) return;
        PreviewBlock b;
        b.align = align; b.bold = bold; b.font = font; b.dbl = dbl;
        for (char c : line) b.text.append(cp858ToUnicode(static_cast<uchar>(c)));
        out.append(b);
        line.clear();
    };

    int i = 0;
    const int n = d.size();
    while (i < n) {
        const uchar c = at(d, i);
        if (c == 0x1B) {                                  // ESC
            switch (at(d, i + 1)) {
            case '@': align = EscPosBuilder::Align::Left; bold = false;
                      font = EscPosBuilder::Font::A; dbl = false; i += 2; break;
            case 't': i += 3; break;                       // ESC t n (code page)
            case 'a': align = static_cast<EscPosBuilder::Align>(at(d, i + 2)); i += 3; break;
            case 'E': bold = at(d, i + 2) != 0; i += 3; break;
            case 'M': font = static_cast<EscPosBuilder::Font>(at(d, i + 2)); i += 3; break;
            case '2': i += 2; break;                       // ESC 2 (default line spacing)
            case '3': i += 3; break;                       // ESC 3 n (line spacing)
            case 'd': { const int k = at(d, i + 2); i += 3; flush(false);
                        for (int j = 0; j < k; ++j) { PreviewBlock b; b.align = align; b.font = font; out.append(b); }
                        break; }
            default: i += 2; break;
            }
        } else if (c == 0x1D) {                            // GS
            const uchar f = at(d, i + 1);
            if (f == '!') { dbl = at(d, i + 2) != 0; i += 3; }
            else if (f == 'V') { flush(false); PreviewBlock b; b.kind = PreviewBlock::Cut; out.append(b);
                                 i += (at(d, i + 2) >= 48 ? 4 : 3); }   // GS V 66 n vs GS V m
            else if (f == 'v') {                           // GS v 0 m xL xH yL yH <data>
                const int widthBytes = at(d, i + 4) + at(d, i + 5) * 256;
                const int h = at(d, i + 6) + at(d, i + 7) * 256;
                const int len = widthBytes * h;
                const QByteArray data = d.mid(i + 8, len);
                flush(false);
                PreviewBlock b; b.kind = PreviewBlock::Raster;
                const int w = widthBytes * 8;
                QImage img(w, h, QImage::Format_RGB32);
                img.fill(qRgb(255, 255, 255));
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        if (static_cast<uchar>(data[y * widthBytes + (x >> 3)]) & (0x80 >> (x & 7)))
                            img.setPixel(x, y, qRgb(0, 0, 0));
                b.raster = img;
                out.append(b);
                i += 8 + len;
            } else i += 2;
        } else if (c == 0x0A) { flush(true); i++; }         // LF -> end of line
        else { line.append(static_cast<char>(c)); i++; }   // printable byte
    }
    flush(false);
    return out;
}

// Render the blocks to a QImage simulating the thermal paper.
inline QImage toImage(const QVector<PreviewBlock> &blocks, int paperDots)
{
    const int margin = 12;
    const int width = paperDots + 2 * margin;
    const QFont base = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    auto fontFor = [&](const PreviewBlock &b) {
        QFont f = base;
        int px = (b.font == EscPosBuilder::Font::B) ? 15 : 19;
        if (b.dbl) px *= 2;
        f.setPixelSize(px);
        f.setBold(b.bold);
        return f;
    };

    int totalH = margin * 2;
    for (const auto &b : blocks) {
        if (b.kind == PreviewBlock::Raster)   totalH += b.raster.height() + 10;
        else if (b.kind == PreviewBlock::Cut) totalH += 18;
        else                                  totalH += QFontMetrics(fontFor(b)).height();
    }

    QImage img(width, totalH, QImage::Format_RGB32);
    img.fill(qRgb(255, 255, 255));
    QPainter p(&img);
    int y = margin;
    for (const auto &b : blocks) {
        if (b.kind == PreviewBlock::Raster) {
            p.drawImage((width - b.raster.width()) / 2, y, b.raster);
            y += b.raster.height() + 10;
        } else if (b.kind == PreviewBlock::Cut) {
            p.setPen(QPen(Qt::gray, 1, Qt::DashLine));
            p.drawLine(margin, y + 8, width - margin, y + 8);
            y += 18;
        } else {
            const QFont f = fontFor(b);
            p.setFont(f);
            const int h = QFontMetrics(f).height();
            const int flag = (b.align == EscPosBuilder::Align::Center) ? Qt::AlignHCenter
                           : (b.align == EscPosBuilder::Align::Right)  ? Qt::AlignRight
                                                                       : Qt::AlignLeft;
            p.setPen(Qt::black);
            p.drawText(QRect(margin, y, paperDots, h), flag | Qt::AlignVCenter, b.text);
            y += h;
        }
    }
    p.end();
    return img;
}

// Render the blocks to an ASCII mock (font sizes / bold not represented).
inline QString toAscii(const QVector<PreviewBlock> &blocks, int paperDots)
{
    const int W = paperDots / 12;                 // Font A columns
    QStringList lines;
    for (const auto &b : blocks) {
        if (b.kind == PreviewBlock::Raster) {
            const QString tag = QStringLiteral("[QR %1x%2]").arg(b.raster.width()).arg(b.raster.height());
            lines << QString(qMax(0, (W - tag.size()) / 2), ' ') + tag;
        } else if (b.kind == PreviewBlock::Cut) {
            lines << QString(W, '-');
        } else {
            int pad = 0;
            if (b.align == EscPosBuilder::Align::Center)     pad = qMax(0, (W - b.text.size()) / 2);
            else if (b.align == EscPosBuilder::Align::Right) pad = qMax(0, W - b.text.size());
            lines << QString(pad, ' ') + b.text;
        }
    }
    return lines.join('\n');
}

} // namespace EscPosPreview

#endif // ESCPOSPREVIEW_H
