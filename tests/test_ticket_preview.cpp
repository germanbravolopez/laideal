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

#include "escposbuilder.h"
#include "ticketrenderer.h"
#include "escpospreview.h"

using namespace EscPosPreview;

namespace {

// A QR-like placeholder image (three finder squares + a checker field), one pixel
// per module, no quiet zone; fakeQr() scales it up as a stand-in for the AEAT pixmap.
QImage fakeQrModules(int mods = 25)
{
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
    return q;
}

QImage fakeQr()
{
    return fakeQrModules().scaled(192, 192);
}

// `modules` with a 4-module quiet zone, `pixels` per module.
QImage withQuietZone(const QImage &modules, int pixels)
{
    QImage q((modules.width() + 8) * pixels, (modules.height() + 8) * pixels, QImage::Format_RGB32);
    q.fill(qRgb(255, 255, 255));
    for (int y = 0; y < modules.height(); ++y)
        for (int x = 0; x < modules.width(); ++x)
            if (qGray(modules.pixel(x, y)) < 128)
                for (int dy = 0; dy < pixels; ++dy)
                    for (int dx = 0; dx < pixels; ++dx)
                        q.setPixel((x + 4) * pixels + dx, (y + 4) * pixels + dy, qRgb(0, 0, 0));
    return q;
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

        const QString png = m_outDir + "/preview_" + name + ".png";
        const QString txt = m_outDir + "/preview_" + name + ".txt";
        if (!img.save(png, "PNG"))
            qWarning().noquote() << "could not write" << png;
        QFile f(txt);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(ascii.toUtf8());
            f.close();
        } else {
            qWarning().noquote() << "could not write" << txt;
        }

        qInfo().noquote() << "\n----- preview:" << name << "----- ->" << png << "\n" << ascii << "\n";
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
        QVERIFY(ascii.contains("42,20"));
        QVERIFY(ascii.contains("(Copia para el cliente)"));
        // A recibo carries no QR.
        bool hasRaster = false;
        for (const auto &b : blocks) if (b.kind == PreviewBlock::Raster) hasRaster = true;
        QVERIFY(!hasRaster);
        QVERIFY(QFile(m_outDir + "/preview_recibo.png").size() > 0);
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
    }

    // Art. 21.1 of the Orden: the printed QR measures 30-40 mm (240-320 dots at 8
    // dots/mm), every module the same whole number of dots, the same modules as the
    // source - a crisp one (direct client) or a smoothly scaled one (gateway pixmap) -
    // and a 4-module quiet zone; it fits the 58 mm roll too.
    void test_printableQr()
    {
        const QImage grid = fakeQrModules();
        const int n = grid.width();
        for (const QImage &source : { withQuietZone(grid, 5),
                                      withQuietZone(grid, 1).scaled(192, 192, Qt::KeepAspectRatio, Qt::SmoothTransformation) }) {
            const QImage out = TicketRenderer::printableQr(source);
            QVERIFY2(!out.isNull(), qPrintable(QString("source %1x%2").arg(source.width()).arg(source.height())));
            const int dots = (out.width() / (n + 8));
            QCOMPARE(out.width(), (n + 8) * dots);
            QVERIFY2(n * dots >= 240 && n * dots <= 320, qPrintable(QString::number(n * dots)));
            QVERIFY(out.width() <= 420);
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                    QCOMPARE(qGray(out.pixel((x + 4) * dots + dots / 2, (y + 4) * dots + dots / 2)) < 128,
                             qGray(grid.pixel(x, y)) < 128);
        }
        // A pixmap whose light modules are transparent (some PNGs): the same grid.
        QImage transparent = withQuietZone(grid, 4).convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < transparent.height(); ++y)
            for (int x = 0; x < transparent.width(); ++x)
                if (qGray(transparent.pixel(x, y)) >= 128)
                    transparent.setPixel(x, y, qRgba(0, 0, 0, 0));
        const QImage fromTransparent = TicketRenderer::printableQr(transparent);
        QVERIFY(!fromTransparent.isNull());
        const int tdots = fromTransparent.width() / (n + 8);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                QCOMPARE(qGray(fromTransparent.pixel((x + 4) * tdots + tdots / 2, (y + 4) * tdots + tdots / 2)) < 128,
                         qGray(grid.pixel(x, y)) < 128);
        QVERIFY(TicketRenderer::printableQr(QImage()).isNull());
        QImage blank(100, 100, QImage::Format_RGB32);
        blank.fill(qRgb(255, 255, 255));
        QVERIFY(TicketRenderer::printableQr(blank).isNull());
    }

    // A small symbol gets bigger modules, so its quiet zone must still fit the roll.
    void test_printableQrFitsPaperWidth()
    {
        for (const int paper : { 420, 576 }) {
            for (const int n : { 21, 25, 29, 37, 41, 57 }) {
                const QImage out = TicketRenderer::printableQr(withQuietZone(fakeQrModules(n), 4), paper);
                QVERIFY2(!out.isNull(), qPrintable(QString("%1 modules").arg(n)));
                QVERIFY2(out.width() <= paper, qPrintable(QString("%1 modules on %2: %3 dots").arg(n).arg(paper).arg(out.width())));
                QCOMPARE(out.width() % (n + 8), 0);   // whole dots per module
            }
        }
    }

    // Art. 20.1.b: the leyenda in a well-visible font like the rest (Font A, not B),
    // on two lines that fit both paper widths.
    void test_leyendaInBodyFont()
    {
        const QByteArray bytes = TicketRenderer::render(sampleFactura(), 420);
        const int at = bytes.indexOf("Factura verificable en la sede");
        QVERIFY(at > 0);
        const int font = bytes.lastIndexOf("\x1bM", at);
        QVERIFY(font < 0 || bytes.at(font + 2) == 0);                          // Font A (the default)
        QVERIFY(bytes.indexOf("de la AEAT", at) > at);
    }
};

QTEST_MAIN(TestTicketPreview)
#include "test_ticket_preview.moc"
