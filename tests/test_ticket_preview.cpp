// Visual preview of a sample recibo and factura, generated from the REAL
// TicketRenderer ESC/POS output (so it reflects exactly what is sent to the
// printer). For each ticket it:
//   - interprets the ESC/POS byte subset we emit (align/bold/font/size/raster/
//     cut/feed/text, PC858-decoded back to Unicode), and
//   - writes a PNG simulating the thermal paper + a .txt ASCII mock next to the
//     test executable, and logs an ASCII mock to the test output.
//
// Run it and look at the files:
//   ctest --test-dir build -R ticket_preview -V        (shows the ASCII mocks)
//   tests/test_ticket_preview.exe                       (or run the exe directly)
//   -> build/tests/preview_recibo.png / preview_factura.png (+ .txt)
//
// It also asserts the previews are produced and contain the expected blocks, so
// it doubles as a regression test on the overall layout. Uses QTEST_MAIN
// (QGuiApplication - no Widgets) under offscreen so QPainter text rasterizes
// headless.

#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFile>
#include <QHash>
#include <QVector>
#include <QCoreApplication>
#include <QBuffer>

#include "escposbuilder.h"
#include "ticketrenderer.h"

namespace {

// Inverse of EscPosBuilder::toCp858 for the glyphs the tickets use, so the
// printed bytes can be shown back as readable text.
QChar cp858ToUnicode(uchar b)
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
        {0xD8,0x00CF},{0xDD,0x00CC},{0xE0,0x00D3},{0xE2,0x00D4},{0xE3,0x00D2},
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

uchar at(const QByteArray &d, int i) { return i < d.size() ? static_cast<uchar>(d[i]) : 0; }

// Parse the ESC/POS subset EscPosBuilder emits into a flat list of blocks.
QVector<PreviewBlock> interpret(const QByteArray &d)
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
QImage toImage(const QVector<PreviewBlock> &blocks, int paperDots)
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
QString toAscii(const QVector<PreviewBlock> &blocks, int paperDots)
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

QString xmlEscape(const QString &s)
{
    QString r = s;
    r.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
    return r;
}

// Render the blocks to a self-contained SVG. Unlike the PNG, this renders as
// crisp vector text and embeds inside Markdown when committed to the repo and
// referenced by path/URL (GitHub serves repo SVGs via its image proxy) - the
// reliable way to show a graphical ticket preview on github.com. The font size
// is chosen so a full 48-char line fits the paper width in a generic monospace.
QString toSvg(const QVector<PreviewBlock> &blocks, int paperDots)
{
    const int margin = 12;
    const int width  = paperDots + 2 * margin;
    auto pxFor = [](const PreviewBlock &b) {
        int px = (b.font == EscPosBuilder::Font::B) ? 14 : 18;
        if (b.dbl) px *= 2;
        return px;
    };
    auto lineH = [](int px) { return qRound(px * 1.35); };

    int totalH = margin * 2;
    for (const auto &b : blocks) {
        if (b.kind == PreviewBlock::Raster)   totalH += b.raster.height() + 10;
        else if (b.kind == PreviewBlock::Cut) totalH += 18;
        else                                  totalH += lineH(pxFor(b));
    }

    QStringList s;
    s << QString("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                 "width=\"%1\" height=\"%2\" viewBox=\"0 0 %1 %2\" font-family=\"monospace\">")
             .arg(width).arg(totalH);
    s << QString("<rect width=\"%1\" height=\"%2\" fill=\"white\"/>").arg(width).arg(totalH);

    int y = margin;
    for (const auto &b : blocks) {
        if (b.kind == PreviewBlock::Raster) {
            QByteArray png;
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            b.raster.save(&buf, "PNG");
            buf.close();
            s << QString("<image x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" "
                         "xlink:href=\"data:image/png;base64,%5\"/>")
                     .arg((width - b.raster.width()) / 2).arg(y)
                     .arg(b.raster.width()).arg(b.raster.height())
                     .arg(QString::fromLatin1(png.toBase64()));
            y += b.raster.height() + 10;
        } else if (b.kind == PreviewBlock::Cut) {
            s << QString("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%2\" stroke=\"#999\" stroke-dasharray=\"4 4\"/>")
                     .arg(margin).arg(y + 9).arg(width - margin);
            y += 18;
        } else {
            const int px = pxFor(b);
            if (!b.text.isEmpty()) {
                QString anchor = "start";
                int x = margin;
                if (b.align == EscPosBuilder::Align::Center) { anchor = "middle"; x = width / 2; }
                else if (b.align == EscPosBuilder::Align::Right) { anchor = "end"; x = width - margin; }
                const QString weight = b.bold ? QStringLiteral(" font-weight=\"bold\"") : QString();
                s << QString("<text x=\"%1\" y=\"%2\" font-size=\"%3\" text-anchor=\"%4\"%5 "
                             "xml:space=\"preserve\">%6</text>")
                         .arg(x).arg(y + px).arg(px).arg(anchor).arg(weight).arg(xmlEscape(b.text));
            }
            y += lineH(px);
        }
    }
    s << "</svg>";
    return s.join('\n');
}

// A QR-like placeholder image (three finder squares + a checker field), scaled
// up - stands in for the real AEAT pixmap in the sample factura.
QImage fakeQr()
{
    const int mods = 25;
    QImage q(mods, mods, QImage::Format_RGB32);
    q.fill(qRgb(255, 255, 255));
    for (int y = 0; y < mods; ++y)
        for (int x = 0; x < mods; ++x)
            if (((x * 7 + y * 13 + x * y) % 3) == 0)
                q.setPixel(x, y, qRgb(0, 0, 0));
    auto finder = [&](int ox, int oy) {
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x) {
                const bool border = (x == 0 || x == 6 || y == 0 || y == 6);
                const bool inner  = (x >= 2 && x <= 4 && y >= 2 && y <= 4);
                q.setPixel(ox + x, oy + y, (border || inner) ? qRgb(0, 0, 0) : qRgb(255, 255, 255));
            }
    };
    finder(0, 0); finder(mods - 7, 0); finder(0, mods - 7);
    return q.scaled(192, 192);
}

TicketData sampleHeader()
{
    TicketData d;
    d.businessName = "La Ideal";
    d.legalName    = QString::fromUtf8("Tintorería La Ideal SL");
    d.nif          = "B12345678";
    d.address      = QString::fromUtf8("Plaza San Pantaleón 1, bajo 2");
    d.city         = "18012 Granada";
    d.phone        = "958123456";
    d.ivaRate      = 21;
    d.timestamp    = "01/06/2026 - 10:30:00";
    return d;
}

TicketData sampleRecibo()
{
    TicketData d = sampleHeader();
    d.isRecibo     = true;
    d.invoiceId    = "1234";
    d.clientName   = QString::fromUtf8("María García");
    d.receptionDate = "01/06/2026";
    d.garments = {
        { "2", "Camisa", "7.00" },
        { "1", QString::fromUtf8("Pantalón"), "4.00" },
        { "1", "Alfombra - 2.60", "31.20" },
    };
    d.total = 42.20;
    d.copyForClient = true;
    return d;
}

TicketData sampleFactura()
{
    TicketData d = sampleHeader();
    d.isRecibo     = false;
    d.invoiceId    = "1234-1";
    d.clientName   = QString::fromUtf8("María García");
    d.receptionDate = "01/06/2026";
    d.garments = { { "1", "Abrigo", "121.00" } };
    d.total = 121.00;
    d.addPayedInfo = true;
    d.copyForClient = true;
    d.qr = fakeQr();
    d.verifactuVerifiable = true;
    return d;
}

} // namespace

class TestTicketPreview : public QObject
{
    Q_OBJECT

    QString m_outDir;

private:
    // Render + interpret + write the PNG and .txt, log the ASCII, return blocks.
    QVector<PreviewBlock> makePreview(const QString &name, const TicketData &d)
    {
        const QByteArray escpos = TicketRenderer::render(d, /*paperDots=*/576);
        const QVector<PreviewBlock> blocks = interpret(escpos);

        const QImage img = toImage(blocks, 576);
        const QString ascii = toAscii(blocks, 576);
        const QString svg = toSvg(blocks, 576);

        const QString png = m_outDir + "/preview_" + name + ".png";
        const QString txt = m_outDir + "/preview_" + name + ".txt";
        const QString svgPath = m_outDir + "/preview_" + name + ".svg";
        if (!img.save(png, "PNG"))
            qWarning().noquote() << "could not write" << png;
        const auto writeText = [](const QString &path, const QString &content) {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) { f.write(content.toUtf8()); f.close(); }
            else qWarning().noquote() << "could not write" << path;
        };
        writeText(txt, ascii);
        writeText(svgPath, svg);

        qInfo().noquote() << "\n----- preview:" << name << "----- ->" << png << "/" << svgPath
                          << "\n" << ascii << "\n";
        return blocks;
    }

private slots:
    void initTestCase() { m_outDir = QCoreApplication::applicationDirPath(); }

    void test_recibo()
    {
        const auto blocks = makePreview("recibo", sampleRecibo());
        const QString ascii = toAscii(blocks, 576);
        QVERIFY(ascii.contains("La Ideal"));
        QVERIFY(ascii.contains("Recibo"));
        QVERIFY(ascii.contains("TOTAL (IVA incl.):"));
        QVERIFY(ascii.contains("42.20"));
        QVERIFY(ascii.contains("(Copia para el cliente)"));
        // A recibo carries no QR.
        bool hasRaster = false;
        for (const auto &b : blocks) if (b.kind == PreviewBlock::Raster) hasRaster = true;
        QVERIFY(!hasRaster);
        QVERIFY(QFile(m_outDir + "/preview_recibo.png").size() > 0);
        QVERIFY(QFile(m_outDir + "/preview_recibo.svg").size() > 0);
    }

    void test_factura()
    {
        const auto blocks = makePreview("factura", sampleFactura());
        const QString ascii = toAscii(blocks, 576);
        QVERIFY(ascii.contains("FACTURA SIMPLIFICADA"));
        QVERIFY(ascii.contains("Base Imponible:"));
        QVERIFY(ascii.contains("IVA (21%):"));
        QVERIFY(ascii.contains("TOTAL:"));
        // A factura rasters the QR.
        bool hasRaster = false;
        for (const auto &b : blocks) if (b.kind == PreviewBlock::Raster) hasRaster = true;
        QVERIFY(hasRaster);
        QVERIFY(QFile(m_outDir + "/preview_factura.png").size() > 0);
        QVERIFY(QFile(m_outDir + "/preview_factura.svg").size() > 0);
    }
};

QTEST_MAIN(TestTicketPreview)
#include "test_ticket_preview.moc"
