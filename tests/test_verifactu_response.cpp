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

    // Tolerances the parser keeps on top of the confirmed schema: a bare array
    // instead of the Items envelope, trivial case variants of the field names, a
    // plain dd-MM-yyyy date, and an amount arriving as a comma-decimal string.
    // NOTE: these are robustness margins, not documented alternatives - the real
    // reply uses Items / InvoiceID / InvoiceDate / TotalAmount / CSV (see
    // test_parseQuery_realPopulatedReply, which is the authority on the schema).
    void test_parseQuery_toleratedVariants()
    {
        const VerifactuRemoteRecord bare = parseVerifactuQueryResponse(
            R"([{"InvoiceId":"30877","InvoiceDate":"03-09-2026","TotalAmount":"24,20","Csv":"A-7F3K9Q"}])");
        QVERIFY(bare.parsed);
        QCOMPARE(bare.invoiceId, QStringLiteral("30877"));
        QCOMPARE(bare.invoiceDate, QStringLiteral("03-09-2026"));
        QVERIFY(qAbs(bare.totalAmount - 24.20) < 0.001);   // comma decimal in a string
        QCOMPARE(bare.csv, QStringLiteral("A-7F3K9Q"));
        QVERIFY(bare.hasUsableCsv());
    }

    // CAPTURED from a real GetFilteredList call (14-09-2026, InvoiceID "2", test
    // key) - unlike the fixtures above this is actual traffic, and it is what
    // confirms the envelope is "Items". Note the reply echoes Offset/Count/
    // TableNameSufix/FilterLower/FilterUpper but NOT the Filters we sent, so an
    // empty Items does not by itself prove the InvoiceID filter was applied.
    void test_parseQuery_realEmptyReply()
    {
        const QByteArray json = R"({"Offset":0,"Count":0,"TableNameSufix":null,)"
                                R"("FilterLower":null,"FilterUpper":null,"Items":[],)"
                                R"("ResultCode":0,)"
                                R"("ResultMessage":"Retrieved element filtered list from domain 'Invoices'."})";
        const VerifactuRemoteRecord r = parseVerifactuQueryResponse(json);
        QVERIFY(r.parsed);              // the answer was understood
        QVERIFY(!r.found);              // and it contained no invoice
        QVERIFY(!r.hasUsableCsv());     // so nothing may be adopted from it
        QVERIFY(r.raw.contains("Retrieved element filtered list"));
    }

    // CAPTURED from a real populated GetFilteredList reply (14-09-2026, InvoiceID
    // "4-1"). Identity fields (NIF, company name, user e-mail, CSV, the batch/
    // instance ids) are REDACTED because this repository is public - the structure
    // and every field the parser reads are verbatim. This is what confirms the
    // record shape, the ISO-8601 InvoiceDate, and that the InvoiceID filter is
    // actually applied server-side (Count comes back 1, not the whole table).
    void test_parseQuery_realPopulatedReply()
    {
        const QByteArray json = R"({"Offset":0,"Count":1,"TableNameSufix":null,)"
            R"("FilterLower":null,"FilterUpper":null,"Items":[{)"
            R"("SellerID":"B00000000","CompanyName":"Tintoreria Ejemplo SL",)"
            R"("IndustryClassificationID":null,"BusinessGroupID":null,)"
            R"("BatchID":"00000000000000000000","InvoiceID":"4-1","Status":null,)"
            R"("InvoiceType":"F2","RectificationType":null,"IsInvoiceFix":false,)"
            R"("IsRejected":false,"ThirdPartyIssuer":null,)"
            R"("InvoiceDate":"2026-09-14T00:00:00","OperationDate":null,)"
            R"("PostingDate":"2026-09-14T01:36:02","PostingYear":"2026",)"
            R"("ValueDate":"2026-09-14T01:36:02","TaxDate":"2026-09-14T01:36:02",)"
            R"("RelatedPartyID":null,"RelatedPartyName":null,"CountryID":null,)"
            R"("TotalAmount":25.00,"ExternKey":"00000000000000000000",)"
            R"("Text":"Servicios de lavanderia","StatusResponse":"Correcto",)"
            R"("ErrorCode":null,"ErrorDescription":null,"CSV":"A-TESTCSV00000001",)"
            R"("TaxItems":null,"RectificationItems":null,"QrCode":null,"Xml":null,)"
            R"("Response":null,"QrCodeUrl":null,)"
            R"("ValidationUrl":"https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR?nif=B00000000&numserie=4-1&fecha=14-09-2026&importe=25.00",)"
            R"("InstanceID":"0000000000000000.0000000000000000","UserID":"usuario@example.com",)"
            R"("Created":"2026-09-14T01:36:02"}],"ResultCode":0,)"
            R"("ResultMessage":"Retrieved element filtered list from domain 'Invoices'."})";

        const VerifactuRemoteRecord r = parseVerifactuQueryResponse(json);
        QVERIFY(r.found);
        QVERIFY(r.parsed);
        QCOMPARE(r.invoiceId, QStringLiteral("4-1"));
        // ISO-8601 with a time component, normalised to the dd-MM-yyyy the DB stores.
        QCOMPARE(r.invoiceDate, QStringLiteral("14-09-2026"));
        QVERIFY(qAbs(r.totalAmount - 25.00) < 0.001);
        QCOMPARE(r.csv, QStringLiteral("A-TESTCSV00000001"));
        QCOMPARE(r.statusResponse, QStringLiteral("Correcto"));
        QVERIFY(r.validationUrl.startsWith("https://"));
        QVERIFY(!r.isRejected);
        QVERIFY(r.errorCode.isEmpty());
        QVERIFY(r.hasUsableCsv());

        // And it matches the local row it was queried for.
        QVERIFY(verifactuRemoteMatches(r, "4-1", "14-09-2026", 25.00));
        // ExternKey / Created are decoys in the same record: the parser must not
        // have picked them up as the InvoiceID or the invoice date.
        QVERIFY(r.invoiceId != QStringLiteral("00000000000000000000"));
        QVERIFY(r.invoiceDate != QStringLiteral("14-09-2026 01:36:02"));
    }

    // CAPTURED shape from ticket 31121 (identity fields redacted - public repo).
    // The service stores every submission ATTEMPT, so one retried invoice came back
    // as FIVE records: four duplicate-rejections with a null CSV, and the original
    // acceptance LAST. Taking Items[0] read a failure and hid the CSV, which is why
    // "Actualizar con los datos de AEAT" was greyed out on an invoice the AEAT
    // demonstrably held. The accepted record must be chosen regardless of position.
    void test_parseQuery_picksAcceptedRecordAmongRetryAttempts()
    {
        auto failed = [](const char *posted) {
            return QStringLiteral(R"({"InvoiceID":"31121","InvoiceDate":"2026-09-08T00:00:00",)"
                   R"("TotalAmount":24.50,"IsRejected":false,"StatusResponse":null,)"
                   R"("ErrorCode":"9999","ErrorDescription":"Ya existe una entrada...",)"
                   R"("CSV":null,"ValidationUrl":null,"PostingDate":"%1"})")
                   .arg(QLatin1String(posted));
        };
        const QString accepted =
            QStringLiteral(R"({"InvoiceID":"31121","InvoiceDate":"2026-09-08T00:00:00",)"
            R"("TotalAmount":24.50,"IsRejected":false,"StatusResponse":"Correcto",)"
            R"("ErrorCode":null,"ErrorDescription":null,"CSV":"A-TESTCSV31121AA",)"
            R"("ValidationUrl":"https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR?nif=B00000000&numserie=31121&fecha=08-09-2026&importe=24.50",)"
            R"("PostingDate":"2026-09-08T13:44:37"})");

        const QByteArray json = QStringLiteral(
            R"({"Offset":0,"Count":5,"Items":[%1,%2,%3,%4,%5],"ResultCode":0,)"
            R"("ResultMessage":"Retrieved element filtered list from domain 'Invoices'."})")
            .arg(failed("2026-09-09T11:56:45"), failed("2026-09-09T10:30:14"),
                 failed("2026-09-14T13:19:36"), failed("2026-09-14T20:27:51"),
                 accepted).toUtf8();

        const VerifactuRemoteRecord r = parseVerifactuQueryResponse(json);
        QVERIFY(r.found);
        QVERIFY(r.parsed);
        QCOMPARE(r.recordCount, 5);
        // The accepted record's data, not the first record's.
        QCOMPARE(r.csv, QStringLiteral("A-TESTCSV31121AA"));
        QVERIFY(r.errorCode.isEmpty());
        QCOMPARE(r.statusResponse, QStringLiteral("Correcto"));
        QVERIFY(r.validationUrl.contains("ValidarQR"));
        QVERIFY(r.hasUsableCsv());
        QVERIFY(verifactuRemoteMatches(r, "31121", "08-09-2026", 24.50));
    }

    // If every returned record is a failure, nothing may be adopted - but the
    // record is still reported so the dialog can show what came back.
    void test_parseQuery_allAttemptsFailed()
    {
        const QByteArray json = R"({"Count":2,"ResultCode":0,"Items":[
            {"InvoiceID":"9","InvoiceDate":"2026-09-08T00:00:00","TotalAmount":10.00,
             "ErrorCode":"9999","CSV":null},
            {"InvoiceID":"9","InvoiceDate":"2026-09-08T00:00:00","TotalAmount":10.00,
             "ErrorCode":"9999","CSV":null}]})";
        const VerifactuRemoteRecord r = parseVerifactuQueryResponse(json);
        QVERIFY(r.found);
        QCOMPARE(r.recordCount, 2);
        QCOMPARE(r.errorCode, QStringLiteral("9999"));
        QVERIFY(!r.hasUsableCsv());
    }

    // A record AEAT rejected can still come back from a query. Its CSV must never
    // be adopted - that would mark us ENVIADA for an invoice AEAT refused.
    void test_parseQuery_rejectedRecordIsNotUsable()
    {
        const QByteArray rejected = R"({"ResultCode":0,"Count":1,"Items":[{)"
            R"("InvoiceID":"9","InvoiceDate":"2026-09-14T00:00:00","TotalAmount":10.00,)"
            R"("CSV":"A-TESTCSV00000002","IsRejected":true,"StatusResponse":"Incorrecto"}]})";
        const VerifactuRemoteRecord r = parseVerifactuQueryResponse(rejected);
        QVERIFY(r.found);
        QVERIFY(r.parsed);
        QVERIFY(!r.hasUsableCsv());          // rejected -> not adoptable
        QVERIFY(!verifactuRemoteMatches(r, "9", "14-09-2026", 10.00) || !r.hasUsableCsv());

        const QByteArray errored = R"({"ResultCode":0,"Count":1,"Items":[{)"
            R"("InvoiceID":"9","InvoiceDate":"2026-09-14T00:00:00","TotalAmount":10.00,)"
            R"("CSV":"A-TESTCSV00000003","IsRejected":false,"ErrorCode":"1102"}]})";
        QVERIFY(!parseVerifactuQueryResponse(errored).hasUsableCsv());
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
