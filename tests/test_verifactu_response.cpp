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
};

QTEST_MAIN(TestVerifactuResponse)
#include "test_verifactu_response.moc"
