// AeatDirectBackend end to end against FakeAeatServer (plain HTTP on 127.0.0.1):
// records stored and chained, sent together, AEAT's wait time honoured, lost replies
// and rejections, cancellation, query, the outbox sent at the next start, and the
// client certificate (throwaway self-signed fixtures, never a real certificate).

#include <QtTest>
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
#include "fakeaeatserver.h"
#include "verifactuinvoice.h"

class TestAeatDirectBackend : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir   m_dir;
    QSqlDatabase    m_db;
    FakeAeatServer *m_server = nullptr;
    int             m_dbCounter = 0;

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
        m_server->enqueue({ QByteArray(), 200, 0, false, true });
        AeatDirectBackend backend(config(), m_db);
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

    // A fault from AEAT's side leaves the record pending; one about the request rejects it.
    void test_faults()
    {
        m_server->enqueue({ FakeAeatServer::faultReply("env:Server", "Servicio no disponible"), 500 });
        AeatDirectBackend backend(config(), m_db);
        const VerifactuResult server = waitFor(backend, backend.submitInvoiceAsync(ticket("1", 10)));
        QCOMPARE(server.status, VerifactuResult::NETWORK_ERROR);
        AeatStore store(m_db, "pruebas");
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "1").state, AeatStore::kPending);

        m_server->enqueue({ FakeAeatServer::faultReply("env:Client", "Codigo[4102].El XML no cumple el esquema"), 500 });
        AeatDirectBackend later(config(), m_db);
        const VerifactuResult client = waitFor(later, later.submitInvoiceAsync(ticket("1", 10)));
        QCOMPARE(client.status, VerifactuResult::ERROR);
        QVERIFY(client.errorDescription.contains("4102"));
        QCOMPARE(store.latest(AeatStore::Kind::Registration, "1").state, AeatStore::kRejected);
    }

    // Records left unsent when the app closed are sent at the next start, unasked.
    void test_outboxSentAtNextStart()
    {
        m_server->enqueue({ QByteArray(), 200, 0, true });
        {
            AeatDirectBackend backend(config(), m_db);
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

    // The chain can continue from the last record the gateway sent.
    void test_seededChain()
    {
        AeatDirectBackend backend(config(), m_db);
        QVERIFY(backend.seedChain({ { "89890001K", "30836", QDate(2026, 10, 9) }, "GATEWAYHASH" }));
        QVERIFY(waitFor(backend, backend.submitInvoiceAsync(ticket("30837", 24.79))).isSuccess());
        QCOMPARE(m_server->sentRecords()[0].previousHash, QStringLiteral("GATEWAYHASH"));
        QVERIFY(m_server->submissions()[0].body.contains("<sf:NumSerieFactura>30836</sf:NumSerieFactura>"));
    }

    // Switching from the gateway: the chain continues after the issuer's newest record
    // AEAT holds (here registered by another installation, as the gateway did).
    void test_continueChainFromAeat()
    {
        QString gatewayHash;
        {
            QSqlDatabase other = QSqlDatabase::addDatabase("QSQLITE", "aeat_gateway_db");
            other.setDatabaseName(m_dir.filePath(QString("gateway_%1.db").arg(m_dbCounter)));
            AeatDirectBackend gateway(config(), other);
            QVERIFY(waitFor(gateway, gateway.submitInvoiceAsync(ticket("1", 10))).isSuccess());
            const VerifactuResult second = waitFor(gateway, gateway.submitInvoiceAsync(ticket("2", 20)));
            QVERIFY(second.isSuccess());
            gatewayHash = second.rawHash;
            other.close();
        }
        QSqlDatabase::removeDatabase("aeat_gateway_db");

        AeatDirectBackend backend(config(), m_db);
        bool ok = false;
        QString message;
        backend.continueChainFromAeat({}, [&](bool o, const QString &m) { ok = o; message = m; });
        QTRY_VERIFY_WITH_TIMEOUT(!message.isEmpty(), 10000);
        QVERIFY2(ok && message.contains("tras el registro 2"), qPrintable(message));
        QVERIFY(waitFor(backend, backend.submitInvoiceAsync(ticket("3", 30))).isSuccess());
        QCOMPARE(m_server->sentRecords().last().previousHash, gatewayHash);

        // Once started, it is left alone.
        message.clear();
        backend.continueChainFromAeat({}, [&](bool o, const QString &m) { ok = o; message = m; });
        QTRY_VERIFY_WITH_TIMEOUT(!message.isEmpty(), 10000);
        QVERIFY2(ok && message.contains("ya continúa"), qPrintable(message));
    }

    // A cancellation the gateway generated after the newest registration is the head
    // (the query does not return cancellations; the stored record XML has it).
    void test_continueChainAfterLocalCancellation()
    {
        AeatDirectBackend registering(config(), m_db);
        const VerifactuResult alta = waitFor(registering, registering.submitInvoiceAsync(ticket("1", 10)));
        QVERIFY(alta.isSuccess());

        AeatRecord::Cancellation c;
        c.invoice = { "89890001K", "1", QDate(2026, 10, 10) };
        c.previous = { c.invoice, alta.rawHash };
        c.generatedAt = QDateTime::currentDateTime().addSecs(60);
        const QString cancellationXml = AeatRecord::cancellationXml(c, config().system);

        QSqlDatabase fresh = QSqlDatabase::addDatabase("QSQLITE", "aeat_fresh_db");
        fresh.setDatabaseName(m_dir.filePath(QString("fresh_%1.db").arg(m_dbCounter)));
        {
            AeatDirectBackend backend(config(), fresh);
            QString message;
            bool ok = false;
            backend.continueChainFromAeat({ cancellationXml, "<not a record/>" }, [&](bool o, const QString &m) { ok = o; message = m; });
            QTRY_VERIFY_WITH_TIMEOUT(!message.isEmpty(), 10000);
            QVERIFY2(ok && message.contains("anulación"), qPrintable(message));
            QVERIFY(waitFor(backend, backend.submitInvoiceAsync(ticket("2", 20))).isSuccess());
            QCOMPARE(m_server->sentRecords().last().previousHash, AeatRecord::hashOf(c));
        }
        fresh.close();
        fresh = QSqlDatabase();
        QSqlDatabase::removeDatabase("aeat_fresh_db");
    }

    // AEAT holds nothing for the issuer: the chain starts with PrimerRegistro. An empty
    // month is skipped (looking back to the previous one).
    void test_continueChainLooksBack()
    {
        AeatDirectBackend backend(config(), m_db);
        QString message;
        bool ok = false;
        backend.continueChainFromAeat({}, [&](bool o, const QString &m) { ok = o; message = m; });
        QTRY_VERIFY_WITH_TIMEOUT(!message.isEmpty(), 30000);
        QVERIFY2(ok && message.contains("primer registro"), qPrintable(message));
        int queries = 0;
        for (const FakeAeatServer::Request &r : m_server->requests())
            queries += r.kind == QLatin1String("Consulta");
        QCOMPARE(queries, 24);                                           // two years back
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

    // Without the test server, the backend needs a readable certificate.
    void test_certificateRequired()
    {
        AeatDirectBackend::setEndpointOverride(QString());
        AeatDirectBackend::Config c = config();
        c.certificatePath = m_dir.filePath("missing.p12");
        AeatDirectBackend backend(c, m_db);
        QVERIFY(!backend.isConfigured());
        QVERIFY(backend.configurationError().contains("certificado"));
        QCOMPARE(waitFor(backend, backend.submitInvoiceAsync(ticket("1", 10))).status, VerifactuResult::INVALID_CONFIG);
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
