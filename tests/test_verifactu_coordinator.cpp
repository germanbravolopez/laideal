// VerifactuCoordinator without the window: the pending-submit bookkeeping, the bounded
// wait for AEAT's reply and the gateway connection test, against FakeVerifactuServer on
// 127.0.0.1 (never the real AEAT) and a throwaway database and settings file.

#include <QtTest>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "appsettings.h"
#include "fakeverifactuserver.h"
#include "modalautocloser.h"
#include "sql_lite.h"
#include "testschema.h"
#include "verifactucoordinator.h"
#include "verifactuconfig.h"

class TestVerifactuCoordinator : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QSqlDatabase m_db;
    FakeVerifactuServer m_server;
    VerifactuCoordinator *m_coordinator = nullptr;

    void exec(const QString &sql)
    {
        m_db.open();
        QSqlQuery q(m_db);
        QVERIFY2(q.exec(sql), qPrintable(q.lastError().text() + " :: " + sql));
        m_db.close();
    }

    QString scalar(const QString &sql)
    {
        m_db.open();
        QSqlQuery q(m_db);
        QString v;
        if (q.exec(sql) && q.first())
            v = q.value(0).toString();
        m_db.close();
        return v;
    }

    // One paid garment waiting for AEAT, as the save leaves it.
    void seedPaidTicket(const QString &nRecibo)
    {
        exec(QStringLiteral("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, "
                            "fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, "
                            "observaciones, edit_lock, hash, verifactu_estado, verifactu_invoice_seq) "
                            "VALUES ('%1', 'Cliente', '05-02-2026', '05-02-2026', '', '12.10', 'SI', "
                            "'En tienda', '1', 'Camisa', '', 'Limp.', '', 0, 'hc%1', 'PENDIENTE', 0)")
             .arg(nRecibo));
    }

    QString estadoOf(const QString &nRecibo)
    {
        return scalar(QStringLiteral("SELECT verifactu_estado FROM ingresos WHERE n_recibo='%1'").arg(nRecibo));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(m_server.start());
        VerifactuConfig::setEndpointOverride(m_server.baseUrl());
        QVERIFY(VerifactuConfig().getEndpointUrl().startsWith(QLatin1String("http://127.0.0.1:")));

        const QString settingsPath = m_dir.filePath("settings.json");
        QFile f(settingsPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
        f.close();
        AppSettings *s = AppSettings::instance();
        s->loadFrom(settingsPath);
        s->setVerifactuNif("B00000000");
        s->setVerifactuName("Tintoreria Coordinator");
        s->setVerifactuServiceKey("coordinator-service-key");
        s->setVerifactuProduction(false);
        s->setEnablePrinting(false);

        m_db = QSqlDatabase::addDatabase("QSQLITE", "coordinator");
        m_db.setDatabaseName(m_dir.filePath("laideal_coordinator.db"));
        QVERIFY(TestSchema::create(m_db));

        m_coordinator = new VerifactuCoordinator(m_db, this);
        bool created = false;
        QVERIFY(m_coordinator->initialize(&created));
        QVERIFY(created);
        QVERIFY(m_coordinator->integration());
        bool createdAgain = true;
        QVERIFY(m_coordinator->initialize(&createdAgain));
        QVERIFY(!createdAgain);
    }

    void cleanupTestCase()
    {
        VerifactuConfig::setEndpointOverride(QString());
    }

    // The reply patches the rows of the submission it answers, clears the pending entry and
    // tells the window through the status text.
    void test_acceptedReplySettlesThePendingSubmit()
    {
        seedPaidTicket("7001");
        QSignalSpy status(m_coordinator, &VerifactuCoordinator::statusMessage);
        QSignalSpy settled(m_coordinator, &VerifactuCoordinator::submitSettled);
        QSignalSpy refused(m_coordinator, &VerifactuCoordinator::submitRefused);

        const QString reqId = m_coordinator->submitInvoice("7001", QDate(2026, 2, 5), 12.10);
        QVERIFY(!reqId.isEmpty());
        QVERIFY(m_coordinator->hasPendingSubmit(reqId));
        QCOMPARE(status.size(), 1);
        QVERIFY(status.at(0).at(0).toString().contains("7001"));

        QTRY_VERIFY_WITH_TIMEOUT(settled.size() == 1, 10000);
        QVERIFY(!m_coordinator->hasPendingSubmit(reqId));
        QCOMPARE(estadoOf("7001"), QStringLiteral("ENVIADA"));
        QVERIFY(status.last().at(0).toString().contains("enviado a AEAT"));
        QCOMPARE(refused.size(), 0);
    }

    void test_refusedReplyIsReportedAndStoredAsError()
    {
        seedPaidTicket("7002");
        FakeVerifactuServer::Reply refusal;
        refusal.body = FakeVerifactuServer::rejectedReply("4102", "Falta el campo InvoiceID");
        m_server.enqueue("Create", refusal);
        QSignalSpy refused(m_coordinator, &VerifactuCoordinator::submitRefused);
        QSignalSpy settled(m_coordinator, &VerifactuCoordinator::submitSettled);

        QVERIFY(!m_coordinator->submitInvoice("7002", QDate(2026, 2, 5), 12.10).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(settled.size() == 1, 10000);
        QCOMPARE(refused.size(), 1);
        QCOMPARE(refused.at(0).at(0).toString(), QStringLiteral("7002"));
        QCOMPARE(estadoOf("7002"), QStringLiteral("ERROR"));
        QVERIFY(m_coordinator->rejectionHtml("7002", refused.at(0).at(1).value<VerifactuResult>()).contains("7002"));
    }

    // The wait ends on the reply, the window is told to lock the form, and the flag that
    // refuses a close is up only inside it.
    void test_waitForReplyLocksTheFormAndReturnsTheReply()
    {
        seedPaidTicket("7003");
        QSignalSpy started(m_coordinator, &VerifactuCoordinator::aeatWaitStarted);
        QSignalSpy finished(m_coordinator, &VerifactuCoordinator::aeatWaitFinished);
        const QString reqId = m_coordinator->submitInvoice("7003", QDate(2026, 2, 5), 12.10);
        QVERIFY(!reqId.isEmpty());

        bool waitingInside = false;
        QTimer::singleShot(0, this, [&] { waitingInside = m_coordinator->isWaitingForAeat(); });
        const VerifactuCoordinator::AeatReply reply = m_coordinator->waitForReply(reqId, "7003");
        QVERIFY(waitingInside);
        QVERIFY(!m_coordinator->isWaitingForAeat());
        QCOMPARE(started.size(), 1);
        QCOMPARE(finished.size(), 1);
        QVERIFY(reply.refusal.isEmpty());
        QCOMPARE(reply.success, !reply.qrCode.isNull());
    }

    // A reply later than the bounded wait is flagged: the status says the factura is printable now.
    void test_lateReplyAfterAQrlessPrintSaysTheFacturaIsPrintable()
    {
        seedPaidTicket("7004");
        FakeVerifactuServer::Reply slow;
        slow.delayMs = 3600;
        m_server.enqueue("Create", slow);
        QSignalSpy status(m_coordinator, &VerifactuCoordinator::statusMessage);
        QSignalSpy settled(m_coordinator, &VerifactuCoordinator::submitSettled);

        const QString reqId = m_coordinator->submitInvoice("7004", QDate(2026, 2, 5), 12.10);
        QVERIFY(!reqId.isEmpty());
        const VerifactuCoordinator::AeatReply reply = m_coordinator->waitForReply(reqId, "7004");
        QVERIFY(!reply.success);
        QVERIFY(m_coordinator->hasPendingSubmit(reqId));
        m_coordinator->markPrintedWithoutQr(reqId);

        QTRY_VERIFY_WITH_TIMEOUT(settled.size() == 1, 10000);
        QVERIFY(status.last().at(0).toString().contains("ya se puede imprimir la factura"));
        QCOMPARE(estadoOf("7004"), QStringLiteral("ENVIADA"));
    }

    void test_anUnknownRequestIsNotPending()
    {
        QVERIFY(!m_coordinator->hasPendingSubmit("not-ours"));
        m_coordinator->markPrintedWithoutQr("not-ours");   // harmless
        QVERIFY(!m_coordinator->hasPendingSubmit("not-ours"));
    }

    // The gateway test needs all three fields and says what the server answered.
    void test_gatewayConnectionTestReportsTheServersAnswer()
    {
        ModalAutoCloser popups;
        m_coordinator->testGatewayConnection(nullptr, "", "", "", false);
        QVERIFY(popups.sawMessageContaining("Datos incompletos"));

        popups.clear();
        FakeVerifactuServer::Reply unauthorized;
        unauthorized.status = 401;
        unauthorized.body = "{}";
        m_server.enqueue("Create", unauthorized);
        m_coordinator->testGatewayConnection(nullptr, "B00000000", "Tintoreria", "a-key", false);
        QVERIFY2(popups.sawMessageContaining("Clave rechazada"), qPrintable(popups.messages().join("|")));
    }

    void test_noticesAreHiddenForTheGateway()
    {
        QVERIFY(!m_coordinator->aeatEnvironmentNotice().visible);   // only the direct client has one
        QVERIFY(!m_coordinator->unsentRecordsNotice().visible);
    }

    // A production client whose start day cannot be read still says PRODUCCIÓN, never PRUEBAS.
    void test_environmentTextFollowsTheEnvironment()
    {
        const QDate day(2026, 11, 20);
        const QString dated = VerifactuCoordinator::aeatEnvironmentText("produccion", day);
        QVERIFY2(dated.contains("PRODUCCIÓN desde el 20-11-2026"), qPrintable(dated));
        const QString undated = VerifactuCoordinator::aeatEnvironmentText("produccion", QDate());
        QVERIFY2(undated.contains("PRODUCCIÓN") && !undated.contains("PRUEBAS") && !undated.contains("desde"),
                 qPrintable(undated));
        QVERIFY(VerifactuCoordinator::aeatEnvironmentText("pruebas", QDate()).contains("PRUEBAS"));
        QVERIFY(VerifactuCoordinator::aeatEnvironmentText("pruebas", day).contains("PRUEBAS"));
    }
};

QTEST_MAIN(TestVerifactuCoordinator)
#include "test_verifactu_coordinator.moc"
