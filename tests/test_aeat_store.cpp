// AeatStore: the direct AEAT client's chain head and record outbox, on a throwaway
// SQLite database.

#include <QtTest>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "aeatrecord.h"
#include "aeatstore.h"
#include "sql_lite.h"

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

    // A record whose XML carries `installation` in its SistemaInformatico block, as the real ones.
    static std::function<AeatStore::Built(const AeatRecord::PreviousRecord &)>
    builderWithInstallation(const QString &number, const QString &hash, const QString &installation)
    {
        return [=](const AeatRecord::PreviousRecord &head) {
            AeatStore::Built b = builder(number, hash)(head);
            b.xml = "<sf:RegistroAlta><sf:SistemaInformatico><sf:IdSistemaInformatico>LI</sf:IdSistemaInformatico>"
                    "<sf:NumeroInstalacion>" + installation + "</sf:NumeroInstalacion></sf:SistemaInformatico>"
                    "</sf:RegistroAlta>";
            return b;
        };
    }

    int installationRows()
    {
        const bool wasOpen = m_db.isOpen();
        if (!wasOpen && !m_db.open())
            return -1;
        int rows = -1;
        {
            QSqlQuery q(m_db);
            if (q.exec("SELECT COUNT(*) FROM aeat_installation") && q.next())
                rows = q.value(0).toInt();
        }
        if (!wasOpen)
            m_db.close();
        return rows;
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
        QCOMPARE(production.pendingCount(), 0);
        // Unreadable is not "none": the notice of unsent records must not vanish on it.
        QVERIFY(m_db.open());
        QVERIFY(QSqlQuery(m_db).exec("ALTER TABLE aeat_records RENAME TO aeat_records_away"));
        QCOMPARE(production.pendingCount(), -1);
        QVERIFY(QSqlQuery(m_db).exec("ALTER TABLE aeat_records_away RENAME TO aeat_records"));
        m_db.close();
        QCOMPARE(test.pendingCount(), 1);
        QCOMPARE(test.pendingCount("89890001K"), 1);
        QCOMPARE(test.pendingCount("B11111111"), 0);
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
        QCOMPARE(store.pendingCount(), 1);
        QVERIFY(!pending[0].delayed);
        QVERIFY(store.markDelayed(c.id));
        QVERIFY(store.pending()[0].delayed);
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

    // A restart clears the head, renews the installation number and is recorded per environment.
    void test_restartChain()
    {
        AeatStore store(m_db, "pruebas"), other(m_db, "produccion");
        QVERIFY(store.ensureSchema());
        store.append(AeatStore::Kind::Registration, "89890001K", builder("1", "AAA"));
        QVERIFY(!store.chainHead("89890001K").isFirst());
        QVERIFY(!store.chainRestartedAt("89890001K").isValid());
        const QString number = AeatStore::newInstallationNumber();
        QVERIFY(number != AeatStore::newInstallationNumber());
        QVERIFY(!store.restartChain("89890001K", " "));
        const QString previous = store.installationNumber();
        QVERIFY(store.restartChain("89890001K", number, "31277"));
        bool read = false;
        QVERIFY(store.chainHead("89890001K", &read).isFirst());
        QVERIFY(read);
        QCOMPARE(store.installationNumber(), number);
        QString restartNumber;
        QVERIFY(store.chainRestartedAt("89890001K", &restartNumber).isValid());
        QCOMPARE(restartNumber, number);
        QVERIFY(!store.chainRestartedAt("00000000T").isValid());
        QCOMPARE(store.chainRestarts(), QSet<QString>{ "89890001K|" + number });
        // The trace: which number it replaced and why, with a UTC time.
        QVERIFY(m_db.open());
        {
            QSqlQuery q(m_db);
            QVERIFY(q.exec("SELECT previous_number, cancelled_invoice, restarted_at FROM aeat_chain_restarts") && q.next());
            QCOMPARE(q.value(0).toString(), previous);
            QCOMPARE(q.value(1).toString(), QStringLiteral("31277"));
            QVERIFY(q.value(2).toString().endsWith(QLatin1Char('Z')));
        }
        m_db.close();
        QVERIFY(other.chainRestarts().isEmpty());
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

    // An aeat_records made before the reached column is completed once, and again is a no-op.
    void test_olderRecordsTableGetsReachedColumn()
    {
        QVERIFY(m_db.open());
        QVERIFY(QSqlQuery(m_db).exec("CREATE TABLE aeat_records (id INTEGER PRIMARY KEY AUTOINCREMENT, environment TEXT NOT NULL,"
                                     " kind TEXT NOT NULL, issuer_nif TEXT NOT NULL, invoice_number TEXT NOT NULL,"
                                     " issue_date TEXT NOT NULL, total REAL NOT NULL DEFAULT 0, hash TEXT NOT NULL,"
                                     " xml TEXT NOT NULL, generated_at TEXT NOT NULL, state TEXT NOT NULL,"
                                     " csv TEXT NOT NULL DEFAULT '', error_code TEXT NOT NULL DEFAULT '',"
                                     " error_description TEXT NOT NULL DEFAULT '', attempts INTEGER NOT NULL DEFAULT 0,"
                                     " last_attempt TEXT NOT NULL DEFAULT '')"));
        m_db.close();
        AeatStore store(m_db, "pruebas");
        QVERIFY(store.ensureSchema());
        QVERIFY(store.ensureSchema());
        QVERIFY(store.lastError().isEmpty());
        const auto a = store.append(AeatStore::Kind::Registration, "89890001K", builder("1", "AAA"));
        QVERIFY(a.isValid());
        QVERIFY(store.markReached(a.id));
        QVERIFY(store.latest(AeatStore::Kind::Registration, "1").reached);
    }

    // reached: 0 not sent, 2 sending, 1 may have reached AEAT; a cut-off send counts as reached.
    void test_reachedStateMachine()
    {
        AeatStore store(m_db, "pruebas");
        QVERIFY(store.ensureSchema());
        const auto a = store.append(AeatStore::Kind::Registration, "89890001K", builder("1", "AAA"));
        const auto reached = [&] { return store.latest(AeatStore::Kind::Registration, "1").reached; };
        QVERIFY(!reached());
        QVERIFY(store.markSending(a.id));
        QVERIFY(!reached());                      // 2: only the send in progress
        QVERIFY(store.markNotSent(a.id));         // never left: back to 0
        QVERIFY(store.markSending(a.id));
        QVERIFY(store.markSending(a.id));         // the first was cut off: it may have reached
        QVERIFY(reached());
        QVERIFY(store.markNotSent(a.id));         // only undoes a send in progress
        QVERIFY(reached());
        const auto b = store.append(AeatStore::Kind::Registration, "89890001K", builder("2", "BBB"));
        QVERIFY(store.markReached(b.id));
        QVERIFY(store.latest(AeatStore::Kind::Registration, "2").reached);
    }

    // NumeroInstalacion: created once per database, the same for every store on it,
    // whatever the environment (AEAT keys the system on it: it may never change).
    void test_installationNumberCreatedOnce()
    {
        AeatStore store(m_db, "pruebas");
        QVERIFY(store.ensureSchema());
        QCOMPARE(installationRows(), 0);                                 // nothing until first use
        const QString number = store.installationNumber();
        QVERIFY2(QRegularExpression("^LAIDEAL-[0-9A-F]{8}$").match(number).hasMatch(), qPrintable(number));
        QCOMPARE(store.installationNumber(), number);
        QCOMPARE(AeatStore(m_db, "pruebas").installationNumber(), number);
        QCOMPARE(AeatStore(m_db, "produccion").installationNumber(), number);
        QCOMPARE(installationRows(), 1);
        QVERIFY(!m_db.isOpen());                                         // the closed connection is closed again
        // Records generated later never change it.
        store.append(AeatStore::Kind::Registration, "89890001K", builderWithInstallation("1", "AAA", "ANOTHER-1"));
        QCOMPARE(store.installationNumber(), number);
    }

    // Two databases never share a generated number.
    void test_installationNumberIsPerDatabase()
    {
        AeatStore store(m_db, "pruebas");
        QVERIFY(store.ensureSchema());
        const QString number = store.installationNumber();
        QVERIFY(!number.isEmpty());
        {
            QSqlDatabase other = QSqlDatabase::addDatabase("QSQLITE", "aeat_store_other");
            other.setDatabaseName(m_dir.filePath(QString("other_%1.db").arg(QDateTime::currentMSecsSinceEpoch())));
            AeatStore otherStore(other, "pruebas");
            QVERIFY(otherStore.ensureSchema());
            const QString otherNumber = otherStore.installationNumber();
            QVERIFY(!otherNumber.isEmpty());
            QVERIFY(otherNumber != number);
            other.close();
        }
        QSqlDatabase::removeDatabase("aeat_store_other");
    }

    // A database that already sent records (made before the number was stored) adopts the
    // number of its newest record, of any environment or issuer: what AEAT holds for the chain.
    void test_installationNumberAdoptedFromNewestRecord()
    {
        AeatStore test(m_db, "pruebas"), production(m_db, "produccion");
        QVERIFY(test.ensureSchema());
        test.append(AeatStore::Kind::Registration, "89890001K", builderWithInstallation("1", "AAA", "LAIDEAL-OLDEST"));
        production.append(AeatStore::Kind::Registration, "B00000000", builderWithInstallation("2", "BBB", "LAIDEAL-SENT0001"));
        QCOMPARE(installationRows(), 0);
        QCOMPARE(test.installationNumber(), QStringLiteral("LAIDEAL-SENT0001"));
        QCOMPARE(production.installationNumber(), QStringLiteral("LAIDEAL-SENT0001"));
        QCOMPARE(installationRows(), 1);
    }

    // The same for a database of an earlier build, which has no aeat_installation table yet.
    void test_installationNumberAdoptedByOlderDatabase()
    {
        {
            AeatStore old(m_db, "pruebas");
            QVERIFY(old.ensureSchema());
            old.append(AeatStore::Kind::Registration, "89890001K", builderWithInstallation("1", "AAA", "LAIDEAL-0BADC0DE"));
            // The newest record carries none (never the case in real ones): the one before it counts.
            old.append(AeatStore::Kind::Registration, "89890001K", builder("2", "BBB"));
            QVERIFY(m_db.open());
            QVERIFY(QSqlQuery(m_db).exec("DROP TABLE aeat_installation"));
            m_db.close();
        }
        AeatStore store(m_db, "pruebas");
        QVERIFY(store.ensureSchema());
        QCOMPARE(store.installationNumber(), QStringLiteral("LAIDEAL-0BADC0DE"));
    }

    void test_installationNumberFromXml()
    {
        const AeatRecord::SystemInfo system{ "Producer", "12345678Z", "LAIDEAL", "LI", "12.0", "LAIDEAL-1A2B3C4D" };
        AeatRecord::Cancellation cancellation;
        cancellation.invoice = { "89890001K", "30837", QDate(2026, 10, 10) };
        cancellation.generatedAt = QDateTime(QDate(2026, 10, 10), QTime(10, 0), Qt::OffsetFromUTC, 7200);
        // A real record, as stored.
        const QString real = AeatRecord::cancellationXml(cancellation, system);
        QCOMPARE(AeatStore::installationNumberFromXml(real), QStringLiteral("LAIDEAL-1A2B3C4D"));
        QCOMPARE(AeatStore::installationNumberFromXml(
                     "<RegistroAlta><SistemaInformatico><NumeroInstalacion> N-1 </NumeroInstalacion>"
                     "</SistemaInformatico></RegistroAlta>"), QStringLiteral("N-1"));
        QCOMPARE(AeatStore::installationNumberFromXml(
                     "<a xmlns:x=\"urn:x\"><x:NumeroInstalacion>A&amp;B</x:NumeroInstalacion></a>"), QStringLiteral("A&B"));
        QVERIFY(AeatStore::installationNumberFromXml("<sf:RegistroAlta>1</sf:RegistroAlta>").isEmpty());
        QVERIFY(AeatStore::installationNumberFromXml("<a><NumeroInstalacionX>1</NumeroInstalacionX></a>").isEmpty());
        QVERIFY(AeatStore::installationNumberFromXml("not xml").isEmpty());
        QVERIFY(AeatStore::installationNumberFromXml(QString()).isEmpty());
    }

    // A database that cannot be opened, read or written gives no number - never a made-up one.
    void test_installationNumberEmptyWhenUnavailable()
    {
        {
            QSqlDatabase missing = QSqlDatabase::addDatabase("QSQLITE", "aeat_store_missing");
            missing.setDatabaseName(m_dir.filePath("no_such_folder/store.db"));
            AeatStore unopened(missing, "pruebas");
            QVERIFY(unopened.installationNumber().isEmpty());
            QVERIFY(!unopened.lastError().isEmpty());
        }
        QSqlDatabase::removeDatabase("aeat_store_missing");

        AeatStore noTables(m_db, "pruebas");                             // ensureSchema() never ran
        QVERIFY(noTables.installationNumber().isEmpty());
        QVERIFY(!noTables.lastError().isEmpty());

        // The records cannot be read: not "no records", so no new number either.
        AeatStore store(m_db, "pruebas");
        QVERIFY(store.ensureSchema());
        QVERIFY(m_db.open());
        QVERIFY(QSqlQuery(m_db).exec("ALTER TABLE aeat_records RENAME TO aeat_records_gone"));
        QVERIFY(store.installationNumber().isEmpty());
        QVERIFY(!store.lastError().isEmpty());
        QCOMPARE(installationRows(), 0);
        QVERIFY(QSqlQuery(m_db).exec("ALTER TABLE aeat_records_gone RENAME TO aeat_records"));
        // A number that cannot be stored is not returned.
        QVERIFY(QSqlQuery(m_db).exec("CREATE TRIGGER no_installation BEFORE INSERT ON aeat_installation "
                                     "BEGIN SELECT RAISE(ABORT, 'read only'); END"));
        QVERIFY(store.installationNumber().isEmpty());
        QCOMPARE(installationRows(), 0);
        QVERIFY(QSqlQuery(m_db).exec("DROP TRIGGER no_installation"));
        QVERIFY(!store.installationNumber().isEmpty());
        // Inside a caller's transaction: refused, and the caller's work is not rolled back.
        QVERIFY(m_db.transaction());
        QVERIFY(QSqlQuery(m_db).exec("DELETE FROM aeat_installation"));
        QVERIFY(store.installationNumber().isEmpty());
        QVERIFY(m_db.commit());
        QCOMPARE(installationRows(), 0);
        m_db.close();
    }

    // An empty stored number (a damaged database) is replaced; the number of the chain
    // adopted from AEAT replaces whatever was stored.
    void test_installationNumberRepairedAndReplaced()
    {
        AeatStore store(m_db, "pruebas");
        QVERIFY(store.ensureSchema());
        QVERIFY(m_db.open());
        QVERIFY(QSqlQuery(m_db).exec("INSERT INTO aeat_installation (id, number, created_at) VALUES (1, '', '')"));
        const QString repaired = store.installationNumber();
        QVERIFY(repaired.startsWith("LAIDEAL-"));
        QCOMPARE(store.installationNumber(), repaired);
        QVERIFY(store.setInstallationNumber("LAIDEAL-ATAEAT00"));
        QCOMPARE(AeatStore(m_db, "produccion").installationNumber(), QStringLiteral("LAIDEAL-ATAEAT00"));
        QCOMPARE(installationRows(), 1);
        QVERIFY(!store.setInstallationNumber(" "));
        QCOMPARE(store.installationNumber(), QStringLiteral("LAIDEAL-ATAEAT00"));
        m_db.close();
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

    // sql_lite reads aeat_records with literal column names (it cannot link this library):
    // run those two reads against the table AeatStore really creates, so a column change
    // here fails this test.
    void test_sqlLiteReadsTheRealTable()
    {
        AeatStore store(m_db, "pruebas");
        QVERIFY(store.ensureSchema());
        QVERIFY(m_db.open());
        QVERIFY(QSqlQuery(m_db).exec("CREATE TABLE ingresos (n_recibo TEXT, pagado TEXT, verifactu_invoice_seq INTEGER)"));
        m_db.close();
        QCOMPARE(aeatPendingRecordCount(m_db, "pruebas"), 0);
        store.append(AeatStore::Kind::Registration, "89890001K", builder("7", "AAA"));
        const auto b = store.append(AeatStore::Kind::Registration, "89890001K", builder("7-2", "BBB"));
        store.append(AeatStore::Kind::Cancellation, "89890001K", builder("7-5", "CCC"));
        QCOMPARE(aeatPendingRecordCount(m_db, "pruebas"), 3);
        QCOMPARE(aeatPendingRecordCount(m_db, "produccion"), 0);
        QVERIFY(store.markOutcome(b.id, AeatStore::kAccepted, "CSV", "", ""));
        QCOMPARE(aeatPendingRecordCount(m_db, "pruebas"), 2);
        QCOMPARE(nextVerifactuInvoiceSeq(m_db, "7", "pruebas"), 3);        // 7 and 7-2 used; the cancellation is not an alta
        QCOMPARE(nextVerifactuInvoiceSeq(m_db, "7", "produccion"), 0);
        QCOMPARE(nextVerifactuInvoiceSeq(m_db, "8", "pruebas"), 0);
    }
};

QTEST_GUILESS_MAIN(TestAeatStore)
#include "test_aeat_store.moc"
