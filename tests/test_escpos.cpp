// Tests for the pure printing core: EscPosBuilder (exact ESC/POS bytes, the
// PC858 transcode, and the column-width math) and TicketRenderer (the receipt /
// invoice layout). Both are I/O-free, so they run headless in the ctest gate;
// ThermalPrinter (the Win32 RAW spooler) is integration-only and not covered
// here. Uses QTEST_GUILESS_MAIN - QImage works under a QCoreApplication.

#include <QtTest>
#include <QImage>

#include "escposbuilder.h"
#include "ticketrenderer.h"
#include "printerstatus.h"

// ESC/POS marker fragments reused across assertions.
static const QByteArray INIT_BYTES   = QByteArray("\x1B\x40\x1B\x74\x13", 5); // ESC @ + ESC t 19
static const QByteArray CUT_BYTES    = QByteArray("\x1D\x56\x42\x00", 4);     // GS V 66 0
static const QByteArray RASTER_HDR   = QByteArray("\x1D\x76\x30\x00", 4);     // GS v 0 m=0

class TestEscPos : public QObject
{
    Q_OBJECT

private slots:
    // --- EscPosBuilder: exact control bytes ---
    void test_initSelectsCp858()
    {
        EscPosBuilder b;
        b.init();
        QCOMPARE(b.bytes(), INIT_BYTES);
    }

    void test_styleCommandBytes()
    {
        EscPosBuilder b;
        b.align(EscPosBuilder::Align::Center);
        QCOMPARE(b.bytes(), QByteArray("\x1B\x61\x01", 3));      // ESC a 1

        EscPosBuilder b2;
        b2.bold(true).bold(false);
        QCOMPARE(b2.bytes(), QByteArray("\x1B\x45\x01\x1B\x45\x00", 6));  // ESC E 1 / 0

        EscPosBuilder b3;
        b3.doubleSize(true);
        QCOMPARE(b3.bytes(), QByteArray("\x1D\x21\x11", 3));     // GS ! 0x11

        EscPosBuilder b4;
        b4.font(EscPosBuilder::Font::B);
        QCOMPARE(b4.bytes(), QByteArray("\x1B\x4D\x01", 3));     // ESC M 1
    }

    void test_cutEndsStream()
    {
        EscPosBuilder b;
        b.init().line("x").cut();
        QVERIFY(b.bytes().endsWith(QByteArray("\x1B\x64\x03", 3) + CUT_BYTES)); // ESC d 3 + cut
    }

    // --- Column-width math: dots / char width, per font ---
    void test_columnCount()
    {
        EscPosBuilder a80(576);
        QCOMPARE(a80.columns(), 48);            // Font A @80mm
        a80.font(EscPosBuilder::Font::B);
        QCOMPARE(a80.columns(), 64);            // Font B @80mm

        EscPosBuilder a58(420);
        QCOMPARE(a58.columns(), 35);            // Font A @58mm
        a58.font(EscPosBuilder::Font::B);
        QCOMPARE(a58.columns(), 46);            // Font B @58mm
    }

    // --- PC858 transcoding (the single most error-prone step) ---
    void test_asciiPassesThrough()
    {
        QCOMPARE(EscPosBuilder::toCp858(QStringLiteral("ABC 123")), QByteArray("ABC 123"));
    }

    void test_spanishGlyphsMapToCp858()
    {
        const QByteArray got = EscPosBuilder::toCp858(QString::fromUtf8("ñÑáéíóú¿¡€"));
        const QByteArray expected = QByteArray("\xA4\xA5\xA0\x82\xA1\xA2\xA3\xA8\xAD\xD5", 10);
        QCOMPARE(got, expected);
    }

    void test_unmappedCharBecomesQuestionMark()
    {
        // U+2192 (rightwards arrow) has no PC858 glyph.
        QCOMPARE(EscPosBuilder::toCp858(QString::fromUtf8("→")), QByteArray("?"));
    }

    void test_lineSpacingBytes()
    {
        EscPosBuilder b;
        b.lineSpacing(24).defaultLineSpacing();
        QCOMPARE(b.bytes(), QByteArray("\x1B\x33\x18\x1B\x32", 5));  // ESC 3 24 / ESC 2
    }

    // --- Layout helpers ---
    void test_leftRightFillsLineWidth()
    {
        EscPosBuilder b(576);                   // 48 cols, Font A
        b.leftRight("TOTAL:", "12.34");
        const QByteArray content = b.bytes().left(b.bytes().size() - 1); // drop trailing LF
        QCOMPARE(content.size(), 48);
        QVERIFY(content.startsWith("TOTAL:"));
        QVERIFY(content.endsWith("12.34"));
    }

    void test_garmentRowColumns()
    {
        EscPosBuilder b(576);                    // 48 cols -> name col 33
        b.garmentRow("2", "Camisa", "3.50");
        const QByteArray content = b.bytes().left(b.bytes().size() - 1);
        QCOMPARE(content.size(), 48);
        QVERIFY(content.startsWith("2     Camisa"));   // qty padded to 6, then name
        QVERIFY(content.endsWith("3.50"));
    }

    void test_wrapText()
    {
        QCOMPARE(EscPosBuilder::wrapText("aaaa bbbb cccc", 9),
                 (QStringList{ "aaaa bbbb", "cccc" }));
        QCOMPARE(EscPosBuilder::wrapText("abcdefghijk", 4),
                 (QStringList{ "abcd", "efgh", "ijk" }));
    }

    // --- TicketRenderer: a recibo ---
    void test_renderReciboFragments()
    {
        TicketData d;
        d.businessName = "La Ideal";
        d.isRecibo     = true;
        d.invoiceId    = "123";
        d.clientName   = "Juan";
        d.receptionDate = "01/02/2026";
        d.garments = { {"2", "Camisa", "3.50"}, {"1", "Pantalon", "4.00"} };
        d.total = 7.50;
        d.ivaRate = 21;
        d.copyForClient = true;
        d.timestamp = "01/02/2026 - 10:00:00";

        const QByteArray out = TicketRenderer::render(d, 576);
        QVERIFY(out.startsWith(INIT_BYTES));
        QVERIFY(out.contains("La Ideal"));
        QVERIFY(out.contains("Recibo"));
        QVERIFY(out.contains("123"));
        QVERIFY(out.contains("Camisa"));
        QVERIFY(out.contains("TOTAL (IVA incl.):"));
        QVERIFY(out.contains("7.50"));
        QVERIFY(out.contains("(Copia para el cliente)"));
        QVERIFY(out.endsWith(CUT_BYTES));
    }

    // --- TicketRenderer: a factura with the IVA split ---
    void test_renderFacturaTaxSplit()
    {
        TicketData d;
        d.businessName = "La Ideal";
        d.isRecibo = false;
        d.invoiceId = "55";
        d.clientName = "Ana";
        d.receptionDate = "01/02/2026";
        d.garments = { {"1", "Abrigo", "121.00"} };
        d.total = 121.00;
        d.ivaRate = 21;
        d.copyForClient = false;
        d.timestamp = "01/02/2026 - 10:00:00";

        const QByteArray out = TicketRenderer::render(d, 576);
        QVERIFY(out.contains("FACTURA SIMPLIFICADA"));
        QVERIFY(out.contains("Base Imponible:"));
        QVERIFY(out.contains("100.00"));        // 121 / 1.21
        QVERIFY(out.contains("IVA (21%):"));
        QVERIFY(out.contains("21.00"));         // 121 - 100
        QVERIFY(out.contains("TOTAL:"));
        // No recibo copy marker on a factura.
        QVERIFY(!out.contains("Copia para el cliente"));
    }

    // --- TicketRenderer: a QR is rastered when present ---
    void test_renderFacturaWithQrRaster()
    {
        TicketData d;
        d.businessName = "La Ideal";
        d.isRecibo = false;
        d.invoiceId = "55";
        d.clientName = "Ana";
        d.total = 10.0;
        d.timestamp = "01/02/2026 - 10:00:00";
        QImage qr(16, 16, QImage::Format_RGB32);
        qr.fill(Qt::black);
        d.qr = qr;

        const QByteArray out = TicketRenderer::render(d, 576);
        QVERIFY(out.contains(RASTER_HDR));      // GS v 0 raster image emitted
    }

    // --- PrinterStatus: ASB status-word decode (Status API, optional path) ---
    void test_printerStatus_okWhenZero()
    {
        const PrinterStatus s = PrinterStatus::fromAsb(0);
        QVERIFY(s.valid);
        QVERIFY(!s.hasError());
        QVERIFY(!s.hasWarning());
        QCOMPARE(s.summary(), QStringLiteral("Impresora lista"));
    }

    void test_printerStatus_paperNearEndIsWarning()
    {
        const PrinterStatus s = PrinterStatus::fromAsb(0x00020000u);
        QVERIFY(s.paperNearEnd);
        QVERIFY(!s.paperEnd);
        QVERIFY(s.hasWarning());
        QVERIFY(!s.hasError());
        QCOMPARE(s.summary(), QStringLiteral("Papel casi agotado"));
    }

    void test_printerStatus_paperEndIsError()
    {
        const PrinterStatus s = PrinterStatus::fromAsb(0x00080000u);
        QVERIFY(s.paperEnd);
        QVERIFY(s.hasError());
        QCOMPARE(s.summary(), QStringLiteral("Sin papel"));
    }

    void test_printerStatus_coverOpenAndCutter()
    {
        QVERIFY(PrinterStatus::fromAsb(0x00000020u).coverOpen);
        QCOMPARE(PrinterStatus::fromAsb(0x00000020u).summary(), QStringLiteral("Tapa abierta"));
        QVERIFY(PrinterStatus::fromAsb(0x00000800u).cutterError);
        QCOMPARE(PrinterStatus::fromAsb(0x00000800u).summary(), QStringLiteral("Error en el cortador"));
        QVERIFY(PrinterStatus::fromAsb(0x00000008u).offline);
    }

    void test_printerStatus_errorsRankBeforeWarning()
    {
        // Paper end + near end set together: the hard error must win the summary.
        const PrinterStatus s = PrinterStatus::fromAsb(0x00080000u | 0x00020000u);
        QVERIFY(s.paperEnd);
        QVERIFY(s.paperNearEnd);
        QCOMPARE(s.summary(), QStringLiteral("Sin papel"));
    }

    void test_printerStatus_defaultIsInvalid()
    {
        const PrinterStatus s;   // not decoded from any word yet
        QVERIFY(!s.valid);
        QCOMPARE(s.summary(), QStringLiteral("Estado de la impresora desconocido"));
    }
};

QTEST_GUILESS_MAIN(TestEscPos)
#include "test_escpos.moc"
