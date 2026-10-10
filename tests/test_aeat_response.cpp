// AeatResponse: replies of the AEAT web service to the direct client, read from the
// fixtures in tests/fixtures/aeat-responses (validated against the official AEAT
// response schemas by the aeat_xsd ctest entry), and their mapping onto the app's
// VerifactuResult / VerifactuRemoteRecord.

#include <QtTest>

#include "aeatresponse.h"

using namespace AeatResponse;

class TestAeatResponse : public QObject
{
    Q_OBJECT

private:
    static QByteArray fixture(const QString &name)
    {
        QFile f(QStringLiteral(AEAT_RESPONSES_DIR "/") + name);
        if (!f.open(QIODevice::ReadOnly))
            qWarning() << "missing fixture" << f.fileName();
        return f.readAll();
    }

private slots:
    void test_accepted()
    {
        const Submission r = parseSubmission(fixture("reply_accepted.xml"));
        QVERIFY(r.parsed && !r.fault);
        QCOMPARE(r.csv, QStringLiteral("A-YDSW8NLFLANWPM"));
        QCOMPARE(r.waitSeconds, 60);
        QCOMPARE(r.sendState, QStringLiteral("Correcto"));
        QCOMPARE(r.presentedAt, QStringLiteral("2026-10-10T10:00:05+02:00"));
        QCOMPARE(r.lines.size(), 1);
        QCOMPARE(r.lines[0].invoiceNumber, QStringLiteral("30837"));
        QCOMPARE(r.lines[0].issueDate, QStringLiteral("10-10-2026"));
        QCOMPARE(r.lines[0].operation, QStringLiteral("Alta"));
        const VerifactuResult v = resultFor(r, "30837", "Alta");
        QVERIFY(v.isSuccess());
        QCOMPARE(v.csv, QStringLiteral("A-YDSW8NLFLANWPM"));
        QVERIFY(v.errorCode.isEmpty());
    }

    // Accepted with errors is accepted: AEAT registered it; the warning is kept.
    void test_acceptedWithErrors()
    {
        const VerifactuResult v = resultFor(parseSubmission(fixture("reply_accepted_with_errors.xml")), "30837", "Alta");
        QVERIFY(v.isSuccess());
        QCOMPARE(v.errorCode, QStringLiteral("2007"));
        QVERIFY(v.errorDescription.contains("FechaOperacion"));
    }

    // A partly correct batch: each record gets its own outcome; the wait time is read.
    void test_partial()
    {
        const Submission r = parseSubmission(fixture("reply_partial.xml"));
        QCOMPARE(r.waitSeconds, 30);
        QCOMPARE(r.lines.size(), 2);
        QVERIFY(resultFor(r, "30837", "Alta").isSuccess());
        const VerifactuResult rejected = resultFor(r, "30838", "Alta");
        QCOMPARE(rejected.status, VerifactuResult::ERROR);
        QCOMPARE(rejected.errorCode, QStringLiteral("1100"));
        QVERIFY(rejected.errorDescription.contains("ImporteTotal"));
        // No line for that invoice / operation: an error, never a success.
        QCOMPARE(resultFor(r, "30837", "Anulacion").status, VerifactuResult::ERROR);
        QCOMPARE(resultFor(r, "99999", "Alta").status, VerifactuResult::ERROR);
    }

    // A duplicate of a record AEAT already accepted is a success, identified by the earlier request.
    void test_duplicateOfAccepted()
    {
        const Submission r = parseSubmission(fixture("reply_duplicate.xml"));
        QVERIFY(r.csv.isEmpty());
        QVERIFY(r.lines[0].duplicate);
        QCOMPARE(r.lines[0].duplicateRequestId, QStringLiteral("202610100000123"));
        const VerifactuResult v = resultFor(r, "30837", "Alta");
        QVERIFY(v.isSuccess());
        QCOMPARE(v.csv, QStringLiteral("IdPeticion 202610100000123"));
        QCOMPARE(v.errorCode, QStringLiteral("3000"));

        Submission cancelledEarlier = r;
        cancelledEarlier.lines[0].duplicateState = "Anulada";
        QCOMPARE(resultFor(cancelledEarlier, "30837", "Alta").status, VerifactuResult::ERROR);
    }

    void test_cancellationLine()
    {
        const Submission r = parseSubmission(fixture("reply_cancelled.xml"));
        QVERIFY(resultFor(r, "30837", "Anulacion").isSuccess());
        QCOMPARE(resultFor(r, "30837", "Alta").status, VerifactuResult::ERROR);
    }

    // A SOAP fault refuses the whole request; its text reaches the result.
    void test_fault()
    {
        const Submission r = parseSubmission(fixture("nonschema_fault.xml"));
        QVERIFY(r.parsed && r.fault);
        const VerifactuResult v = resultFor(r, "30837", "Alta");
        QCOMPARE(v.status, VerifactuResult::ERROR);
        QVERIFY(v.errorDescription.contains("4102"));
        QVERIFY(parseQuery(fixture("nonschema_fault.xml")).fault);
    }

    // Not XML, or XML of something else: not parsed, never a success.
    void test_garbage()
    {
        QVERIFY(!parseSubmission("<html><body>502 Bad Gateway</body></html>").parsed);
        QVERIFY(!parseSubmission("not xml at all <").parsed);
        QCOMPARE(resultFor(parseSubmission(""), "30837", "Alta").status, VerifactuResult::ERROR);
    }

    // The query keeps the record's own hash, not the previous one from its chain block.
    void test_queryFound()
    {
        const Query q = parseQuery(fixture("query_found.xml"));
        QVERIFY(q.parsed && q.hasData && !q.morePages);
        QCOMPARE(q.records.size(), 1);
        const QueryRecord &r = q.records[0];
        QCOMPARE(r.invoiceNumber, QStringLiteral("30837"));
        QCOMPARE(r.issueDate, QStringLiteral("10-10-2026"));
        QCOMPARE(r.invoiceType, QStringLiteral("F2"));
        QCOMPARE(r.totalAmount, QStringLiteral("30.00"));
        QCOMPARE(r.hash, QStringLiteral("9F2C3E71A5B04D8E6C1F2A3B4C5D6E7F8091A2B3C4D5E6F708192A3B4C5D6E7F"));
        QCOMPARE(r.state, QStringLiteral("Correcto"));
        QCOMPARE(r.requestId, QStringLiteral("202610100000123"));

        const VerifactuRemoteRecord remote = remoteRecordFor(q, "30837");
        QVERIFY(remote.hasUsableCsv());
        QCOMPARE(remote.totalAmount, 30.0);
        QCOMPARE(remote.invoiceDate, QStringLiteral("10-10-2026"));
        QCOMPARE(remote.csv, QStringLiteral("IdPeticion 202610100000123"));
        QVERIFY(!remoteRecordFor(q, "30838").found);
    }

    // A cancelled registration is found but never adopted as sent.
    void test_queryCancelled()
    {
        const VerifactuRemoteRecord remote = remoteRecordFor(parseQuery(fixture("query_cancelled.xml")), "30837");
        QVERIFY(remote.found);
        QVERIFY(!remote.hasUsableCsv());
    }

    void test_queryEmpty()
    {
        const Query q = parseQuery(fixture("query_empty.xml"));
        QVERIFY(q.parsed && !q.hasData && q.records.isEmpty());
        const VerifactuRemoteRecord remote = remoteRecordFor(q, "30837");
        QVERIFY(remote.parsed && !remote.found);
    }
};

QTEST_GUILESS_MAIN(TestAeatResponse)
#include "test_aeat_response.moc"
