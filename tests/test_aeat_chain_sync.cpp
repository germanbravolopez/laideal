// AeatChainSync: which record is the tip of the chain, from plain data. Pure, no store, no
// transport; the same rules run through the whole backend in test_aeat_direct_backend.

#include <QtTest>

#include "aeatchainsync.h"

using AeatResponse::RecordSummary;
using AeatChainSync::Plan;

class TestAeatChainSync : public QObject
{
    Q_OBJECT

private:
    static QString at(int second, int minute = 0)
    {
        return QStringLiteral("2026-10-10T10:%1:%2+02:00").arg(minute, 2, 10, QLatin1Char('0')).arg(second, 2, 10, QLatin1Char('0'));
    }

    // A registration of our system as AEAT's query returns it.
    static RecordSummary reg(const QString &hash, const QString &previous, const QString &invoice, const QString &generatedAt,
                             const QString &installation = QString())
    {
        RecordSummary r;
        r.valid = true;
        r.operation = QStringLiteral("Alta");
        r.issuerNif = QStringLiteral("89890001K");
        r.invoiceNumber = invoice;
        r.issueDate = QStringLiteral("10-10-2026");
        r.hash = hash;
        r.previousHash = previous;
        r.generatedAt = generatedAt;
        r.installationNumber = installation;
        r.presentedAt = generatedAt;
        return r;
    }

    // A cancelled invoice whose cancellation record AEAT hides.
    static RecordSummary hidden(const QString &hash, const QString &invoice, const QString &cancelledAt)
    {
        RecordSummary r = reg(hash, QString(), invoice, at(0));
        r.cancelledAtAeat = true;
        r.cancelledAt = cancelledAt;
        return r;
    }

    static AeatRecord::PreviousRecord head(const QString &hash, const QString &invoice, const QString &generatedAt)
    {
        AeatRecord::PreviousRecord p;
        p.invoice = { QStringLiteral("89890001K"), invoice, QDate(2026, 10, 10) };
        p.hash = hash;
        p.generatedAt = generatedAt;
        return p;
    }

    static AeatChainSync::Input input(const QList<RecordSummary> &candidates)
    {
        AeatChainSync::Input in;
        in.candidates = candidates;
        in.issuerNif = QStringLiteral("89890001K");
        in.installationNumber = QStringLiteral("LAIDEAL-0001");
        return in;
    }

private slots:
    void continuesFromTheNewestRecord()
    {
        AeatChainSync::Input in = input({ reg("A1", "", "T1", at(1)), reg("B2", "A1", "T2", at(2)) });
        in.head = head("A1", "T1", at(1));
        const Plan p = AeatChainSync::plan(in);
        QCOMPARE(p.kind, Plan::Kind::Continue);
        QCOMPARE(p.tip.hash, QStringLiteral("B2"));
        QVERIFY(p.headMoves);
    }

    void headThatIsTheTipDoesNotMove()
    {
        AeatChainSync::Input in = input({ reg("a1", "", "T1", at(1)) });
        in.head = head("A1", "T1", at(1));
        const Plan p = AeatChainSync::plan(in);
        QCOMPARE(p.kind, Plan::Kind::Continue);
        QVERIFY2(!p.headMoves, "huellas compare without case");
    }

    void storedHeadIsTheTipWhenAeatShowsNothingNewer()
    {
        AeatChainSync::Input in = input({});
        in.head = head("A1", "T1", at(1));
        const Plan p = AeatChainSync::plan(in);
        QCOMPARE(p.kind, Plan::Kind::Continue);
        QCOMPARE(p.tip.invoiceNumber, QStringLiteral("T1"));
        QVERIFY(!p.headMoves);
    }

    void firstSyncWithEmptyAeatStartsTheChain()
    {
        AeatChainSync::Input in = input({});
        in.mayRestart = true;
        QCOMPARE(AeatChainSync::plan(in).kind, Plan::Kind::NoRecords);
    }

    void recordsBeforeARestartAreDropped()
    {
        const QDateTime restarted = QDateTime::fromString(at(5), Qt::ISODate);
        const QList<RecordSummary> kept = AeatChainSync::candidatesAfterRestart(
            { reg("O1", "", "T1", at(1), "OLD"), reg("S1", "", "T2", at(5), "OLD"), reg("S2", "", "T3", at(5), "NEW"),
              reg("N1", "S2", "T4", at(6), "NEW") },
            restarted, QStringLiteral("NEW"));
        QStringList hashes;
        for (const RecordSummary &r : kept)
            hashes << r.hash;
        QCOMPARE(hashes, QStringList({ "S2", "N1" }));
    }

    void noRestartKeepsEveryCandidate()
    {
        QCOMPARE(AeatChainSync::candidatesAfterRestart({ reg("O1", "", "T1", at(1)) }, QDateTime(), QString()).size(), 1);
    }

    void unreadableTimeIsNeverALinkAfterARestart()
    {
        const QDateTime restarted = QDateTime::fromString(at(5), Qt::ISODate);
        QVERIFY(AeatChainSync::candidatesAfterRestart({ reg("X1", "", "T1", QString(), "NEW") }, restarted,
                                                      QStringLiteral("NEW")).isEmpty());
    }

    void anotherSystemsRegistrationIsNeverALink()
    {
        RecordSummary foreign = reg("F9", "A1", "G1", at(9));
        foreign.foreignSystem = true;
        AeatChainSync::Input in = input({ reg("A1", "", "T1", at(1)), foreign });
        in.head = head("A1", "T1", at(1));
        const Plan p = AeatChainSync::plan(in);
        QCOMPARE(p.kind, Plan::Kind::Continue);
        QCOMPARE(p.tip.hash, QStringLiteral("A1"));
    }

    void unprovenReturnedCancellationIsDemoted()
    {
        RecordSummary cancel = reg("C1", "Z9", "T2", at(2));
        cancel.operation = QStringLiteral("Anulacion");
        cancel.cancellationFromQuery = true;
        QList<RecordSummary> candidates = { reg("A1", "", "T1", at(1)), cancel };
        const QList<RecordSummary> chain = AeatChainSync::chainOf(
            candidates, AeatChainSync::provenOurs(candidates, { "A1" }), AeatRecord::PreviousRecord(), "89890001K");
        QCOMPARE(chain.size(), 1);
        QVERIFY(candidates.at(1).cancelledAtAeat);
        QCOMPARE(candidates.at(1).operation, QStringLiteral("Alta"));
    }

    void returnedCancellationChainingToOursIsProven()
    {
        RecordSummary cancel = reg("C1", "a1", "T1", at(2));
        cancel.operation = QStringLiteral("Anulacion");
        cancel.cancellationFromQuery = true;
        const QSet<QString> ours = AeatChainSync::provenOurs({ reg("A1", "", "T1", at(1)), cancel }, {});
        QVERIFY(ours.contains("C1"));
        AeatChainSync::Input in = input({ reg("A1", "", "T1", at(1)), cancel });
        QCOMPARE(AeatChainSync::plan(in).tip.hash, QStringLiteral("C1"));
    }

    void lostCancellationOnAFirstSyncRestartsTheChain()
    {
        AeatChainSync::Input in = input({ hidden("H1", "T7", at(0, 5)) });
        in.mayRestart = true;
        const Plan p = AeatChainSync::plan(in);
        QCOMPARE(p.kind, Plan::Kind::Restart);
        QCOMPARE(p.lostInvoice, QStringLiteral("T7"));
    }

    void lostCancellationOutsideAFirstSyncIsIgnored()
    {
        AeatChainSync::Input in = input({ hidden("H1", "T7", at(0, 5)) });
        in.mayRestart = false;
        QCOMPARE(AeatChainSync::plan(in).kind, Plan::Kind::Continue);    // the registration itself is the tip
    }

    void cancellationOlderThanTheTipIsNotLost()
    {
        RecordSummary tip = reg("A1", "", "T1", at(0, 9));
        AeatChainSync::Input in = input({ tip, hidden("H1", "T7", at(0, 5)) });
        in.mayRestart = true;
        QCOMPARE(AeatChainSync::plan(in).kind, Plan::Kind::Continue);
        in.candidates = { tip, hidden("H1", "T7", at(0, 15)) };
        QCOMPARE(AeatChainSync::plan(in).kind, Plan::Kind::Restart);
    }

    void markerNamingTheTipLeavesNoLostRecord()
    {
        const RecordSummary tip = reg("A1", "", "T1", at(0, 9));
        const RecordSummary lost = hidden("H1", "T7", at(0, 15));
        const QList<RecordSummary> candidates = { tip, lost };
        QVERIFY(!AeatChainSync::lostRecord(candidates, { tip }, tip, "a1"));
        QVERIFY(AeatChainSync::lostRecord(candidates, { tip }, tip, "ZZ"));          // names no visible record
        QVERIFY(AeatChainSync::lostRecord(candidates, { tip }, tip, QString()));     // no marker: AEAT's times
    }

    void adoptsTheChainsInstallationNumber()
    {
        AeatChainSync::Input in = input({ reg("A1", "", "T1", at(1), "LAIDEAL-0007") });
        const Plan p = AeatChainSync::plan(in);
        QCOMPARE(p.installationNumberToAdopt, QStringLiteral("LAIDEAL-0007"));
    }

    void keepsTheInstallationNumberWhenAdoptingIsOff()
    {
        AeatChainSync::Input in = input({ reg("A1", "", "T1", at(1), "LAIDEAL-0007") });
        in.adoptInstallationNumber = false;
        QVERIFY(AeatChainSync::plan(in).installationNumberToAdopt.isEmpty());
        in.adoptInstallationNumber = true;
        in.installationNumber = QStringLiteral("LAIDEAL-0007");
        QVERIFY(AeatChainSync::plan(in).installationNumberToAdopt.isEmpty());
    }

    void cancellationTipTakesTheNewestRegistrationsNumber()
    {
        RecordSummary cancel = reg("C1", "a2", "T2", at(3), "OTHER");
        cancel.operation = QStringLiteral("Anulacion");
        cancel.cancellationFromQuery = true;
        const QList<RecordSummary> chain = { reg("A1", "", "T1", at(1), "OLD-1"), reg("A2", "A1", "T3", at(2), "NEW-2"), cancel };
        QCOMPARE(AeatChainSync::chainInstallationNumber(chain, cancel), QStringLiteral("NEW-2"));
        QCOMPARE(AeatChainSync::chainInstallationNumber(chain, chain.at(0)), QStringLiteral("OLD-1"));
    }

    void continuationMessageNamesTheRecord()
    {
        const RecordSummary tip = reg("A1", "", "T1", at(1));
        QVERIFY(AeatChainSync::continuationMessage(tip, false).contains("T1"));
        QVERIFY(AeatChainSync::continuationMessage(tip, false).contains("alta"));
        QVERIFY(AeatChainSync::continuationMessage(tip, true).contains("anulación"));
        QVERIFY(AeatChainSync::continuationMessage(reg("A1", "", "T1", QString()), false).contains("generado antes"));
    }
};

QTEST_APPLESS_MAIN(TestAeatChainSync)
#include "test_aeat_chain_sync.moc"
