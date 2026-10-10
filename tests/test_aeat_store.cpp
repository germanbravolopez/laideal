// AeatStore: the direct AEAT client's chain head and record outbox, on a throwaway
// SQLite database.

#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "aeatrecord.h"
#include "aeatstore.h"

class TestAeatStore : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QSqlDatabase  m_db;

    // A builder that records the head it was given and returns a record chained to it.
    static std::function<AeatStore::Built(const AeatRecord::PreviousRecord &)>
    builder(const QString &number, const QString &hash, AeatRecord::PreviousRecord *seen = nullptr)
    {
        return [=](const AeatRecord::PreviousRecord &head) {
            if (seen)
                *seen = head;
            AeatStore::Built b;
            b.invoiceNumber = number;
            b.issueDate = QDate(2026, 10, 10);
            b.total = 30.0;
            b.hash = hash;
            b.xml = "<sf:RegistroAlta>" + number + "</sf:RegistroAlta>";
            b.generatedAt = QDateTime(QDate(2026, 10, 10), QTime(10, 0), Qt::OffsetFromUTC, 7200);
            return b;
        };
    }

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_db = QSqlDatabase::addDatabase("QSQLITE", "aeat_store_test");
        m_db.setDatabaseName(m_dir.filePath(QString("store_%1.db").arg(QDateTime::currentMSecsSinceEpoch())));
    }

    void cleanup()
    {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase("aeat_store_test");
    }

    // Each record chains to the previous one; the head moves with every append.
    void test_chainProgresses()
    {
        AeatStore store(m_db, "pruebas");
        QVERIFY(store.ensureSchema());
        QVERIFY(store.ensureSchema());                                   // idempotent
        QVERIFY(store.chainHead("89890001K").isFirst());

        AeatRecord::PreviousRecord seen;
        const AeatStore::Record first = store.append(AeatStore::Kind::Registration, "89890001K", builder("1", "AAA", &seen));
        QVERIFY(first.isValid());
        QVERIFY(seen.isFirst());
        QCOMPARE(first.state, AeatStore::kPending);
        QCOMPARE(first.xml, QStringLiteral("<sf:RegistroAlta>1</sf:RegistroAlta>"));

        store.append(AeatStore::Kind::Registration, "89890001K", builder("2", "BBB", &seen));
        QCOMPARE(seen.hash, QStringLiteral("AAA"));
        QCOMPARE(seen.invoice.invoiceNumber, QStringLiteral("1"));
        QCOMPARE(seen.invoice.issueDate, QDate(2026, 10, 10));
        QCOMPARE(store.chainHead("89890001K").hash, QStringLiteral("BBB"));
        QVERIFY(store.chainHead("B00000000").isFirst());                // one chain per issuer
    }

    // A builder that produces nothing stores nothing and leaves the head alone.
    void test_failedBuildStoresNothing()
    {
        AeatStore store(m_db, "pruebas");
        QVERIFY(store.ensureSchema());
        store.append(AeatStore::Kind::Registration, "89890001K", builder("1", "AAA"));
        const AeatStore::Record none = store.append(AeatStore::Kind::Registration, "89890001K", builder("2", ""));
        QVERIFY(!none.isValid());
        QCOMPARE(store.chainHead("89890001K").hash, QStringLiteral("AAA"));
        QVERIFY(!store.latest(AeatStore::Kind::Registration, "2").isValid());
    }

    // Test and production records never mix, nor do their chains.
    void test_environmentsApart()
    {
        AeatStore test(m_db, "pruebas"), production(m_db, "produccion");
        QVERIFY(test.ensureSchema());
        test.append(AeatStore::Kind::Registration, "89890001K", builder("1", "AAA"));
        QVERIFY(production.chainHead("89890001K").isFirst());
        QVERIFY(!production.latest(AeatStore::Kind::Registration, "1").isValid());
        QVERIFY(production.pending().isEmpty());
        QCOMPARE(test.pending().size(), 1);
    }

    // The chain can start after the last record sent by the gateway, only while empty.
    void test_seedChainHead()
    {
        AeatStore store(m_db, "produccion");
        QVERIFY(store.ensureSchema());
        const AeatRecord::PreviousRecord gateway{ { "89890001K", "30836", QDate(2026, 10, 9) }, "GATEWAYHASH" };
        QVERIFY(!store.seedChainHead("89890001K", {}));                  // nothing to seed
        QVERIFY(store.seedChainHead("89890001K", gateway));
        QVERIFY(!store.seedChainHead("89890001K", { gateway.invoice, "OTHER" }));
        AeatRecord::PreviousRecord seen;
        store.append(AeatStore::Kind::Registration, "89890001K", builder("30837", "NEW", &seen));
        QCOMPARE(seen.hash, QStringLiteral("GATEWAYHASH"));
        QCOMPARE(seen.invoice.invoiceNumber, QStringLiteral("30836"));
    }

    // Outcomes and attempts; pending lists only unconfirmed records, oldest first;
    // latest is per invoice and kind.
    void test_outcomes()
    {
        AeatStore store(m_db, "pruebas");
        QVERIFY(store.ensureSchema());
        const auto a = store.append(AeatStore::Kind::Registration, "89890001K", builder("1", "AAA"));
        const auto b = store.append(AeatStore::Kind::Registration, "89890001K", builder("2", "BBB"));
        const auto c = store.append(AeatStore::Kind::Cancellation, "89890001K", builder("1", "CCC"));
        QVERIFY(store.markAttempt(a.id));
        QVERIFY(store.markOutcome(a.id, AeatStore::kAccepted, "A-CSV", "", ""));
        QVERIFY(store.markOutcome(b.id, AeatStore::kRejected, "", "1100", "ImporteTotal"));
        const QList<AeatStore::Record> pending = store.pending();
        QCOMPARE(pending.size(), 1);
        QCOMPARE(pending[0].id, c.id);
        QCOMPARE(pending[0].kind, AeatStore::Kind::Cancellation);
        const AeatStore::Record latestA = store.latest(AeatStore::Kind::Registration, "1");
        QCOMPARE(latestA.state, AeatStore::kAccepted);
        QCOMPARE(latestA.csv, QStringLiteral("A-CSV"));
        QCOMPARE(latestA.attempts, 1);
        QCOMPARE(store.latest(AeatStore::Kind::Cancellation, "1").hash, QStringLiteral("CCC"));
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "2").errorCode, QStringLiteral("1100"));
        QVERIFY(!store.markOutcome(999, AeatStore::kAccepted, "", "", ""));
        // Empty (null) texts are stored as empty, not refused by the NOT NULL columns.
        QVERIFY(store.markOutcome(c.id, AeatStore::kAccepted, QString(), QString(), QString()));
        QCOMPARE(store.latest(AeatStore::Kind::Cancellation, "1").state, AeatStore::kAccepted);
    }

    // An unreadable chain is an error, never "no chain": nothing is appended.
    void test_unreadableChainRefused()
    {
        AeatStore store(m_db, "pruebas");
        QVERIFY(store.ensureSchema());
        store.append(AeatStore::Kind::Registration, "89890001K", builder("1", "AAA"));
        QVERIFY(m_db.open());
        QSqlQuery(m_db).exec("ALTER TABLE aeat_chain RENAME TO aeat_chain_gone");
        bool ok = true;
        store.chainHead("89890001K", &ok);
        QVERIFY(!ok);
        bool built = false;
        const AeatStore::Record r = store.append(AeatStore::Kind::Registration, "89890001K",
            [&](const AeatRecord::PreviousRecord &p) { built = true; return builder("2", "BBB")(p); });
        QVERIFY(!r.isValid());
        QVERIFY(!built);
        m_db.close();
    }

    // The head can be moved (a sync); the sync flag and the records' XML are kept per environment.
    void test_moveHeadAndSyncFlag()
    {
        AeatStore store(m_db, "pruebas"), other(m_db, "produccion");
        QVERIFY(store.ensureSchema());
        store.append(AeatStore::Kind::Registration, "89890001K", builder("1", "AAA"));
        QVERIFY(store.setChainHead("89890001K", { { "89890001K", "9", QDate(2026, 10, 11) }, "ZZZ", "2026-10-11T10:00:00+02:00" }));
        const AeatRecord::PreviousRecord head = store.chainHead("89890001K");
        QCOMPARE(head.hash, QStringLiteral("ZZZ"));
        QCOMPARE(head.generatedAt, QStringLiteral("2026-10-11T10:00:00+02:00"));
        QVERIFY(!store.chainSynced("89890001K"));
        QVERIFY(store.markChainSynced("89890001K"));
        QVERIFY(store.chainSynced("89890001K") && !other.chainSynced("89890001K"));
        QCOMPARE(store.recordXmls(), QStringList{ "<sf:RegistroAlta>1</sf:RegistroAlta>" });
        QVERIFY(other.recordXmls().isEmpty());
    }

    // An aeat_chain created by an earlier build (no generated_at) is completed, not left unreadable.
    void test_olderChainTableUpgraded()
    {
        QVERIFY(m_db.open());
        QVERIFY(QSqlQuery(m_db).exec("CREATE TABLE aeat_chain (environment TEXT NOT NULL, issuer_nif TEXT NOT NULL, "
                                     "invoice_number TEXT NOT NULL, issue_date TEXT NOT NULL, hash TEXT NOT NULL, "
                                     "PRIMARY KEY (environment, issuer_nif))"));
        QVERIFY(QSqlQuery(m_db).exec("INSERT INTO aeat_chain VALUES ('pruebas', '89890001K', '1', '10-10-2026', 'OLD')"));
        m_db.close();
        AeatStore store(m_db, "pruebas");
        QVERIFY(store.ensureSchema());
        bool ok = false;
        QCOMPARE(store.chainHead("89890001K", &ok).hash, QStringLiteral("OLD"));
        QVERIFY(ok);
        QVERIFY(store.append(AeatStore::Kind::Registration, "89890001K", builder("2", "NEW")).isValid());
    }

    // An open connection stays open (the app's models read through it); a closed one is closed again.
    void test_connectionStateKept()
    {
        AeatStore store(m_db, "pruebas");
        QVERIFY(!m_db.isOpen());
        QVERIFY(store.ensureSchema());
        QVERIFY(!m_db.isOpen());
        QVERIFY(m_db.open());
        store.append(AeatStore::Kind::Registration, "89890001K", builder("1", "AAA"));
        store.pending();
        QVERIFY(m_db.isOpen());
    }
};

QTEST_GUILESS_MAIN(TestAeatStore)
#include "test_aeat_store.moc"
