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
#include "statusapiprinter.h"

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

    // Independent of the preview decoder: every mapped glyph against hard-coded PC858 values.
    void test_cp858TableAgainstReference()
    {
        struct Ref { ushort u; uchar b; };
        static const Ref ref[] = {
            {0xC7,0x80},{0xFC,0x81},{0xE9,0x82},{0xE2,0x83},{0xE4,0x84},{0xE0,0x85},{0xE7,0x87},{0xEA,0x88},
            {0xEB,0x89},{0xE8,0x8A},{0xEF,0x8B},{0xEE,0x8C},{0xEC,0x8D},{0xC4,0x8E},{0xC9,0x90},{0xF4,0x93},
            {0xF6,0x94},{0xF2,0x95},{0xFB,0x96},{0xF9,0x97},{0xD6,0x99},{0xDC,0x9A},{0xA3,0x9C},{0xE1,0xA0},
            {0xED,0xA1},{0xF3,0xA2},{0xFA,0xA3},{0xF1,0xA4},{0xD1,0xA5},{0xAA,0xA6},{0xBA,0xA7},{0xBF,0xA8},
            {0xAC,0xAA},{0xBD,0xAB},{0xBC,0xAC},{0xA1,0xAD},{0xAB,0xAE},{0xBB,0xAF},{0xC1,0xB5},{0xC2,0xB6},
            {0xC0,0xB7},{0xA9,0xB8},{0xE3,0xC6},{0xC3,0xC7},{0xCA,0xD2},{0xCB,0xD3},{0xC8,0xD4},{0x20AC,0xD5},
            {0xCD,0xD6},{0xCE,0xD7},{0xCF,0xD8},{0xCC,0xDE},{0xD3,0xE0},{0xD4,0xE2},{0xD2,0xE3},{0xD5,0xE5},
            {0xDA,0xE9},{0xDB,0xEA},{0xD9,0xEB},{0xDD,0xED},{0xB4,0xEF},{0xB0,0xF8},{0xB7,0xFA},
        };
        for (const Ref &r : ref) {
            const QByteArray got = EscPosBuilder::toCp858(QString(QChar(r.u)));
            QVERIFY2(got.size() == 1 && uchar(got[0]) == r.b,
                     qPrintable(QString("U+%1 -> 0x%2").arg(r.u, 4, 16).arg(uchar(got[0]), 2, 16)));
        }
        // 0xDD is the broken bar in PC858, never a letter.
        QCOMPARE(EscPosBuilder::toCp858(QString(QChar(0x00CC))), QByteArray::fromHex("DE"));
    }

    void test_controlCharactersBecomeSpaces()
    {
        const QString s = QString("A") + QChar(0x1B) + "p" + QChar(0x0A) + QChar(0x0D) + QChar(0x09)
                          + QChar(0x1D) + QChar(0x7F) + "B";
        QCOMPARE(EscPosBuilder::toCp858(s), QByteArray("A p     B"));
    }

    void test_unmappedCharBecomesQuestionMark()
    {
        // U+2192 (rightwards arrow) has no PC858 glyph.
        QCOMPARE(EscPosBuilder::toCp858(QString::fromUtf8("→")), QByteArray("?"));
    }

    // Glyphs a laundry plausibly prints (m2 on carpets, symbols, Nordic letters).
    void test_commonExtraGlyphsMapToCp858()
    {
        QCOMPARE(EscPosBuilder::toCp858(QString::fromUtf8("m²")), QByteArray("m\xFD", 2));
        QCOMPARE(EscPosBuilder::toCp858(QString::fromUtf8("²³¹")), QByteArray::fromHex("FDFCFB"));
        QCOMPARE(EscPosBuilder::toCp858(QString::fromUtf8("×±§®µ")), QByteArray::fromHex("9EF1F5A9E6"));
        QCOMPARE(EscPosBuilder::toCp858(QString::fromUtf8("åÅæÆøØßõýÿ")), QByteArray::fromHex("868F91929B9DE1E4EC98"));
        QCOMPARE(EscPosBuilder::toCp858(QString::fromUtf8("¢¥")), QByteArray::fromHex("BDBE"));
    }

    void test_typographicPunctuationFoldsToAscii()
    {
        QCOMPARE(EscPosBuilder::toCp858(QString::fromUtf8("a–b—c")), QByteArray("a-b-c"));
        QCOMPARE(EscPosBuilder::toCp858(QString::fromUtf8("‘x’ “y”")), QByteArray("'x' \"y\""));
        QCOMPARE(EscPosBuilder::toCp858(QString::fromUtf8("fin…")), QByteArray("fin..."));
        QCOMPARE(EscPosBuilder::toCp858(QString("a") + QChar(0x00A0) + "b"), QByteArray("a b"));
    }

    void test_feedIsClampedToOneByte()
    {
        EscPosBuilder b;
        b.feed(300).feed(0).feed(-1);
        QCOMPARE(b.bytes(), QByteArray::fromHex("1B64FF"));   // ESC d 255, nothing for 0 and -1
    }

    // The QR is thresholded, never dithered: a mid-grey image is solid black or solid white.
    void test_rasterImageThresholdsInsteadOfDithering()
    {
        for (const auto &pair : { qMakePair(100, uchar(0xFF)), qMakePair(200, uchar(0x00)) }) {
            QImage img(8, 2, QImage::Format_Grayscale8);
            img.fill(pair.first);
            EscPosBuilder b;
            b.rasterImage(img);
            QCOMPARE(b.bytes().left(8), QByteArray::fromHex("1D763000" "0100" "0200"));
            QCOMPARE(b.bytes().mid(8), QByteArray(2, char(pair.second)));
        }
    }

    // Header lines longer than the paper are wrapped by the renderer, not by the firmware.
    void test_renderWrapsALongHeader()
    {
        TicketData d;
        d.isRecibo = true;
        d.businessName = "Tintoreria La Ideal de Madrid Sociedad Limitada";   // 47 chars, 24 fit double-size
        d.address = "Calle de la Constitucion Espanola numero 123 bajo izquierda";
        const QByteArray out = TicketRenderer::render(d, 576);
        QVERIFY(!out.contains(d.businessName.toLatin1()));
        QVERIFY(out.contains("Tintoreria La Ideal de\nMadrid Sociedad Limitada\n"));
        QVERIFY(!out.contains(d.address.toLatin1()));
        QVERIFY(out.contains("Calle de la Constitucion Espanola numero 123\nbajo izquierda\n"));
    }

    void test_renderPrintsTheRateWithItsDecimals()
    {
        TicketData d;
        d.isRecibo = false;
        d.total = 11.05;
        d.ivaRate = 10.5;
        QVERIFY(TicketRenderer::render(d, 576).contains("IVA (10,5%):"));
        d.ivaRate = 21;
        QVERIFY(TicketRenderer::render(d, 576).contains("IVA (21%):"));
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
        QVERIFY(out.contains("7,50"));
        QVERIFY(out.contains("(Copia para el cliente)"));
        QVERIFY(out.endsWith(CUT_BYTES));
    }

    // --- TicketRenderer: the paid marker is opt-in per print ---
    // PayDialog's AEAT-timeout fallback prints a recibo for rows it has ALREADY
    // marked pagado=SI, so that recibo must carry the marker - otherwise the
    // customer gets a ticket showing neither the payment nor a Verifactu QR.
    void test_renderPaidMarker()
    {
        TicketData d;
        d.businessName  = "La Ideal";
        d.isRecibo      = true;
        d.invoiceId     = "123";
        d.total         = 7.50;
        d.ivaRate       = 21;
        d.copyForClient = true;

        d.addPayedInfo = false;
        QVERIFY(!TicketRenderer::render(d, 576).contains("IMPORTE PAGADO"));

        d.addPayedInfo = true;
        QVERIFY(TicketRenderer::render(d, 576).contains("IMPORTE PAGADO"));
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
        QVERIFY(out.contains("100,00"));        // 121 / 1.21
        QVERIFY(out.contains("IVA (21%):"));
        QVERIFY(out.contains("21,00"));         // 121 - 100
        QVERIFY(out.contains("TOTAL:"));
        // No recibo copy marker on a factura.
        QVERIFY(!out.contains("Copia para el cliente"));
    }

    // Art. 7 RD 1619/2012: a factura prints its date of issue (the payment date in the
    // QR), apart from the reception date; a recibo does not.
    void test_renderFacturaIssueDate()
    {
        TicketData d;
        d.isRecibo = false;
        d.invoiceId = "55";
        d.receptionDate = "01/02/2026";
        d.issueDate = "10/02/2026";
        const QByteArray factura = TicketRenderer::render(d, 576);
        QVERIFY(factura.contains("Fecha de expedici"));
        QVERIFY(factura.contains("10/02/2026"));
        QVERIFY(factura.contains("01/02/2026"));
        d.isRecibo = true;
        QVERIFY(!TicketRenderer::render(d, 576).contains("Fecha de expedici"));
    }

    // A reprint of a cancelled or rectified invoice says so and carries no QR or leyenda.
    void test_renderVoidedFacturaMarked()
    {
        TicketData d;
        d.isRecibo = false;
        d.invoiceId = "56";
        d.qr = QImage(29, 29, QImage::Format_Mono);
        d.qr.fill(0);
        d.verifactuVerifiable = true;
        const QByteArray valid = TicketRenderer::render(d, 576);
        QVERIFY(valid.contains("Factura verificable"));
        d.voidedMarker = "ANULADA";
        d.voidedDate = "12/02/2026";
        const QByteArray voided = TicketRenderer::render(d, 576);
        QVERIFY(voided.contains("*** FACTURA ANULADA ***"));
        QVERIFY(voided.contains("anulada el 12/02/2026"));
        QVERIFY(voided.contains("factura en vigor"));   // wrapped: the end of the note
        QVERIFY(!voided.contains("Factura verificable"));
        const QByteArray raster("\x1d\x76\x30", 3);       // GS v 0: the QR image
        QVERIFY(valid.contains(raster));
        QVERIFY(!voided.contains(raster));
        d.voidedMarker = "RECTIFICADA";
        QVERIFY(TicketRenderer::render(d, 576).contains("*** FACTURA RECTIFICADA ***"));
    }

    // A completa without a typed DNI prints no empty "DNI:" line.
    void test_renderCompleteInvoiceWithoutDni()
    {
        TicketData d;
        d.isRecibo = false;
        d.isCompleteInvoice = true;
        d.clientAddress = "Calle Mayor 1";
        QVERIFY(!TicketRenderer::render(d, 576).contains("DNI:"));
        d.clientDni = "12345678Z";
        QVERIFY(TicketRenderer::render(d, 576).contains("DNI: 12345678Z"));
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

    // --- StatusApiSendGate: a worker the watchdog gave up on must not send later ---
    void test_sendGate_abandonedWorkerNeverSends()
    {
        StatusApiSendGate gate;
        QVERIFY(gate.abandon());          // watchdog first: the worker had not started sending
        QVERIFY(!gate.beginSend());       // so the worker's later send is refused
    }

    void test_sendGate_sendingWorkerIsNotAbandonable()
    {
        StatusApiSendGate gate;
        QVERIFY(gate.beginSend());
        QVERIFY(!gate.abandon());         // already inside the send: RAW fallback would print twice
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

    // What the shop's unit reports for an open cover, off or unplugged: ASB 0x1.
    void test_printerStatus_noResponse()
    {
        const PrinterStatus s = PrinterStatus::fromAsb(0x00000001u);
        QVERIFY(s.noResponse);
        QVERIFY(s.summary().contains("no responde"));
        QVERIFY(!s.isFatal());
        QCOMPARE(PrinterStatus::fromAsb(0).noResponse, false);
        // A paper-out outranks it; all bits set stays decodable and fatal.
        QCOMPARE(PrinterStatus::fromAsb(0x00080001u).summary(), QStringLiteral("Sin papel"));
        const PrinterStatus all = PrinterStatus::fromAsb(0xFFFFFFFFu);
        QVERIFY(all.valid && all.isFatal() && all.coverOpen && all.paperEnd && all.noResponse);
    }

    void test_printerStatus_fatalVsRecoverable()
    {
        // Fatal faults (cutter / mechanical / unrecoverable): block printing.
        QVERIFY(PrinterStatus::fromAsb(0x00000800u).isFatal());   // auto-cutter
        QVERIFY(PrinterStatus::fromAsb(0x00000400u).isFatal());   // mechanical
        QVERIFY(PrinterStatus::fromAsb(0x00002000u).isFatal());   // unrecoverable
        // Recoverable conditions: an error/warning worth surfacing, but the job
        // may still be queued - so NOT fatal.
        QVERIFY(PrinterStatus::fromAsb(0x00000020u).hasError());  // cover open
        QVERIFY(!PrinterStatus::fromAsb(0x00000020u).isFatal());
        QVERIFY(PrinterStatus::fromAsb(0x00080000u).hasError());  // paper end
        QVERIFY(!PrinterStatus::fromAsb(0x00080000u).isFatal());
        QVERIFY(PrinterStatus::fromAsb(0x00020000u).hasWarning());// paper near end
        QVERIFY(!PrinterStatus::fromAsb(0x00020000u).isFatal());
        QVERIFY(!PrinterStatus::fromAsb(0).isFatal());
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
