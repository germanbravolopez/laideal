// Tests for parseVerifactuResponse() - the pure parser extracted from
// VerifactuManager. Fixtures mirror the Irene Solutions / AEAT JSON reply shapes
// (ResultCode + Return{CSV,ValidationUrl,Xml,ErrorCode,QrCode}).
//
// Uses QTEST_MAIN (QApplication) rather than the guiless main because the QR
// path constructs a QPixmap, which needs a QGuiApplication; the test runs under
// QT_QPA_PLATFORM=offscreen (set by CTest) so it stays headless.

#include <QtTest>
#include <QBuffer>
#include <QImage>

#include "verifacturesponse.h"

class TestVerifactuResponse : public QObject
{
    Q_OBJECT

private slots:
    void test_invalidJsonIsError()
    {
        const VerifactuResult r = parseVerifactuResponse(QByteArray("this is not json"));
        QCOMPARE(r.status, VerifactuResult::ERROR);
        QVERIFY(!r.errorDescription.isEmpty());
    }

    void test_resultCodeErrorUsesResultMessage()
    {
        const QByteArray json = R"({"ResultCode":1,"ResultMessage":"Boom"})";
        const VerifactuResult r = parseVerifactuResponse(json);
        QCOMPARE(r.status, VerifactuResult::ERROR);
        QCOMPARE(r.errorCode, QStringLiteral("1"));
        QCOMPARE(r.errorDescription, QStringLiteral("Boom"));
    }

    void test_resultCodeErrorPrefersReturnErrorDescription()
    {
        const QByteArray json =
            R"({"ResultCode":2,"ResultMessage":"Generic","Return":{"ErrorDescription":"Specific"}})";
        const VerifactuResult r = parseVerifactuResponse(json);
        QCOMPARE(r.status, VerifactuResult::ERROR);
        QCOMPARE(r.errorDescription, QStringLiteral("Specific"));
    }

    // ResultCode 0 but an invoice-level Return.ErrorCode is still an error.
    void test_invoiceLevelErrorWithResultCodeZero()
    {
        const QByteArray json =
            R"({"ResultCode":0,"Return":{"ErrorCode":"E13","ErrorDescription":"NIF incorrecto"}})";
        const VerifactuResult r = parseVerifactuResponse(json);
        QCOMPARE(r.status, VerifactuResult::ERROR);
        QCOMPARE(r.errorCode, QStringLiteral("E13"));
        QCOMPARE(r.errorDescription, QStringLiteral("NIF incorrecto"));
    }

    void test_successExtractsCsvUrlAndHuella()
    {
        // The XML carries both HuellaPrevia and Huella; only the exact Huella
        // element must be captured, and the hash is upper-cased.
        const QByteArray json =
            R"({"ResultCode":0,"Return":{"CSV":"CSV-123","ValidationUrl":"https://aeat.example/qr",)"
            R"("Xml":"<r><sum1:HuellaPrevia>aaaa</sum1:HuellaPrevia><sum1:Huella>abcdef0123</sum1:Huella></r>"}})";
        const VerifactuResult r = parseVerifactuResponse(json);
        QCOMPARE(r.status, VerifactuResult::SUCCESS);
        QCOMPARE(r.csv, QStringLiteral("CSV-123"));
        QCOMPARE(r.validationUrl, QStringLiteral("https://aeat.example/qr"));
        QCOMPARE(r.rawHash, QStringLiteral("ABCDEF0123"));
    }

    void test_successWithoutReturn()
    {
        const VerifactuResult r = parseVerifactuResponse(QByteArray(R"({"ResultCode":0})"));
        QCOMPARE(r.status, VerifactuResult::SUCCESS);
    }

    // GetQrCode reply: Return is the base64 image directly; it must decode to a
    // non-null pixmap (1x1 PNG fixture).
    void test_qrRequestDecodesPixmap()
    {
        // Generate a valid PNG in-memory so the fixture can't be a bad literal.
        QImage img(2, 2, QImage::Format_ARGB32);
        img.fill(Qt::black);
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        QVERIFY(img.save(&buf, "PNG"));
        const QString b64 = QString::fromLatin1(png.toBase64());

        const QByteArray json = QStringLiteral(R"({"ResultCode":0,"Return":"%1"})").arg(b64).toUtf8();
        const VerifactuResult r = parseVerifactuResponse(json, /*isQrRequest=*/true);
        QCOMPARE(r.status, VerifactuResult::SUCCESS);
        QVERIFY(!r.qrCode.isNull());
    }

    // ---- GetFilteredList (invoice query) --------------------------------
    // The vendor does not publish this response schema, so the parser probes
    // several plausible envelopes and field spellings. These fixtures are
    // therefore HYPOTHETICAL shapes, not captured traffic - what they pin is the
    // parser's behaviour, above all that it never half-decodes: anything it does
    // not understand comes back parsed=false so the caller shows the raw payload
    // instead of concluding something about AEAT.
    void test_parseQuery_enveloppedRecord()
    {
        const QByteArray json = R"({"ResultCode":0,"Return":[{
            "InvoiceID":"30877","InvoiceDate":"03-09-2026","TotalAmount":24.20,
            "CSV":"A-7F3K9Q","StatusResponse":"Correcta",
            "ValidationUrl":"https://aeat.example/v?x=1"}]})";
        const VerifactuRemoteRecord r = parseVerifactuQueryResponse(json);
        QVERIFY(r.found);
        QVERIFY(r.parsed);
        QCOMPARE(r.invoiceId, QStringLiteral("30877"));
        QCOMPARE(r.invoiceDate, QStringLiteral("03-09-2026"));
        QVERIFY(qAbs(r.totalAmount - 24.20) < 0.001);
        QCOMPARE(r.csv, QStringLiteral("A-7F3K9Q"));
        QVERIFY(r.hasUsableCsv());
        QVERIFY(!r.raw.isEmpty());
    }

    // Alternative spellings/shapes the parser must also cope with: a bare array,
    // ISO dates, a string amount, and the detail nested one level down.
    void test_parseQuery_alternativeShapes()
    {
        const VerifactuRemoteRecord bare = parseVerifactuQueryResponse(
            R"([{"InvoiceId":"30877","Date":"2026-09-03","Total":"24,20","Csv":"A-7F3K9Q"}])");
        QVERIFY(bare.parsed);
        QCOMPARE(bare.invoiceId, QStringLiteral("30877"));
        QCOMPARE(bare.invoiceDate, QStringLiteral("03-09-2026")); // ISO normalised
        QVERIFY(qAbs(bare.totalAmount - 24.20) < 0.001);          // comma decimal
        QCOMPARE(bare.csv, QStringLiteral("A-7F3K9Q"));

        const VerifactuRemoteRecord nested = parseVerifactuQueryResponse(
            R"({"ResultCode":0,"Records":[{"InvoiceID":"30877","InvoiceDate":"03-09-2026",
                "TotalAmount":24.20,"Return":{"CSV":"A-7F3K9Q"}}]})");
        QVERIFY(nested.parsed);
        QCOMPARE(nested.csv, QStringLiteral("A-7F3K9Q"));
    }

    // "AEAT does not have it" and "we could not read the answer" must never be
    // conflated - the first is a fact, the second is ignorance.
    void test_parseQuery_absentVsUnreadable()
    {
        const VerifactuRemoteRecord empty = parseVerifactuQueryResponse(
            R"({"ResultCode":0,"Return":[]})");
        QVERIFY(empty.parsed);      // understood
        QVERIFY(!empty.found);      // genuinely not registered
        QVERIFY(!empty.hasUsableCsv());

        // Failed query: NOT evidence of absence.
        const VerifactuRemoteRecord failed = parseVerifactuQueryResponse(
            R"({"ResultCode":-1,"ResultMessage":"ServiceKey invalida"})");
        QVERIFY(!failed.parsed);
        QVERIFY(!failed.found);

        for (const char *junk : { "", "not json at all", "{\"Unexpected\":{\"Shape\":1}}" }) {
            const VerifactuRemoteRecord bad = parseVerifactuQueryResponse(junk);
            QVERIFY2(!bad.hasUsableCsv(), junk);
        }
    }

    // An "already exists" rejection is good news wearing an error's clothes: the
    // invoice IS at AEAT and only the reply was lost, so it triggers a reconcile
    // instead of another doomed retry. Matched on wording - no stable code exists.
    void test_errorIsDuplicate()
    {
        QVERIFY(verifactuErrorIsDuplicate("", "Registro de facturacion duplicado"));
        QVERIFY(verifactuErrorIsDuplicate("", "La factura ya existe en el sistema"));
        QVERIFY(verifactuErrorIsDuplicate("", "Factura ya registrada"));
        QVERIFY(verifactuErrorIsDuplicate("", "Duplicate invoice ID"));
        QVERIFY(verifactuErrorIsDuplicate("", "Invoice already exists"));
        QVERIFY(verifactuErrorIsDuplicate("DUPLICADO", ""));   // code carries it
        QVERIFY(verifactuErrorIsDuplicate("", "DUPLICADA"));   // case-insensitive

        // Real failures must NOT be mistaken for it - reconciling on those would
        // adopt a CSV for an invoice AEAT never accepted.
        QVERIFY(!verifactuErrorIsDuplicate("", "NIF del emisor invalido"));
        QVERIFY(!verifactuErrorIsDuplicate("", "Tiempo de espera agotado"));
        QVERIFY(!verifactuErrorIsDuplicate("", "Importe incorrecto"));
        QVERIFY(!verifactuErrorIsDuplicate("", ""));
    }

    // The reconciliation gate. A record may only overwrite a local row when the
    // InvoiceID, the date AND the amount all agree - reconciling on a partial
    // match would stamp a CSV belonging to a different invoice onto our row.
    void test_remoteMatches()
    {
        VerifactuRemoteRecord r;
        r.found = r.parsed = true;
        r.invoiceId   = "30877";
        r.invoiceDate = "03-09-2026";
        r.totalAmount = 24.20;
        r.csv         = "A-7F3K9Q";

        QVERIFY(verifactuRemoteMatches(r, "30877", "03-09-2026", 24.20));
        QVERIFY(verifactuRemoteMatches(r, "30877", "2026-09-03", 24.204)); // ISO + cent tolerance

        QVERIFY(!verifactuRemoteMatches(r, "30878", "03-09-2026", 24.20)); // other invoice
        QVERIFY(!verifactuRemoteMatches(r, "30877", "04-09-2026", 24.20)); // other date
        QVERIFY(!verifactuRemoteMatches(r, "30877", "03-09-2026", 24.30)); // other amount

        // Missing evidence is not agreement.
        QVERIFY(!verifactuRemoteMatches(r, "30877", "03-09-2026", 0.0));
        VerifactuRemoteRecord noDate = r; noDate.invoiceDate.clear();
        QVERIFY(!verifactuRemoteMatches(noDate, "30877", "03-09-2026", 24.20));
        VerifactuRemoteRecord noAmount = r; noAmount.totalAmount = 0.0;
        QVERIFY(!verifactuRemoteMatches(noAmount, "30877", "03-09-2026", 24.20));
        VerifactuRemoteRecord unparsed = r; unparsed.parsed = false;
        QVERIFY(!verifactuRemoteMatches(unparsed, "30877", "03-09-2026", 24.20));
        VerifactuRemoteRecord absent = r; absent.found = false;
        QVERIFY(!verifactuRemoteMatches(absent, "30877", "03-09-2026", 24.20));
    }
};

QTEST_MAIN(TestVerifactuResponse)
#include "test_verifactu_response.moc"
