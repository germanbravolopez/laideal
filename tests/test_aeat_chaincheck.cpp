// AeatChainCheck: recomputes the huella of every stored record and checks the links.

#include <QtTest>

#include "aeatchaincheck.h"
#include "aeatrecord.h"
#include "aeatresponse.h"

using namespace AeatRecord;

class TestAeatChainCheck : public QObject
{
    Q_OBJECT

private:
    static SystemInfo system() { return { "Germán Bravo López", "12345678Z", "LAIDEAL", "LI", "11.0", "LAIDEAL-0001" }; }

    static QDateTime at(int s) { return QDateTime(QDate(2026, 10, 10), QTime(10, 0, s), Qt::OffsetFromUTC, 7200); }

    static InvoiceRef ref(const QString &number) { return { "89890001K", number, QDate(2026, 10, 10) }; }

    static PreviousRecord link(const QString &number, const QString &hash)
    {
        PreviousRecord p;
        p.invoice = ref(number);
        p.hash = hash;
        return p;
    }

    static Registration alta(const QString &number, double base, const PreviousRecord &previous, int second)
    {
        Registration r;
        r.invoice = ref(number);
        r.issuerName = "Tintorería La Ideal";
        r.invoiceType = "F2";
        r.description = "Servicios de lavandería";
        r.taxLines = { { 21.0, base, base * 0.21 } };
        r.previous = previous;
        r.generatedAt = at(second);
        return r;
    }

    // alta, alta, anulación, rectificativa chained through their hashes.
    static QStringList chain()
    {
        const Registration a = alta("A-1", 10.0, {}, 0);
        const Registration b = alta("A-2", 20.0, link("A-1", hashOf(a)), 1);
        Cancellation c;
        c.invoice = ref("A-1");
        c.previous = link("A-2", hashOf(b));
        c.generatedAt = at(2);
        Registration r = alta("R-1", -5.0, link("A-1", hashOf(c)), 3);
        r.invoiceType = "R5";
        r.rectificationType = "I";
        r.rectifiedInvoices = { ref("A-2") };
        return { registrationXml(a, system()), registrationXml(b, system()), cancellationXml(c, system()),
                 registrationXml(r, system()) };
    }

private slots:
    void test_validChainPasses()
    {
        const AeatChainCheck::Result r = AeatChainCheck::verify(chain());
        QVERIFY2(r.problem.isEmpty(), qPrintable(r.problem));
        QVERIFY(r.ok);
        QCOMPARE(r.checked, 4);
        QCOMPARE(r.failedIndex, -1);
    }

    void test_emptyChainPasses()
    {
        const AeatChainCheck::Result r = AeatChainCheck::verify({});
        QVERIFY(r.ok);
        QCOMPARE(r.checked, 0);
    }

    void test_editedXmlDetected()
    {
        QStringList records = chain();
        records[1].replace("ImporteTotal>24.20", "ImporteTotal>14.20");
        QVERIFY(records[1] != chain().at(1));
        const AeatChainCheck::Result r = AeatChainCheck::verify(records);
        QVERIFY(!r.ok);
        QCOMPARE(r.failedIndex, 1);
        QVERIFY2(r.problem.contains("La huella del registro de la factura A-2 no coincide"), qPrintable(r.problem));
    }

    // Records out of order: a link to another stored record than the previous one.
    void test_brokenLinkDetected()
    {
        QStringList records = chain();
        records.move(1, 2);                                     // A-1, anulación of A-1, A-2, R-1
        const AeatChainCheck::Result r = AeatChainCheck::verify(records);
        QVERIFY(!r.ok);
        QCOMPARE(r.failedIndex, 1);
        QVERIFY2(r.problem.contains("El registro de la factura A-1 no enlaza con el anterior"), qPrintable(r.problem));
    }

    // A second PrimerRegistro restarts the chain: never valid after a stored record.
    void test_restartDetected()
    {
        QStringList records = chain();
        records << registrationXml(alta("A-9", 5.0, {}, 9), system());
        const AeatChainCheck::Result r = AeatChainCheck::verify(records);
        QVERIFY(!r.ok);
        QCOMPARE(r.failedIndex, 4);
    }

    // A record continuing one this database does not hold (a restored copy, the rest at
    // AEAT) is counted, not reported as an alteration; every record is still checked.
    void test_continuationOfARecordOnlyAtAeat()
    {
        QStringList records = chain();
        records.removeAt(2);
        const AeatChainCheck::Result r = AeatChainCheck::verify(records);
        QVERIFY2(r.ok, qPrintable(r.problem));
        QCOMPARE(r.continuations, 1);
        QCOMPARE(r.checked, 3);
        QCOMPARE(AeatChainCheck::verify(chain()).continuations, 0);
    }

    // With the anchors a sync adopted, a link to a record this database does not hold passes
    // only when it is an anchor: a deleted record is then reported.
    void test_anchorsMakeADeletedRecordVisible()
    {
        QStringList records = chain();
        records.removeAt(2);                                    // the anulación is gone
        const QSet<QString> none;
        const AeatChainCheck::Result strict = AeatChainCheck::verify(records, &none);
        QVERIFY(!strict.ok);
        QCOMPARE(strict.failedIndex, 2);
        const AeatChainCheck::Result lenient = AeatChainCheck::verify(records);
        QVERIFY(lenient.ok);
        QCOMPARE(lenient.continuationHashes.size(), 1);
        const QSet<QString> adopted{ lenient.continuationHashes.first() };
        QVERIFY(AeatChainCheck::verify(records, &adopted).ok);   // an adopted AEAT head
    }

    // A sync that moved the head back to an older stored record (the newest never reached
    // AEAT) records it as an anchor: the next record linking to it is no alteration.
    void test_anchoredHeadMovedBack()
    {
        QStringList records = chain().mid(0, 2);                 // A-1, A-2
        const QString firstHash = AeatRecord::hashOf(alta("A-1", 10.0, {}, 0));
        records << registrationXml(alta("A-3", 30.0, link("A-1", firstHash), 4), system());
        QVERIFY(!AeatChainCheck::verify(records).ok);
        const QSet<QString> adopted{ firstHash.toUpper() };
        QVERIFY(AeatChainCheck::verify(records, &adopted).ok);
    }

    // The newest record deleted: every link still holds, but the head is not the stored tail.
    void test_deletedNewestRecordDetected()
    {
        const QStringList records = chain();
        const QString head = AeatResponse::summarizeRecord(records.last()).hash;
        QVERIFY(AeatChainCheck::verifyTail(records, "89890001K", head, {}).ok);
        const QStringList shortened = records.mid(0, 3);
        QVERIFY(AeatChainCheck::verify(shortened).ok);
        const AeatChainCheck::Result r = AeatChainCheck::verifyTail(shortened, "89890001K", head, {});
        QVERIFY(!r.ok);
        QVERIFY(!r.problem.isEmpty());
        QVERIFY(AeatChainCheck::verifyTail(shortened, "89890001K", head, { head.toUpper() }).ok);  // an adopted head
        QVERIFY(AeatChainCheck::verifyTail({}, "89890001K", QString(), {}).ok);                     // chain not started
        QVERIFY(!AeatChainCheck::verifyTail({}, "89890001K", head, {}).ok);                         // all deleted
    }

    // Each issuer is its own chain, even interleaved in the store.
    void test_issuersCheckedSeparately()
    {
        Registration other = alta("B-1", 7.0, {}, 5);
        other.invoice.issuerNif = "B12345674";
        QStringList records = chain();
        records.insert(1, registrationXml(other, system()));
        const AeatChainCheck::Result r = AeatChainCheck::verify(records);
        QVERIFY2(r.ok, qPrintable(r.problem));
        QCOMPARE(r.checked, 5);
    }
};

QTEST_APPLESS_MAIN(TestAeatChainCheck)
#include "test_aeat_chaincheck.moc"
