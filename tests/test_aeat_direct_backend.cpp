// AeatDirectBackend end to end against FakeAeatServer (plain HTTP on 127.0.0.1):
// records stored and chained, sent together, AEAT's wait time honoured, lost replies
// and rejections, cancellation, query, the outbox sent at the next start, and the
// client certificate (throwaway self-signed fixtures, never a real certificate).

#include <QtTest>
#include <QTcpServer>
#include <QSignalSpy>
#include <QSslKey>
#include <QSslSocket>
#include <QSqlDatabase>
#include <QTemporaryDir>

#include "aeatcertificate.h"
#include "aeatrecord.h"
#include "aeatselftest.h"
#include "aeatdirectbackend.h"
#include "aeatstore.h"
#include "sql_lite.h"
#include "fakeaeatserver.h"
#include "testhostguard.h"
#include "verifactuinvoice.h"

class TestAeatDirectBackend : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir   m_dir;
    QSqlDatabase    m_db;
    FakeAeatServer *m_server = nullptr;
    int             m_dbCounter = 0;
    int             m_otherCounter = 0;

    static AeatDirectBackend::Config config()
    {
        AeatDirectBackend::Config c;
        c.testEnvironment = true;
        c.issuerNif = "89890001K";
        c.issuerName = "Tintorería La Ideal";
        c.system = { "Germán Bravo López", "12345678Z", "LAIDEAL", "LI", "11.0", "LAIDEAL-TEST" };
        return c;
    }

    static VerifactuInvoice ticket(const QString &number, double base)
    {
        VerifactuInvoice inv;
        inv.setInvoiceNumber(number);
        inv.setInvoiceDate(QDate(2026, 10, 10));
        inv.setInvoiceType(VerifactuInvoice::SIMPLIFIED);
        inv.setSellerNIF("89890001K");
        inv.setSellerName("Tintorería La Ideal");
        inv.setDescription("Servicios de lavandería");
        VerifactuTaxItem item;
        item.setTaxRate(21.0);
        item.setTaxBase(base);
        item.setTaxAmount(base * 0.21);
        inv.addTaxItem(item);
        inv.calculateTotals();
        return inv;
    }

    // Waits for the answer to `requestId` and returns it.
    static VerifactuResult waitFor(AeatDirectBackend &backend, const QString &requestId, int timeoutMs = 10000)
    {
        VerifactuResult out;
        bool done = false;
        QMetaObject::Connection c = QObject::connect(&backend, &VerifactuBackend::requestFinished,
            [&](const QString &id, const VerifactuResult &r) { if (id == requestId) { out = r; done = true; } });
        if (!QTest::qWaitFor([&done]() { return done; }, timeoutMs)) {
            out.status = VerifactuResult::PENDING;
            out.errorDescription = QStringLiteral("no answer from the backend within the test timeout");
        }
        QObject::disconnect(c);
        return out;
    }

    // Syncs the backend's chain (the first sync queries AEAT), so that a reply scripted
    // afterwards goes to the submission.
    static bool sync(AeatDirectBackend &backend)
    {
        bool done = false, ok = false;
        backend.syncChain([&](bool o, const QString &) { done = true; ok = o; });
        return QTest::qWaitFor([&done]() { return done; }, 30000) && ok;
    }

    // Registers `numbers` at the fake AEAT from another database, as `system` (by
    // default ours: a new or restored database of the same system).
    QString registerElsewhere(const QStringList &numbers, const AeatRecord::SystemInfo &system = config().system)
    {
        QString lastHash;
        {
            QSqlDatabase other = QSqlDatabase::addDatabase("QSQLITE", "aeat_other_db");
            other.setDatabaseName(m_dir.filePath(QString("other_%1_%2.db").arg(m_dbCounter).arg(++m_otherCounter)));
            AeatDirectBackend::Config c = config();
            c.system = system;
            AeatDirectBackend elsewhere(c, other);
            for (const QString &n : numbers)
                lastHash = waitFor(elsewhere, elsewhere.submitInvoiceAsync(ticket(n, 10))).rawHash;
            other.close();
        }
        QSqlDatabase::removeDatabase("aeat_other_db");
        return lastHash;
    }

    int queriesSince(int index) const
    {
        int n = 0;
        for (int i = index; i < m_server->requests().size(); ++i)
            n += m_server->requests()[i].kind == QLatin1String("Consulta");
        return n;
    }

private slots:
    void initTestCase()
    {
        qInfo() << "TLS backend:" << QSslSocket::activeBackend();
    }

    void init()
    {
        m_server = new FakeAeatServer(this);
        QVERIFY(m_server->start());
        AeatDirectBackend::setEndpointOverride(m_server->url());
        AeatDirectBackend::setRetryDelaySeconds(3600);     // no automatic retry unless a test asks
        m_db = QSqlDatabase::addDatabase("QSQLITE", "aeat_backend_test");
        m_db.setDatabaseName(m_dir.filePath(QString("backend_%1.db").arg(++m_dbCounter)));
    }

    void cleanup()
    {
        delete m_server;
        m_server = nullptr;
        AeatDirectBackend::setEndpointOverride(QString());
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase("aeat_backend_test");
    }

    // A ticket is registered: CSV, the stored record and hash, the QR drawn locally.
    void test_submitAccepted()
    {
        AeatDirectBackend backend(config(), m_db);
        QVERIFY2(backend.isConfigured(), qPrintable(backend.configurationError()));
        const VerifactuResult r = waitFor(backend, backend.submitInvoiceAsync(ticket("30837", 24.79)));
        QVERIFY2(r.isSuccess(), qPrintable(r.errorDescription));
        QVERIFY(r.csv.startsWith("A-FAKE"));
        QCOMPARE(r.rawHash.size(), 64);
        QVERIFY(r.rawXml.contains("<sf:NumSerieFactura>30837</sf:NumSerieFactura>"));
        QVERIFY(!r.qrCode.isNull());
        QCOMPARE(r.validationUrl, QStringLiteral("https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR?nif=89890001K"
                                                 "&numserie=30837&fecha=10-10-2026&importe=30.00"));
        const QList<FakeAeatServer::SentRecord> sent = m_server->sentRecords();
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent[0].total, QStringLiteral("30.00"));
        QCOMPARE(sent[0].hash, r.rawHash);
        QVERIFY(sent[0].previousHash.isEmpty());                          // first record of the chain
        AeatStore store(m_db, "pruebas");
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "30837").state, AeatStore::kAccepted);
    }

    // AEAT's wait time is honoured; records created meanwhile go together, each chained to the previous.
    void test_waitTimeAndBatching()
    {
        m_server->setWaitSeconds(2);
        AeatDirectBackend backend(config(), m_db);
        const VerifactuResult first = waitFor(backend, backend.submitInvoiceAsync(ticket("1", 10)));
        QVERIFY(first.isSuccess());
        const QDateTime firstReply = QDateTime::currentDateTime();
        QVERIFY(backend.secondsUntilNextSend() >= 1);
        // B and C are answered by the same reply: record every answer from the start.
        QHash<QString, VerifactuResult> answers;
        connect(&backend, &VerifactuBackend::requestFinished,
                [&answers](const QString &id, const VerifactuResult &r) { answers.insert(id, r); });
        const QString idB = backend.submitInvoiceAsync(ticket("2", 20));
        const QString idC = backend.submitInvoiceAsync(ticket("3", 30));
        QTest::qWait(500);
        QCOMPARE(m_server->submissions().size(), 1);                      // still waiting
        QTRY_VERIFY_WITH_TIMEOUT(answers.contains(idB) && answers.contains(idC), 10000);
        QVERIFY(answers.value(idB).isSuccess());
        QVERIFY(answers.value(idC).isSuccess());
        QVERIFY(firstReply.msecsTo(m_server->submissions().last().receivedAt) >= 1500);
        QCOMPARE(m_server->submissions().size(), 2);
        QCOMPARE(m_server->submissions().last().records.size(), 2);       // sent together
        const QList<FakeAeatServer::SentRecord> sent = m_server->sentRecords();
        QCOMPARE(sent[1].previousHash, sent[0].hash);
        QCOMPARE(sent[2].previousHash, sent[1].hash);
    }

    // The answer is lost after AEAT registered the record: pending, and the retry
    // sends the same record, which AEAT reports as a duplicate of an accepted one.
    void test_lostReplyResendsSameRecord()
    {
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(sync(backend));
        m_server->enqueue({ QByteArray(), 200, 0, false, true });
        const VerifactuResult lost = waitFor(backend, backend.submitInvoiceAsync(ticket("30837", 24.79)));
        QCOMPARE(lost.status, VerifactuResult::NETWORK_ERROR);
        AeatStore store(m_db, "pruebas");
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "30837").state, AeatStore::kPending);

        AeatDirectBackend later(config(), m_db);        // e.g. the next day, no wait pending
        const VerifactuResult retried = waitFor(later, later.submitInvoiceAsync(ticket("30837", 24.79)), 15000);
        QVERIFY2(retried.isSuccess(), qPrintable(retried.errorDescription));
        QVERIFY(retried.csv.startsWith("IdPeticion "));
        const QList<FakeAeatServer::SentRecord> sent = m_server->sentRecords();
        QVERIFY(sent.size() >= 2);
        QCOMPARE(sent.last().hash, sent.first().hash);                    // the same record, unchanged
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "30837").state, AeatStore::kAccepted);
    }

    // A rejection is final for that record; a new attempt is a new record flagged
    // Subsanacion / RechazoPrevio, chained after the rejected one.
    void test_rejectionThenNewRecord()
    {
        m_server->rejectNext("30838", "1100", "Valor o tipo incorrecto del campo: ImporteTotal");
        AeatDirectBackend backend(config(), m_db);
        const VerifactuResult rejected = waitFor(backend, backend.submitInvoiceAsync(ticket("30838", 8.26)));
        QCOMPARE(rejected.status, VerifactuResult::ERROR);
        QCOMPARE(rejected.errorCode, QStringLiteral("1100"));
        QVERIFY(rejected.qrCode.isNull());

        AeatDirectBackend later(config(), m_db);
        const VerifactuResult again = waitFor(later, later.submitInvoiceAsync(ticket("30838", 8.26)));
        QVERIFY(again.isSuccess());
        const QList<FakeAeatServer::SentRecord> sent = m_server->sentRecords();
        QCOMPARE(sent.size(), 2);
        QVERIFY(!sent[0].afterRejection && sent[1].afterRejection);
        QVERIFY(sent[1].hash != sent[0].hash);
        QCOMPARE(sent[1].previousHash, sent[0].hash);
    }

    // An invoice AEAT already accepted is answered from the store, without a new submission.
    void test_alreadyAcceptedAnsweredLocally()
    {
        AeatDirectBackend backend(config(), m_db);
        const VerifactuResult first = waitFor(backend, backend.submitInvoiceAsync(ticket("30837", 24.79)));
        QVERIFY(first.isSuccess());
        const VerifactuResult again = waitFor(backend, backend.submitInvoiceAsync(ticket("30837", 24.79)));
        QVERIFY(again.isSuccess());
        QCOMPARE(again.csv, first.csv);
        QCOMPARE(again.rawHash, first.rawHash);
        QVERIFY(!again.qrCode.isNull());
        QCOMPARE(m_server->submissions().size(), 1);
    }

    // A cancellation is its own record, chained after the registration.
    void test_cancellation()
    {
        AeatDirectBackend backend(config(), m_db);
        const VerifactuResult alta = waitFor(backend, backend.submitInvoiceAsync(ticket("30837", 24.79)));
        QVERIFY(alta.isSuccess());
        const VerifactuResult anulacion = waitFor(backend, backend.cancelInvoiceAsync("30837", QDate(2026, 10, 10)));
        QVERIFY2(anulacion.isSuccess(), qPrintable(anulacion.errorDescription));
        QVERIFY(anulacion.qrCode.isNull());                               // no QR for a cancellation
        const QList<FakeAeatServer::SentRecord> sent = m_server->sentRecords();
        QCOMPARE(sent.last().operation, QStringLiteral("Anulacion"));
        QCOMPARE(sent.last().previousHash, alta.rawHash);
        QVERIFY(m_server->registered().contains("Anulacion:30837"));
    }

    // A cancellation whose reply was lost is resent; AEAT answers that it holds it (a
    // duplicate, the invoice Anulada): accepted, never an error that would keep the income
    // counted and make Anular loop.
    void test_cancellationLostReplyResentIsAccepted()
    {
        AeatDirectBackend backend(config(), m_db);
        const VerifactuResult alta = waitFor(backend, backend.submitInvoiceAsync(ticket("30839", 12.10)));
        QVERIFY(alta.isSuccess());
        m_server->enqueue({ QByteArray(), 200, 0, false, true });          // registered, answer lost
        const VerifactuResult lost = waitFor(backend, backend.cancelInvoiceAsync("30839", QDate(2026, 10, 10)));
        QCOMPARE(lost.status, VerifactuResult::NETWORK_ERROR);

        AeatDirectBackend later(config(), m_db);
        const VerifactuResult resent = waitFor(later, later.cancelInvoiceAsync("30839", QDate(2026, 10, 10)), 15000);
        QVERIFY2(resent.isSuccess(), qPrintable(resent.errorDescription));
        AeatStore store(m_db, "pruebas");
        QCOMPARE(store.latest(AeatStore::Kind::Cancellation, "30839").state, AeatStore::kAccepted);
    }

    // A client SOAP fault on a batch (one malformed record refuses the whole message) is
    // not every record's refusal: they stay pending, are resent one by one, and only the
    // faulty one is refused. The waiting requests hear PENDING; the outcomes follow.
    void test_clientFaultOnABatchIsolatesTheBadRecord()
    {
        AeatDirectBackend::setRetryDelaySeconds(1);
        m_server->setWaitSeconds(2);
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(waitFor(backend, backend.submitInvoiceAsync(ticket("1", 10))).isSuccess());
        m_server->faultWhenSending("3");
        QHash<QString, VerifactuResult> answers;
        connect(&backend, &VerifactuBackend::requestFinished,
                [&answers](const QString &id, const VerifactuResult &r) { answers.insert(id, r); });
        const QStringList ids = { backend.submitInvoiceAsync(ticket("2", 20)), backend.submitInvoiceAsync(ticket("3", 30)),
                                  backend.submitInvoiceAsync(ticket("4", 40)) };
        QTRY_VERIFY_WITH_TIMEOUT(answers.size() == 3, 15000);
        for (const QString &id : ids)
            QCOMPARE(answers.value(id).status, VerifactuResult::PENDING);
        QCOMPARE(m_server->submissions().at(1).records.size(), 3);        // the batch, refused whole

        AeatStore store(m_db, "pruebas");
        QTRY_VERIFY_WITH_TIMEOUT(store.pending(10).isEmpty(), 30000);
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "2").state, AeatStore::kAccepted);
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "3").state, AeatStore::kRejected);
        QVERIFY(store.latest(AeatStore::Kind::Registration, "3").errorDescription.contains("4102"));
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "4").state, AeatStore::kAccepted);
        const QList<FakeAeatServer::Request> sends = m_server->submissions();
        for (int i = 2; i < sends.size(); ++i)
            QCOMPARE(sends[i].records.size(), 1);                         // one by one after the fault
        QVERIFY(m_server->registered().contains("Alta:2") && m_server->registered().contains("Alta:4"));
    }

    // AEAT answers "duplicate" but holds the number with another amount (e.g. a reused
    // number after a restore in pre-production): not adopted, an ERROR saying why.
    void test_duplicateWithOtherAmountIsNotAdopted()
    {
        registerElsewhere({ "520" });                                     // 10.00 + IVA there
        AeatDirectBackend backend(config(), m_db);
        const VerifactuResult r = waitFor(backend, backend.submitInvoiceAsync(ticket("520", 20)), 15000);
        QCOMPARE(r.status, VerifactuResult::ERROR);
        QVERIFY2(r.errorDescription.contains("otro importe"), qPrintable(r.errorDescription));
        QVERIFY(r.csv.isEmpty() && r.rawXml.isEmpty() && r.qrCode.isNull());
        AeatStore store(m_db, "pruebas");
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "520").state, AeatStore::kRejected);
        QCOMPARE(m_server->requests().last().kind, QStringLiteral("Consulta"));   // checked before deciding
    }

    // Before a backend is replaced (Configuración), every request still waiting hears an
    // answer: a stored record PENDING (the next backend sends it), a request held for the
    // first chain sync NETWORK_ERROR (no record was generated).
    void test_waitersAnsweredWhenTheBackendIsReleased()
    {
        QList<QPair<QString, VerifactuResult>> heard;
        const auto listen = [&heard](AeatDirectBackend &b) {
            connect(&b, &VerifactuBackend::requestFinished,
                    [&heard](const QString &id, const VerifactuResult &r) { heard.append({ id, r }); });
        };
        {
            m_server->enqueue({ QByteArray(), 200, 3000 });               // the first sync's query, slow
            AeatDirectBackend fresh(config(), m_db);
            listen(fresh);
            const QString held = fresh.submitInvoiceAsync(ticket("700", 10));
            fresh.releaseWaiters();
            QCOMPARE(heard.size(), 1);
            QCOMPARE(heard[0].first, held);
            QCOMPARE(heard[0].second.status, VerifactuResult::NETWORK_ERROR);
            QTest::qWait(3500);                                           // the sync ends: no second answer
            QCOMPARE(heard.size(), 1);
        }
        heard.clear();
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(sync(backend));
        m_server->enqueue({ QByteArray(), 200, 3000 });                   // the submission, slow
        listen(backend);
        const QString stored = backend.submitInvoiceAsync(ticket("701", 10));
        QTRY_VERIFY_WITH_TIMEOUT(!m_server->submissions().isEmpty(), 10000);
        backend.releaseWaiters();
        QCOMPARE(heard.size(), 1);
        QCOMPARE(heard[0].first, stored);
        QCOMPARE(heard[0].second.status, VerifactuResult::PENDING);
    }

    // A record settled while its submission was in flight (no longer pending at the reply)
    // still answers its waiters, from the store.
    void test_waiterAnsweredWhenItsRecordSettledMeanwhile()
    {
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(sync(backend));
        m_server->enqueue({ QByteArray(), 200, 1500 });
        const QString id = backend.submitInvoiceAsync(ticket("702", 10));
        AeatStore store(m_db, "pruebas");
        QTRY_VERIFY_WITH_TIMEOUT(!m_server->submissions().isEmpty(), 10000);
        const AeatStore::Record record = store.latest(AeatStore::Kind::Registration, "702");
        QVERIFY(store.markOutcome(record.id, AeatStore::kAccepted, "A-SETTLED", QString(), QString()));
        const VerifactuResult r = waitFor(backend, id);
        QVERIFY2(r.isSuccess(), qPrintable(r.errorDescription));
        QCOMPARE(r.csv, QStringLiteral("A-SETTLED"));
    }

    // Tripwire: with LAIDEAL_TEST=1 (CTest sets it for every suite) the transport refuses
    // any host but this machine before connecting. The refused host never resolves anyway.
    void test_tripwireRefusesNonLocalHosts()
    {
        const QByteArray previous = qgetenv("LAIDEAL_TEST");
        qputenv("LAIDEAL_TEST", "1");
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression("TestHostGuard.*laideal-tripwire.invalid"));
        const AeatTransport::Response r = AeatTransport::postBlocking("http://laideal-tripwire.invalid/soap", "<x/>", nullptr, 1000);
        QVERIFY2(r.error.contains("refused"), qPrintable(r.error));
        QVERIFY(!r.requestSent);
        QVERIFY(TestHostGuard::allows(QUrl("http://127.0.0.1:1/x")));
        QVERIFY(TestHostGuard::allows(QUrl("http://localhost:1/x")));
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression("TestHostGuard.*agenciatributaria"));
        QVERIFY(!TestHostGuard::allows(QUrl("https://www1.agenciatributaria.gob.es/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP")));
        qputenv("LAIDEAL_TEST", "0");
        QVERIFY(TestHostGuard::allows(QUrl("https://prewww1.aeat.es/x")));    // only the flag arms it
        qputenv("LAIDEAL_TEST", previous);
    }

    // A fault from AEAT's side leaves the record pending; one about the request rejects it.
    void test_faults()
    {
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(sync(backend));
        m_server->enqueue({ FakeAeatServer::faultReply("env:Server", "Servicio no disponible"), 500 });
        const VerifactuResult server = waitFor(backend, backend.submitInvoiceAsync(ticket("1", 10)));
        QCOMPARE(server.status, VerifactuResult::NETWORK_ERROR);
        AeatStore store(m_db, "pruebas");
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "1").state, AeatStore::kPending);

        // The pending record goes out at the next start, before any request (and before
        // the chain is synced with AEAT): a client fault rejects it.
        m_server->enqueue({ FakeAeatServer::faultReply("env:Client", "Codigo[4102].El XML no cumple el esquema"), 500 });
        AeatDirectBackend later(config(), m_db);
        QTRY_COMPARE_WITH_TIMEOUT(store.latest(AeatStore::Kind::Registration, "1").state, AeatStore::kRejected, 10000);
        QVERIFY(store.latest(AeatStore::Kind::Registration, "1").errorDescription.contains("4102"));
        QCOMPARE(m_server->requests().last().kind, QStringLiteral("RegFactu"));
    }

    // Records left unsent when the app closed are sent at the next start, unasked.
    void test_outboxSentAtNextStart()
    {
        {
            AeatDirectBackend backend(config(), m_db);
            QVERIFY(sync(backend));
            m_server->enqueue({ QByteArray(), 200, 0, true });
            QCOMPARE(waitFor(backend, backend.submitInvoiceAsync(ticket("30837", 24.79))).status,
                     VerifactuResult::NETWORK_ERROR);
        }
        AeatDirectBackend next(config(), m_db);
        AeatStore store(m_db, "pruebas");
        QTRY_COMPARE_WITH_TIMEOUT(store.latest(AeatStore::Kind::Registration, "30837").state, AeatStore::kAccepted, 10000);
        QCOMPARE(m_server->submissions().size(), 2);
    }

    // The query reads what AEAT holds; a cancelled registration is found but not usable.
    void test_query()
    {
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(waitFor(backend, backend.submitInvoiceAsync(ticket("30837", 24.79))).isSuccess());
        QSignalSpy spy(&backend, &VerifactuBackend::queryFinished);
        const QString id = backend.queryInvoiceAsync("30837");
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 10000);
        QCOMPARE(spy[0][0].toString(), id);
        const VerifactuRemoteRecord found = spy[0][1].value<VerifactuRemoteRecord>();
        QVERIFY(found.hasUsableCsv());
        QCOMPARE(found.totalAmount, 30.0);

        QVERIFY(waitFor(backend, backend.cancelInvoiceAsync("30837", QDate(2026, 10, 10))).isSuccess());
        const QString idCancelled = backend.queryInvoiceAsync("30837");
        const QString idMissing = backend.queryInvoiceAsync("99999");
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 3, 10000);
        // The two answers may arrive in either order.
        const auto answerTo = [&spy](const QString &requestId) {
            for (const QList<QVariant> &call : spy)
                if (call[0].toString() == requestId)
                    return call[1].value<VerifactuRemoteRecord>();
            return VerifactuRemoteRecord();
        };
        const VerifactuRemoteRecord cancelled = answerTo(idCancelled);
        const VerifactuRemoteRecord missing = answerTo(idMissing);
        QVERIFY(cancelled.found && !cancelled.hasUsableCsv());
        QVERIFY(missing.parsed && !missing.found);
    }

    // One chain per system and issuer (AEAT, 13-10-2026): what another system (the
    // gateway) registered for the issuer is never continued - our first record starts
    // a chain of its own with PrimerRegistro.
    void test_chainIgnoresOtherSystems()
    {
        const AeatRecord::SystemInfo gateway{ "IreneSolutions", "B11111111", "VERIFACTU-API", "IR", "1.0", "GW-1" };
        registerElsewhere({ "1", "2" }, gateway);
        AeatDirectBackend backend(config(), m_db);
        bool ok = false;
        QString message;
        backend.syncChain([&](bool o, const QString &m) { ok = o; message = m; });
        QTRY_VERIFY_WITH_TIMEOUT(!message.isEmpty(), 30000);
        QVERIFY2(ok && message.contains("primer registro"), qPrintable(message));
        QVERIFY(m_server->requests().last().body.contains("<sfLRC:MostrarSistemaInformatico>S</sfLRC:MostrarSistemaInformatico>"));
        QVERIFY(waitFor(backend, backend.submitInvoiceAsync(ticket("3", 30))).isSuccess());
        QVERIFY(m_server->sentRecords().last().previousHash.isEmpty());
        QVERIFY(m_server->submissions().last().body.contains("PrimerRegistro>S<"));
        // Our records name La Ideal's producer, not the issuer.
        QCOMPARE(m_server->sentRecords().last().producerNif, config().system.producerNif);
    }

    // The producer of every record is La Ideal's, whoever the issuer is.
    void test_laIdealSystem()
    {
        const AeatRecord::SystemInfo s = AeatRecord::laIdealSystem("12.0", "LAIDEAL-1");
        QCOMPARE(s.producerNif, QStringLiteral("75929840D"));
        QCOMPARE(s.systemId, QStringLiteral("LI"));
        QCOMPARE(s.systemName, QStringLiteral("La Ideal"));                    // as in the declaración's a)
        QCOMPARE(s.version, QStringLiteral("12.0"));
        QCOMPARE(s.installationNumber, QStringLiteral("LAIDEAL-1"));
    }

    // Our own cancellation can be the head: the next record chains to it, also after a
    // restart (AEAT's query returns registrations only; the local store has it).
    void test_ownCancellationIsTheHead()
    {
        m_server->setWaitSeconds(0);
        QString cancellationHash;
        {
            AeatDirectBackend first(config(), m_db);
            QVERIFY(waitFor(first, first.submitInvoiceAsync(ticket("1", 10))).isSuccess());
            const VerifactuResult cancelled = waitFor(first, first.cancelInvoiceAsync("1", QDate(2026, 10, 10)));
            QVERIFY2(cancelled.isSuccess(), qPrintable(cancelled.errorDescription));
            cancellationHash = m_server->sentRecords().last().hash;
        }
        AeatDirectBackend restarted(config(), m_db);
        QVERIFY(waitFor(restarted, restarted.submitInvoiceAsync(ticket("2", 20))).isSuccess());
        QCOMPARE(m_server->sentRecords().last().previousHash, cancellationHash);
    }

    // AEAT holds nothing for the issuer: the chain starts with PrimerRegistro. An empty
    // month is skipped (looking back to the previous one).
    void test_continueChainLooksBack()
    {
        AeatDirectBackend backend(config(), m_db);
        QString message;
        bool ok = false;
        backend.syncChain([&](bool o, const QString &m) { ok = o; message = m; });
        QTRY_VERIFY_WITH_TIMEOUT(!message.isEmpty(), 30000);
        QVERIFY2(ok && message.contains("primer registro"), qPrintable(message));
        QCOMPARE(queriesSince(0), 24);                                   // two years back
        QVERIFY(waitFor(backend, backend.submitInvoiceAsync(ticket("1", 10))).isSuccess());
        QVERIFY(m_server->sentRecords().last().previousHash.isEmpty());
    }

    // The proof-of-concept run: test invoice registered, queried back and cancelled,
    // after AEAT's wait time; every step reported; a refusal stops it.
    void test_selfTest()
    {
        m_server->setWaitSeconds(1);
        AeatDirectBackend::Config c = config();
        c.testEnvironment = false;                       // ignored: the self-test never uses production
        AeatSelfTest selfTest(c, m_db);
        QStringList steps;
        bool finished = false, ok = false;
        connect(&selfTest, &AeatSelfTest::progress, [&](bool, const QString &t) { steps << t; });
        connect(&selfTest, &AeatSelfTest::finished, [&](bool o) { finished = true; ok = o; });
        selfTest.run();
        QTRY_VERIFY_WITH_TIMEOUT(finished, 20000);
        QVERIFY2(ok, qPrintable(steps.join(" | ")));
        QVERIFY(selfTest.invoiceNumber().startsWith("PRUEBA-"));
        QVERIFY(m_server->registered().contains("Alta:" + selfTest.invoiceNumber()));
        QVERIFY(m_server->registered().contains("Anulacion:" + selfTest.invoiceNumber()));
        QVERIFY2(steps.join(" ").contains("Esperando"), qPrintable(steps.join(" | ")));
        QVERIFY(steps.last().contains("La conexión directa funciona"));
        AeatStore production(m_db, "produccion");
        QVERIFY(production.pending().isEmpty() && production.chainHead("89890001K").isFirst());

        AeatSelfTest refused(config(), m_db);
        m_server->rejectNext(refused.invoiceNumber(), "1100", "Valor o tipo incorrecto");
        steps.clear();
        finished = false;
        connect(&refused, &AeatSelfTest::progress, [&](bool, const QString &t) { steps << t; });
        connect(&refused, &AeatSelfTest::finished, [&](bool o) { finished = true; ok = o; });
        refused.run();
        QTRY_VERIFY_WITH_TIMEOUT(finished, 20000);
        QVERIFY(!ok);
        QVERIFY2(steps.last().contains("Alta de") && steps.last().contains("1100"), qPrintable(steps.last()));
    }

    // A self-test in the shop's pre-production chain keeps the shop's chain marker: the
    // next start finds it matching and needs no AEAT (the main window wires the marker in
    // only while the shop's direct client is in pre-production).
    void test_selfTestKeepsTheChainMarker()
    {
        m_server->setWaitSeconds(1);
        QString marker;
        AeatDirectBackend::Config c = config();
        c.readChainMarker = [&marker] { return marker; };
        c.writeChainMarker = [&marker](const QString &m) { marker = m; };
        {
            AeatDirectBackend shop(c, m_db);
            QVERIFY(waitFor(shop, shop.submitInvoiceAsync(ticket("1", 10))).isSuccess());
        }
        AeatSelfTest selfTest(c, m_db);
        bool finished = false;
        connect(&selfTest, &AeatSelfTest::finished, [&](bool) { finished = true; });
        selfTest.run();
        QTRY_VERIFY_WITH_TIMEOUT(finished, 30000);
        QCOMPARE(marker, QStringLiteral("pruebas|89890001K|") + m_server->sentRecords().last().hash);
        m_server->enqueue({ QByteArray(), 200, 0, true });
        AeatDirectBackend next(c, m_db);
        QVERIFY(sync(next));                                                  // matching: no AEAT needed
    }

    // The self-test's backend sends every pending record of the issuer: their outcomes
    // are reported so the app settles them (pre-production records of the shop).
    void test_selfTestReportsOtherRecordsItSettles()
    {
        m_server->setWaitSeconds(1);
        {
            AeatDirectBackend shop(config(), m_db);
            QVERIFY(sync(shop));
            m_server->enqueue({ QByteArray(), 200, 0, true });
            QCOMPARE(waitFor(shop, shop.submitInvoiceAsync(ticket("S1", 10))).status, VerifactuResult::NETWORK_ERROR);
        }
        AeatSelfTest selfTest(config(), m_db);
        QSignalSpy settled(&selfTest, &AeatSelfTest::recordSettled);
        bool finished = false;
        connect(&selfTest, &AeatSelfTest::finished, [&](bool) { finished = true; });
        selfTest.run();
        QTRY_VERIFY_WITH_TIMEOUT(finished, 30000);
        bool sawShop = false;
        for (const QList<QVariant> &s : settled)
            sawShop |= s[0].toString() == QLatin1String("S1");
        QVERIFY(sawShop);
    }

    // A rectificativa R5 by substitution carries the rectified invoice and its former amounts.
    void test_rectificationMapping()
    {
        VerifactuInvoice inv = ticket("30900", 16.53);
        inv.setInvoiceType(VerifactuInvoice::RECTIFICATION_R5);
        inv.setRectificationType(VerifactuInvoice::BY_SUBSTITUTION);
        inv.setRectificationTaxBase(24.79);
        inv.setRectificationTaxAmount(5.21);
        inv.addRectifiedInvoice("30837", QDate(2026, 10, 9));
        const AeatRecord::Registration r = AeatDirectBackend::registrationFrom(inv, {}, QDateTime::currentDateTime());
        QCOMPARE(r.invoiceType, QStringLiteral("R5"));
        QCOMPARE(r.rectificationType, QStringLiteral("S"));
        QCOMPARE(r.rectifiedInvoices.size(), 1);
        QCOMPARE(r.rectifiedInvoices[0].invoiceNumber, QStringLiteral("30837"));
        QVERIFY(r.hasRectifiedAmounts);
        QCOMPARE(r.rectifiedBase, 24.79);
        QCOMPARE(r.taxLines.size(), 1);
        QCOMPARE(r.taxLines[0].rate, 21.0);
    }

    // An unusable certificate stops the sending, not the records: before the first
    // sync nothing can be generated (the chain needs AEAT), afterwards records are
    // generated and kept pending.
    void test_certificateUnusable()
    {
        AeatDirectBackend::setEndpointOverride("https://127.0.0.1:9/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP");
        AeatDirectBackend::Config c = config();
        c.certificatePath = m_dir.filePath("missing.p12");
        {
            AeatDirectBackend fresh(c, m_db);
            QVERIFY(fresh.isConfigured());
            QVERIFY(fresh.sendingError().contains("certificado"));
            const VerifactuResult held = waitFor(fresh, fresh.submitInvoiceAsync(ticket("1", 10)));
            QCOMPARE(held.status, VerifactuResult::NETWORK_ERROR);
            QVERIFY2(held.errorDescription.contains("cadena"), qPrintable(held.errorDescription));
        }
        AeatStore store(m_db, "pruebas");
        QVERIFY(!store.latest(AeatStore::Kind::Registration, "1").isValid());     // nothing generated
        QVERIFY(store.markChainSynced("89890001K"));
        AeatDirectBackend synced(c, m_db);
        const VerifactuResult kept = waitFor(synced, synced.submitInvoiceAsync(ticket("1", 10)));
        QCOMPARE(kept.status, VerifactuResult::NETWORK_ERROR);
        QVERIFY2(kept.errorDescription.contains("pendiente de envío"), qPrintable(kept.errorDescription));
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "1").state, AeatStore::kPending);
    }

    // A new database of the same system (a new PC): requests made before the first sync
    // wait for it, and the first record follows our system's newest record at AEAT.
    void test_heldUntilFirstSync()
    {
        const QString tipHash = registerElsewhere({ "1" });
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(!backend.chainReady());
        const VerifactuResult r = waitFor(backend, backend.submitInvoiceAsync(ticket("2", 20)));
        QVERIFY2(r.isSuccess(), qPrintable(r.errorDescription));
        QVERIFY(backend.chainReady());
        QCOMPARE(m_server->sentRecords().last().previousHash, tipHash);
        QCOMPARE(m_server->requests().first().kind, QStringLiteral("Consulta"));   // asked before generating
    }

    // A database restored from an older backup (AEAT: the same SIF, the same chain):
    // records sent after the backup are only at AEAT, and the next start continues
    // after them, asking for this month and the previous one only.
    void test_restoredBackupContinuesFromAeat()
    {
        {
            AeatDirectBackend first(config(), m_db);
            QVERIFY(waitFor(first, first.submitInvoiceAsync(ticket("1", 10))).isSuccess());
        }
        const QString afterBackup = registerElsewhere({ "2" });
        const int before = m_server->requests().size();
        AeatDirectBackend restored(config(), m_db);
        QVERIFY(waitFor(restored, restored.submitInvoiceAsync(ticket("3", 30))).isSuccess());
        QCOMPARE(m_server->sentRecords().last().previousHash, afterBackup);
        QCOMPARE(queriesSince(before), 2);
    }

    // A first sync (here a database that never synced, as after a restore or on a new PC)
    // reads every month: the chain's newest record may be dated months back (a resend),
    // and the next record must follow it. A later, daily sync reads two months only.
    void test_firstSyncReadsEveryMonth()
    {
        m_server->setQueryByMonth(true);
        const QDate today = QDate::currentDate();
        QString newest;
        {
            QSqlDatabase other = QSqlDatabase::addDatabase("QSQLITE", "aeat_other_db");
            other.setDatabaseName(m_dir.filePath(QString("other_%1_months.db").arg(m_dbCounter)));
            AeatDirectBackend elsewhere(config(), other);
            VerifactuInvoice recent = ticket("1", 10);
            recent.setInvoiceDate(today);
            QVERIFY(waitFor(elsewhere, elsewhere.submitInvoiceAsync(recent)).isSuccess());
            VerifactuInvoice resent = ticket("2", 20);                          // generated after, dated back
            resent.setInvoiceDate(today.addMonths(-3));
            newest = waitFor(elsewhere, elsewhere.submitInvoiceAsync(resent)).rawHash;
            QVERIFY(!newest.isEmpty());
            other.close();
        }
        QSqlDatabase::removeDatabase("aeat_other_db");
        int before = m_server->requests().size();
        {
            AeatDirectBackend restored(config(), m_db);
            VerifactuInvoice next = ticket("3", 30);
            next.setInvoiceDate(today);
            QVERIFY(waitFor(restored, restored.submitInvoiceAsync(next)).isSuccess());
            QCOMPARE(m_server->sentRecords().last().previousHash, newest);
            QCOMPARE(queriesSince(before), 24);
        }
        before = m_server->requests().size();
        AeatDirectBackend daily(config(), m_db);
        QVERIFY(sync(daily));
        QCOMPARE(queriesSince(before), 2);
    }

    // Registers, from another database of our system, "1" dated today and "2" generated
    // after it but dated three months back (a resend): the chain's newest record.
    void registerWithAnOlderDatedTip()
    {
        {
            QSqlDatabase other = QSqlDatabase::addDatabase("QSQLITE", "aeat_other_db");
            other.setDatabaseName(m_dir.filePath(QString("other_%1_tip.db").arg(m_dbCounter)));
            AeatDirectBackend elsewhere(config(), other);
            VerifactuInvoice recent = ticket("1", 10);
            recent.setInvoiceDate(QDate::currentDate());
            QVERIFY(waitFor(elsewhere, elsewhere.submitInvoiceAsync(recent)).isSuccess());
            VerifactuInvoice resent = ticket("2", 20);
            resent.setInvoiceDate(QDate::currentDate().addMonths(-3));
            QVERIFY(waitFor(elsewhere, elsewhere.submitInvoiceAsync(resent)).isSuccess());
            other.close();
        }
        QSqlDatabase::removeDatabase("aeat_other_db");
    }

    // A first sync cannot trust a record of ours AEAT returns without its huella: it may
    // be the chain's newest. The sync fails (numbering stays blocked) instead of guessing.
    void test_firstSyncFailsOnARecordWithoutItsHuella()
    {
        m_server->setQueryByMonth(true);
        registerWithAnOlderDatedTip();
        m_server->queryWithoutHashFor("2");
        AeatDirectBackend restored(config(), m_db);
        bool ok = true;
        QString message;
        restored.syncChain([&](bool o, const QString &m) { ok = o; message = m; });
        QTRY_VERIFY_WITH_TIMEOUT(!message.isEmpty(), 30000);
        QVERIFY2(!ok && message.contains("sin su huella"), qPrintable(message));
        QVERIFY(restored.chainSyncFailed());
    }

    // AEAT omitting the SistemaInformatico block (ignored MostrarSistemaInformatico, a schema
    // change) leaves our own records unrecognisable: the first sync fails instead of taking
    // them for another system's and restarting the chain with PrimerRegistro.
    void test_firstSyncFailsWhenAeatOmitsTheSystemBlock()
    {
        m_server->setQueryByMonth(true);
        registerWithAnOlderDatedTip();
        m_server->setQueryWithoutSystemBlock(true);
        AeatDirectBackend restored(config(), m_db);
        bool ok = true;
        QString message;
        restored.syncChain([&](bool o, const QString &m) { ok = o; message = m; });
        QTRY_VERIFY_WITH_TIMEOUT(!message.isEmpty(), 30000);
        QVERIFY2(!ok && message.contains("sin el sistema"), qPrintable(message));
        QVERIFY(restored.chainSyncFailed());
        m_server->setQueryWithoutSystemBlock(false);
    }

    // A certificate close to its expiry, or past it, is announced at start.
    void test_certificateExpiryWarning()
    {
        const QDateTime now(QDate(2026, 10, 19), QTime(9, 0));
        QVERIFY(AeatDirectBackend::expiryWarning(now.addDays(31), now).isEmpty());
        QVERIFY(AeatDirectBackend::expiryWarning(QDateTime(), now).isEmpty());
        QVERIFY(AeatDirectBackend::expiryWarning(now.addDays(29), now).contains("caduca el 17-11-2026"));
        QVERIFY(AeatDirectBackend::expiryWarning(now.addDays(-1), now).contains("caducó el 18-10-2026"));
    }

    // A first sync failing partway through the months adopts nothing: the next one starts over.
    void test_firstSyncFailingMidWalkAdoptsNothing()
    {
        m_server->setQueryByMonth(true);
        registerWithAnOlderDatedTip();
        for (int i = 0; i < 4; ++i)
            m_server->enqueue({});                                            // months 0 to -3 answered
        m_server->enqueue({ QByteArray(), 200, 0, true });                    // month -4: no answer
        AeatDirectBackend restored(config(), m_db);
        QVERIFY(!sync(restored));
        QVERIFY(!restored.chainReady());
        QVERIFY(!AeatStore(m_db, "pruebas").chainSynced(config().issuerNif));
        QVERIFY(sync(restored));                                              // all 24 months this time
        VerifactuInvoice next = ticket("3", 30);
        next.setInvoiceDate(QDate::currentDate());
        QVERIFY(waitFor(restored, restored.submitInvoiceAsync(next)).isSuccess());
        QCOMPARE(m_server->sentRecords().last().invoiceNumber, QStringLiteral("3"));
        QCOMPARE(m_server->sentRecords().last().previousHash,
                 m_server->registered().value(QStringLiteral("Alta:2")).hash);
    }

    // Our highest number at AEAT reads "7-1" as 7 and ignores the self-test's records and
    // other systems' numbers.
    void test_highestNumberAtAeat()
    {
        registerElsewhere({ "7-1", "PRUEBA-20261012142926" });
        registerElsewhere({ "9000" }, { "IreneSolutions", "B11111111", "VERIFACTU-API", "IR", "1.0", "GW-1" });
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(sync(backend));
        QCOMPARE(backend.highestNumberAtAeat(), 9000);                         // the gateway's numbers count too
    }

    // The helpers in sql_lite read the table the store writes (same kind, state and
    // environment values): a payment whose rows were never stored still holds its number,
    // and its unsent record counts in its environment.
    void test_storeValuesSeenBySqlLite()
    {
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(sync(backend));
        m_server->enqueue({ QByteArray(), 200, 0, true });
        QCOMPARE(waitFor(backend, backend.submitInvoiceAsync(ticket("U1", 10))).status, VerifactuResult::NETWORK_ERROR);
        QCOMPARE(nextVerifactuInvoiceSeq(m_db, "U1"), 1);
        QCOMPARE(aeatPendingRecordCount(m_db, "pruebas"), 1);
        QCOMPARE(aeatPendingRecordCount(m_db, "produccion"), 0);
    }

    // A later sync whose second month fails keeps what AEAT already answered for the
    // first: the restored database still continues after the record only AEAT has.
    void test_laterSyncKeepsWhatAeatAlreadyAnswered()
    {
        {
            AeatDirectBackend first(config(), m_db);
            QVERIFY(waitFor(first, first.submitInvoiceAsync(ticket("1", 10))).isSuccess());
        }
        const QString afterBackup = registerElsewhere({ "2" });
        m_server->enqueue({});                                             // this month: answered
        m_server->enqueue({ QByteArray(), 200, 0, true });                 // previous month: lost
        AeatDirectBackend restored(config(), m_db);
        QVERIFY(waitFor(restored, restored.submitInvoiceAsync(ticket("3", 30))).isSuccess());
        QCOMPARE(m_server->sentRecords().last().previousHash, afterBackup);
        QCOMPARE(restored.highestNumberAtAeat(), 2);
    }

    // The first sync must reach AEAT: when it cannot, nothing is generated (a new chain
    // could fork our system's chain at AEAT) and the next attempt follows AEAT's tip.
    void test_firstSyncNetworkFailureGeneratesNothing()
    {
        const QString tip = registerElsewhere({ "1" });
        m_server->enqueue({ QByteArray(), 200, 0, true });
        AeatDirectBackend backend(config(), m_db);
        const VerifactuResult held = waitFor(backend, backend.submitInvoiceAsync(ticket("2", 20)));
        QCOMPARE(held.status, VerifactuResult::NETWORK_ERROR);
        AeatStore store(m_db, "pruebas");
        QVERIFY(!store.latest(AeatStore::Kind::Registration, "2").isValid());
        QVERIFY(!store.chainSynced(config().issuerNif));
        QVERIFY(waitFor(backend, backend.submitInvoiceAsync(ticket("2", 20))).isSuccess());
        QCOMPARE(m_server->sentRecords().last().previousHash, tip);
    }

    // The chain marker kept outside the database (settings): it follows every record, and
    // a database whose chain differs (restored from an older backup, or a new PC) must
    // reach AEAT before any record - no fallback to its stale local chain.
    void test_chainMarkerOutsideTheDatabase()
    {
        QString marker;
        AeatDirectBackend::Config c = config();
        c.readChainMarker = [&marker] { return marker; };
        c.writeChainMarker = [&marker](const QString &m) { marker = m; };
        QString first;
        {
            AeatDirectBackend backend(c, m_db);
            first = waitFor(backend, backend.submitInvoiceAsync(ticket("1", 10))).rawHash;
        }
        QCOMPARE(marker, QStringLiteral("pruebas|89890001K|") + first);

        // Matching: AEAT out of reach is no obstacle (the local chain is this installation's).
        m_server->enqueue({ QByteArray(), 200, 0, true });
        {
            AeatDirectBackend same(c, m_db);
            QVERIFY(sync(same));
        }
        // Differing: the database is not the one this installation last used.
        marker = QStringLiteral("pruebas|89890001K|") + QString(64, 'A');
        m_server->enqueue({ QByteArray(), 200, 0, true });
        AeatDirectBackend restored(c, m_db);
        bool done = false, ok = true;
        restored.syncChain([&](bool o, const QString &) { done = true; ok = o; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(!ok);
        QCOMPARE(waitFor(restored, restored.submitInvoiceAsync(ticket("2", 20))).status, VerifactuResult::SUCCESS);
        QCOMPARE(marker, QStringLiteral("pruebas|89890001K|") + m_server->sentRecords().last().hash);
    }

    // After the first sync, AEAT out of reach does not stop the shop: the local chain
    // is used and the message says AEAT was not asked.
    void test_laterSyncFallsBackToLocalChain()
    {
        QString ours;
        {
            AeatDirectBackend first(config(), m_db);
            ours = waitFor(first, first.submitInvoiceAsync(ticket("1", 10))).rawHash;
        }
        m_server->enqueue({ QByteArray(), 200, 0, true });
        AeatDirectBackend next(config(), m_db);
        bool ok = false;
        QString message;
        next.syncChain([&](bool o, const QString &m) { ok = o; message = m; });
        QTRY_VERIFY_WITH_TIMEOUT(!message.isEmpty(), 30000);
        QVERIFY2(ok && message.contains("sin consultar la AEAT"), qPrintable(message));
        QVERIFY(waitFor(next, next.submitInvoiceAsync(ticket("2", 20))).isSuccess());
        QCOMPARE(m_server->sentRecords().last().previousHash, ours);
    }

    // AEAT holds the invoice from another system and our first send never got there (no
    // connection): AEAT's duplicate answer to the resend is not ours - only a send that
    // reached AEAT (the reply lost) makes a duplicate ours (test_lostReplyResendsSameRecord).
    void test_duplicateOfAnotherSystemAfterAFailedSend()
    {
        registerElsewhere({ "510" }, { "IreneSolutions", "B11111111", "VERIFACTU-API", "IR", "1.0", "GW-1" });
        {
            QTcpServer closed;
            QVERIFY(closed.listen(QHostAddress::LocalHost));
            const quint16 port = closed.serverPort();
            closed.close();
            AeatDirectBackend backend(config(), m_db);
            QVERIFY(sync(backend));
            AeatDirectBackend::setEndpointOverride(QStringLiteral("http://127.0.0.1:%1/never-used").arg(port));
            QCOMPARE(waitFor(backend, backend.submitInvoiceAsync(ticket("510", 10))).status, VerifactuResult::NETWORK_ERROR);
            AeatDirectBackend::setEndpointOverride(m_server->url());
        }
        AeatStore store(m_db, "pruebas");
        QVERIFY(!store.latest(AeatStore::Kind::Registration, "510").reached);
        AeatDirectBackend again(config(), m_db);                              // sends it at start
        QTRY_COMPARE_WITH_TIMEOUT(store.latest(AeatStore::Kind::Registration, "510").state, AeatStore::kDuplicate, 15000);
        QVERIFY(store.latest(AeatStore::Kind::Registration, "510").reached);
    }

    // A reply with no line for a record it carried: that record stays pending (never
    // taken as refused, which would bring a Subsanacion, nor as accepted without a CSV),
    // its request hears PENDING, and it goes out again on its own and settles once.
    void test_replyWithoutALineForARecord()
    {
        AeatDirectBackend::setRetryDelaySeconds(1);
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(sync(backend));
        QSignalSpy settled(&backend, &VerifactuBackend::recordSettled);
        m_server->omitLineFor("971");
        QHash<QString, VerifactuResult> answers;
        connect(&backend, &VerifactuBackend::requestFinished, this,
                [&answers](const QString &id, const VerifactuResult &r) { answers.insert(id, r); });
        const QString first = backend.submitInvoiceAsync(ticket("970", 10));
        const QString second = backend.submitInvoiceAsync(ticket("971", 20));    // same batch
        QTRY_VERIFY_WITH_TIMEOUT(answers.contains(first) && answers.contains(second), 10000);
        QVERIFY(answers[first].isSuccess());
        QCOMPARE(answers[second].status, VerifactuResult::PENDING);
        AeatStore store(m_db, "pruebas");
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "971").state, AeatStore::kPending);
        QTRY_COMPARE_WITH_TIMEOUT(store.latest(AeatStore::Kind::Registration, "971").state, AeatStore::kAccepted, 10000);
        QVERIFY(!m_server->sentRecords().last().afterRejection);
        int settled971 = 0;
        for (const QList<QVariant> &s : settled)
            settled971 += s[0].toString() == QLatin1String("971");
        QCOMPARE(settled971, 1);
    }

    // AEAT answers a query in pages: the sync follows ClavePaginacion to the last page,
    // so the chain continues after a record that is not on the first one.
    void test_syncFollowsQueryPages()
    {
        const QString tip = registerElsewhere({ "1", "2", "3" });
        m_server->setQueryPageSize(1);
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(waitFor(backend, backend.submitInvoiceAsync(ticket("4", 40))).isSuccess());
        QCOMPARE(m_server->sentRecords().last().previousHash, tip);
        int paged = 0;
        for (const FakeAeatServer::Request &r : m_server->requests())
            paged += r.kind == QLatin1String("Consulta") && !r.pageAfter.isEmpty();
        QVERIFY2(paged >= 2, qPrintable(QString::number(paged)));
    }

    // AEAT's stored answer: none and pending give nothing; an accepted record gives SUCCESS
    // with its CSV, a refused one ERROR with AEAT's code.
    void test_storedOutcomeOnlyOnceAnswered()
    {
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(sync(backend));
        VerifactuResult r;
        QVERIFY(!backend.storedOutcome(AeatStore::Kind::Registration, "7", &r));             // none
        m_server->rejectNext("8", "1100", "Valor o tipo incorrecto del campo: ImporteTotal");
        waitFor(backend, backend.submitInvoiceAsync(ticket("8", 20)));
        QVERIFY(backend.storedOutcome(AeatStore::Kind::Registration, "8", &r));
        QCOMPARE(r.status, VerifactuResult::ERROR);
        QCOMPARE(r.errorCode, QStringLiteral("1100"));
        QVERIFY(waitFor(backend, backend.submitInvoiceAsync(ticket("9", 30))).isSuccess());
        QVERIFY(backend.storedOutcome(AeatStore::Kind::Registration, "9", &r));
        QVERIFY(r.isSuccess() && !r.csv.isEmpty());
        m_server->enqueue({ QByteArray(), 200, 0, true });
        waitFor(backend, backend.submitInvoiceAsync(ticket("7", 10)));
        QVERIFY(!backend.storedOutcome(AeatStore::Kind::Registration, "7", &r));             // pending
    }

    // "Pending" means pending at AEAT: a refused (or settled) record is not one.
    void test_holdsPendingRecordOnlyWhilePending()
    {
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(sync(backend));
        m_server->rejectNext("5", "1100", "Valor o tipo incorrecto del campo: ImporteTotal");
        QCOMPARE(waitFor(backend, backend.submitInvoiceAsync(ticket("5", 10))).status, VerifactuResult::ERROR);
        QVERIFY(!backend.holdsPendingRecord(AeatStore::Kind::Registration, "5"));
        m_server->enqueue({ QByteArray(), 200, 0, true });
        QCOMPARE(waitFor(backend, backend.submitInvoiceAsync(ticket("6", 20))).status, VerifactuResult::NETWORK_ERROR);
        QVERIFY(backend.holdsPendingRecord(AeatStore::Kind::Registration, "6"));
    }

    // A send cut off before its answer (the app closed, or Configuración replaced the
    // backend) may have reached AEAT: its duplicate answer to the resend is ours.
    void test_duplicateAfterAnInterruptedSendIsOurs()
    {
        {
            AeatDirectBackend backend(config(), m_db);
            QVERIFY(sync(backend));
            m_server->enqueue({ QByteArray(), 200, 3000 });                   // registered, answered late
            backend.submitInvoiceAsync(ticket("1", 10));
            QTRY_COMPARE_WITH_TIMEOUT(m_server->submissions().size(), 1, 10000);
        }                                                                     // gone before the answer
        QTest::qWait(3500);
        AeatStore store(m_db, "pruebas");
        AeatDirectBackend again(config(), m_db);                              // resends it at start
        QTRY_COMPARE_WITH_TIMEOUT(store.latest(AeatStore::Kind::Registration, "1").state, AeatStore::kAccepted, 10000);
        QVERIFY(store.latest(AeatStore::Kind::Registration, "1").reached);
    }

    // The QR printed before AEAT answers comes only from this issuer's record still
    // pending, with the invoice's date and amount: none once AEAT accepted or refused it.
    void test_queuedRecordQrOnlyForItsPendingRecord()
    {
        AeatDirectBackend::setRetryDelaySeconds(1);
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(sync(backend));
        m_server->enqueue({ QByteArray(), 200, 0, true });
        QCOMPARE(waitFor(backend, backend.submitInvoiceAsync(ticket("960", 10))).status, VerifactuResult::NETWORK_ERROR);
        QVERIFY(!backend.queuedRecordQr(ticket("960", 10)).isNull());          // pending: its QR
        QVERIFY(backend.queuedRecordQr(ticket("960", 11)).isNull());           // another amount
        AeatDirectBackend::Config other = config();
        other.issuerNif = "B11111111";
        AeatDirectBackend otherIssuer(other, m_db);
        VerifactuInvoice theirs = ticket("960", 10);
        theirs.setSellerNIF("B11111111");
        QVERIFY(otherIssuer.queuedRecordQr(theirs).isNull());                  // not this issuer's record

        QVERIFY(waitFor(backend, backend.submitInvoiceAsync(ticket("961", 10))).isSuccess());
        QVERIFY(backend.queuedRecordQr(ticket("961", 10)).isNull());           // accepted: no longer queued
        m_server->rejectNext("962", "1100", "Valor o tipo incorrecto del campo: ImporteTotal");
        QCOMPARE(waitFor(backend, backend.submitInvoiceAsync(ticket("962", 10))).status, VerifactuResult::ERROR);
        QVERIFY(backend.queuedRecordQr(ticket("962", 10)).isNull());           // refused
    }

    // An invoice number is unique per issuer only: after the NIF changes, the same number
    // is a new invoice of the new issuer - never answered with the old issuer's outcome,
    // never flagged as a resend of the old one's refused record.
    void test_sameNumberUnderAnotherIssuer()
    {
        {
            AeatDirectBackend first(config(), m_db);
            QVERIFY(sync(first));
            QVERIFY(waitFor(first, first.submitInvoiceAsync(ticket("900", 10))).isSuccess());
        }
        const int sentBefore = m_server->sentRecords().size();
        AeatDirectBackend::Config other = config();
        other.issuerNif = "B11111111";
        other.issuerName = "Otra empresa";
        AeatDirectBackend second(other, m_db);
        QVERIFY(sync(second));
        VerifactuInvoice inv = ticket("900", 20);
        inv.setSellerNIF("B11111111");
        const VerifactuResult r = waitFor(second, second.submitInvoiceAsync(inv));
        QVERIFY2(r.isSuccess(), qPrintable(r.errorDescription));
        QCOMPARE(m_server->sentRecords().size(), sentBefore + 1);              // a new record was sent
        QVERIFY(!m_server->sentRecords().last().afterRejection);
        QVERIFY(m_server->submissions().last().body.contains("<sf:IDEmisorFactura>B11111111</sf:IDEmisorFactura>"));
        AeatStore store(m_db, "pruebas");
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "900", "B11111111").issuerNif, QStringLiteral("B11111111"));
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "900", config().issuerNif).issuerNif, config().issuerNif);
    }

    // A submission carries one issuer in its header: records left pending under one
    // issuer are never sent by a backend configured for another (found in the AEAT
    // test runs: AEAT refused them with 4104), and go out once their issuer is back.
    void test_outboxPerIssuer()
    {
        {
            AeatDirectBackend first(config(), m_db);
            QVERIFY(sync(first));
            m_server->enqueue({ QByteArray(), 200, 0, true });
            QCOMPARE(waitFor(first, first.submitInvoiceAsync(ticket("800", 10))).status, VerifactuResult::NETWORK_ERROR);
        }
        const auto sends800 = [this] {
            int n = 0;
            for (const FakeAeatServer::SentRecord &r : m_server->sentRecords())
                n += r.invoiceNumber == QLatin1String("800");
            return n;
        };
        QCOMPARE(sends800(), 1);
        AeatDirectBackend::Config other = config();
        other.issuerNif = "B00000000";
        other.issuerName = "OTRO EMISOR";
        {
            AeatDirectBackend second(other, m_db);
            // Its own submission would carry every pending record of the environment.
            QVERIFY(waitFor(second, second.submitInvoiceAsync(ticket("801", 10))).isSuccess());
            QCOMPARE(sends800(), 1);
        }
        AeatDirectBackend again(config(), m_db);
        AeatStore store(m_db, "pruebas");
        QTRY_COMPARE_WITH_TIMEOUT(store.latest(AeatStore::Kind::Registration, "800").state, AeatStore::kAccepted, 10000);
    }

    // AEAT already holds another system's record for the invoice: accepted, without
    // claiming our record; answered from the store afterwards.
    void test_duplicateOfAnotherSystem()
    {
        {
            QSqlDatabase other = QSqlDatabase::addDatabase("QSQLITE", "aeat_other_db");
            other.setDatabaseName(m_dir.filePath(QString("other_%1.db").arg(m_dbCounter)));
            AeatDirectBackend gateway(config(), other);
            QVERIFY(waitFor(gateway, gateway.submitInvoiceAsync(ticket("500", 10))).isSuccess());
            other.close();
        }
        QSqlDatabase::removeDatabase("aeat_other_db");
        AeatDirectBackend backend(config(), m_db);
        const VerifactuResult r = waitFor(backend, backend.submitInvoiceAsync(ticket("500", 10)));
        QVERIFY2(r.isSuccess(), qPrintable(r.errorDescription));
        QVERIFY(r.rawXml.isEmpty() && r.rawHash.isEmpty());
        QVERIFY(r.csv.startsWith("IdPeticion "));
        AeatStore store(m_db, "pruebas");
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "500").state, AeatStore::kDuplicate);
        const int sends = m_server->submissions().size();
        const VerifactuResult again = waitFor(backend, backend.submitInvoiceAsync(ticket("500", 10)));
        QVERIFY(again.isSuccess() && again.rawXml.isEmpty());
        QCOMPARE(m_server->submissions().size(), sends);
    }

    // AEAT holds the invoice as cancelled: reported, never registered again.
    void test_duplicateOfCancelledInvoice()
    {
        {
            QSqlDatabase other = QSqlDatabase::addDatabase("QSQLITE", "aeat_other_db");
            other.setDatabaseName(m_dir.filePath(QString("other_%1.db").arg(m_dbCounter)));
            AeatDirectBackend gateway(config(), other);
            QVERIFY(waitFor(gateway, gateway.submitInvoiceAsync(ticket("600", 10))).isSuccess());
            QVERIFY(waitFor(gateway, gateway.cancelInvoiceAsync("600", QDate(2026, 10, 10))).isSuccess());
            other.close();
        }
        QSqlDatabase::removeDatabase("aeat_other_db");
        AeatDirectBackend backend(config(), m_db);
        const VerifactuResult r = waitFor(backend, backend.submitInvoiceAsync(ticket("600", 10)));
        QCOMPARE(r.status, VerifactuResult::ERROR);
        QVERIFY2(r.errorDescription.contains("anulada"), qPrintable(r.errorDescription));
        AeatStore store(m_db, "pruebas");
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "600").state, AeatStore::kCancelledAtAeat);
        const int sends = m_server->submissions().size();
        QCOMPARE(waitFor(backend, backend.submitInvoiceAsync(ticket("600", 10))).status, VerifactuResult::ERROR);
        QCOMPARE(m_server->submissions().size(), sends);                  // no new record, no Subsanacion
    }

    // After a failed send the records are retried on their own; an outcome nobody
    // waited for is reported on recordSettled.
    void test_automaticRetry()
    {
        AeatDirectBackend::setRetryDelaySeconds(1);
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(sync(backend));
        QSignalSpy settled(&backend, &VerifactuBackend::recordSettled);
        m_server->enqueue({ QByteArray(), 200, 0, true });
        QCOMPARE(waitFor(backend, backend.submitInvoiceAsync(ticket("700", 10))).status, VerifactuResult::NETWORK_ERROR);
        AeatStore store(m_db, "pruebas");
        QTRY_COMPARE_WITH_TIMEOUT(store.latest(AeatStore::Kind::Registration, "700").state, AeatStore::kAccepted, 10000);
        QTRY_COMPARE(settled.size(), 1);
        QCOMPARE(settled[0][0].toString(), QStringLiteral("700"));
        QVERIFY(!settled[0][1].toBool());
        QVERIFY(settled[0][2].value<VerifactuResult>().isSuccess());
        // Reported also when a request waited for it: the dialog may have closed meanwhile.
        QVERIFY(waitFor(backend, backend.submitInvoiceAsync(ticket("701", 10))).isSuccess());
        QTRY_COMPARE(settled.size(), 2);
        QCOMPARE(settled[1][0].toString(), QStringLiteral("701"));
    }

    // AEAT returns records without their hash: the sync fails instead of starting a new chain.
    void test_syncRefusesRecordsWithoutHash()
    {
        QVERIFY(!registerElsewhere({ "1" }).isEmpty());
        m_server->setQueryWithoutHashes(true);
        AeatDirectBackend backend(config(), m_db);
        bool done = false, ok = true;
        QString message;
        backend.syncChain([&](bool o, const QString &m) { done = true; ok = o; message = m; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(!ok && message.contains("sin su huella"), qPrintable(message));
        QVERIFY(!backend.chainReady());
        const VerifactuResult r = waitFor(backend, backend.submitInvoiceAsync(ticket("2", 20)));
        QCOMPARE(r.status, VerifactuResult::NETWORK_ERROR);
        QVERIFY(!AeatStore(m_db, "pruebas").latest(AeatStore::Kind::Registration, "2").isValid());
    }

    // The client certificate, through Windows' crypto API: a .pfx with its password
    // (imported in memory), refused with a readable reason when the password is wrong,
    // it has expired or the store thumbprint is not installed.
    void test_certificate()
    {
        const QString dir = QStringLiteral(AEAT_CERT_DIR);
        AeatCertificate cert;
        QString error;
        QVERIFY2(cert.loadFromFile(dir + "/test-client.p12", "prueba-laideal", &error), qPrintable(error));
        QVERIFY(cert.isLoaded());
        const AeatCertificate::Info info = cert.info();
        QVERIFY2(info.subject.contains("CERTIFICADO DE PRUEBA LAIDEAL"), qPrintable(info.subject));
        QVERIFY(info.hasPrivateKey);
        QCOMPARE(info.thumbprint.size(), 40);
        QVERIFY(info.expiry > QDateTime::currentDateTime().addYears(10));
        QCOMPARE(info.nif, QStringLiteral("00000000T"));                 // from serialNumber IDCES-00000000T

        AeatCertificate wrong;
        QVERIFY(!wrong.loadFromFile(dir + "/test-client.p12", "wrong", &error));
        QVERIFY(error.contains("contraseña"));
        QVERIFY(!wrong.isLoaded());
        QVERIFY(!wrong.loadFromFile(dir + "/expired-client.p12", "prueba-laideal", &error));
        QVERIFY2(error.contains("caducó"), qPrintable(error));
        QVERIFY(!wrong.loadFromStore("00" + QString(38, 'A'), &error));
        QVERIFY(error.contains("instalado"));
        QVERIFY(!wrong.loadFromStore("", &error));
        // The personal store can be listed (whatever is installed on this machine).
        for (const AeatCertificate::Info &c : AeatCertificate::personalCertificates())
            QVERIFY(c.hasPrivateKey && c.thumbprint.size() == 40);
    }

    // With a certificate loaded, a request reaches the server over WinHTTP as usual
    // (plain http here: the certificate is only presented in a TLS handshake).
    void test_backendWithFileCertificate()
    {
        AeatDirectBackend::Config c = config();
        c.certificatePath = QStringLiteral(AEAT_CERT_DIR) + "/test-client.p12";
        c.certificatePassword = "prueba-laideal";
        AeatDirectBackend backend(c, m_db);
        QVERIFY2(backend.isConfigured(), qPrintable(backend.configurationError()));
        QVERIFY(waitFor(backend, backend.submitInvoiceAsync(ticket("30837", 24.79))).isSuccess());
    }
};

QTEST_MAIN(TestAeatDirectBackend)
#include "test_aeat_direct_backend.moc"
