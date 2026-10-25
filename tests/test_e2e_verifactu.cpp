// End-to-end scenarios for the payment / AEAT flows (test bench, phase 1).
//
// Unlike the unit suites, these run the real objects together: PayDialog (driven
// headless, as the Cobrar button would), the real Verifactu client and HTTP stack,
// and the sql_lite writes, against a seeded throwaway database. Every request goes
// to FakeVerifactuServer on 127.0.0.1 through VerifactuConfig::setEndpointOverride,
// so nothing ever reaches the real AEAT. Pop-ups are closed and recorded by
// ModalAutoCloser. Settings come from a throwaway file (AppSettings::loadFrom), with
// printing disabled.

#include <QtTest>
#include <QDateEdit>
#include <QLabel>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>

#include "appsettings.h"
#include "contabilidad.h"
#include "fakeverifactuserver.h"
#include "modalautocloser.h"
#include "pay_dialog.h"
#include "sql_lite.h"
#include "testschema.h"
#include "verifactuconfig.h"
#include "verifactuintegration.h"
#include "verifactumanager.h"
#include "verifacturesponse.h"

class TestE2eVerifactu : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QSqlDatabase m_db;
    FakeVerifactuServer m_server;
    VerifactuIntegration *m_verifactu = nullptr;
    ModalAutoCloser *m_popups = nullptr;

    void exec(const QString &sql)
    {
        m_db.open();
        // Test data is shaped by hand: the Req. 1 guard triggers go first.
        for (const char *drop : { "DROP TRIGGER IF EXISTS ingresos_registered_no_delete",
                                  "DROP TRIGGER IF EXISTS ingresos_accepted_identity" })
            QSqlQuery(m_db).exec(QString::fromLatin1(drop));
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

    // One unpaid garment of a ticket, as MainWindow::saveTicket leaves it.
    void seedGarment(const QString &nRecibo, const QString &hash, const QString &importe,
                     const QString &fechaRecepcion = QStringLiteral("05-02-2026"))
    {
        exec(QStringLiteral("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, "
                            "fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, "
                            "observaciones, edit_lock, hash, verifactu_estado, verifactu_invoice_seq) "
                            "VALUES ('%1', 'Cliente E2E', '%4', '', '', '%3', 'NO', 'En tienda', '1', "
                            "'Camisa', '', 'Limp.', '', 0, '%2', 'SIN COBRAR', 0)")
             .arg(nRecibo, hash, importe, fechaRecepcion));
    }

    // Charges a ticket through the real PayDialog, as the Cobrar button does.
    // Garments whose hash is in `skip` are unticked (partial payment). Returns
    // whether the dialog completed (accept), false when it refused.
    bool payThroughDialog(const QString &nRecibo, const QDate &fechaPago,
                          const QStringList &skip = {}, int waitMs = 15000, QString *resultText = nullptr)
    {
        PayDialog dlg(m_db);
        dlg.m_verifactu = m_verifactu;
        if (!dlg.loadTicket(nRecibo))
            return false;
        dlg.findChild<QDateEdit *>()->setDate(fechaPago);
        auto *table = dlg.findChild<QTableWidget *>();
        for (int r = 0; r < table->rowCount(); ++r)
            if (skip.contains(table->item(r, 7)->text()))       // COL_HASH
                table->item(r, 0)->setCheckState(Qt::Unchecked);
        QMetaObject::invokeMethod(&dlg, "onCobrarClicked");
        QElapsedTimer t;
        t.start();
        while (dlg.result() != QDialog::Accepted && t.elapsed() < waitMs)
            QTest::qWait(50);
        if (resultText)
            *resultText = dlg.findChild<QLabel *>("lblResult")->text();
        return dlg.result() == QDialog::Accepted;
    }

    // The gateway failure modes below all mean "AEAT may or may not have the invoice": the sale
    // stays paid, the row is PENDIENTE with its InvoiceID kept and no CSV (the start-up
    // recovery resends it), and it is never ERROR, which is for a refusal.
    void expectLeftPendiente(const QString &nRecibo, const QString &hash)
    {
        const QString where = QStringLiteral(" FROM ingresos WHERE hash='%1'").arg(hash);
        QCOMPARE(scalar("SELECT pagado" + where), QStringLiteral("SI"));
        QCOMPARE(scalar("SELECT verifactu_estado" + where), QStringLiteral("PENDIENTE"));
        QCOMPARE(scalar("SELECT verifactu_invoice_id" + where), nRecibo);
        QCOMPARE(scalar("SELECT COALESCE(verifactu_csv, '')" + where), QString());
        QVERIFY(!m_server.requestsTo("Create").isEmpty());
        QVERIFY(m_popups->messages().isEmpty());
    }

private slots:
    // The gateway's rectificativas carry no RectificationItems: never sent in PRODUCTION.
    void test_gatewayRectificativaRefusedInProduction()
    {
        AppSettings *s = AppSettings::instance();
        const auto restore = qScopeGuard([s] { s->setVerifactuProduction(false); });
        s->setVerifactuProduction(true);
        VerifactuIntegration production;
        QVERIFY(production.initialize());
        const int before = m_server.requestsTo("Create").size();
        const QString id = production.submitRectificationAsync(
            "1900", QDate::currentDate(), VerifactuInvoice::RECTIFICATION_R5, VerifactuInvoice::BY_DIFFERENCES,
            -1.0, -0.21, 10.0, 2.10, 21.0, "x", "1899", QDate::currentDate());
        QVERIFY(id.isEmpty());
        QVERIFY2(production.getLastError().contains("PRODUCCIÓN"), qPrintable(production.getLastError()));
        QTest::qWait(200);
        QCOMPARE(m_server.requestsTo("Create").size(), before);
    }

    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(m_server.start());
        VerifactuConfig::setEndpointOverride(m_server.baseUrl());
        // Safety net: if the seam ever stopped working, these scenarios would post fake
        // invoices to the real service. Refuse to run unless every endpoint is local.
        QVERIFY2(VerifactuConfig().getEndpointUrl().startsWith(QLatin1String("http://127.0.0.1:")),
                 "Verifactu endpoint is not the local fake server - aborting before any request");

        const QString settingsPath = m_dir.filePath("settings.json");
        QFile f(settingsPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
        f.close();
        AppSettings *s = AppSettings::instance();
        s->loadFrom(settingsPath);
        s->setVerifactuNif("B00000000");
        s->setVerifactuName("Tintoreria E2E");
        s->setVerifactuServiceKey("e2e-service-key");
        s->setVerifactuProduction(false);
        s->setEnablePrinting(false);

        m_db = QSqlDatabase::addDatabase("QSQLITE", "e2e");
        m_db.setDatabaseName(m_dir.filePath("laideal_e2e.db"));
        QVERIFY(TestSchema::create(m_db));

        m_verifactu = new VerifactuIntegration(this);
        QVERIFY2(m_verifactu->initialize(), "Verifactu client must initialise from the test settings");
        m_popups = new ModalAutoCloser(this);
    }

    void cleanupTestCase()
    {
        VerifactuConfig::setEndpointOverride(QString());
    }

    // Probar conexion: any answer used to read "Conexion correcta", a refused key (401,
    // or a key error in the body) and a server error included. Only a plain refusal of
    // the stub's missing fields proves the service and the key.
    void test_testConnectionReportsARefusedKey()
    {
        VerifactuManager mgr;
        mgr.getConfig()->setEmitterData("B00000000", "Tintoreria E2E");
        mgr.getConfig()->setServiceKey("e2e-service-key");

        FakeVerifactuServer::Reply missingFields;
        missingFields.body = FakeVerifactuServer::rejectedReply("4102", "Falta el campo InvoiceID");
        m_server.enqueue("Create", missingFields);
        QVERIFY(mgr.testConnection().isSuccess());

        FakeVerifactuServer::Reply unauthorized;
        unauthorized.status = 401;
        unauthorized.body = "{}";
        m_server.enqueue("Create", unauthorized);
        VerifactuResult r = mgr.testConnection();
        QCOMPARE(r.status, VerifactuResult::ERROR);
        QVERIFY2(r.errorDescription.contains("Clave de servicio"), qPrintable(r.errorDescription));

        FakeVerifactuServer::Reply badKey;
        badKey.body = FakeVerifactuServer::rejectedReply("8001", "ServiceKey no valida");
        m_server.enqueue("Create", badKey);
        r = mgr.testConnection();
        QCOMPARE(r.status, VerifactuResult::ERROR);
        QVERIFY2(r.errorDescription.contains("Clave de servicio"), qPrintable(r.errorDescription));

        FakeVerifactuServer::Reply serverError;
        serverError.status = 500;
        serverError.body = "<html>oops</html>";
        m_server.enqueue("Create", serverError);
        QCOMPARE(mgr.testConnection().status, VerifactuResult::NETWORK_ERROR);

        FakeVerifactuServer::Reply html;
        html.body = "<html>portal</html>";
        m_server.enqueue("Create", html);
        QCOMPARE(mgr.testConnection().status, VerifactuResult::NETWORK_ERROR);
        m_server.clear();

        // Tripwire: under LAIDEAL_TEST=1 a request to any host but this machine is refused
        // before it leaves (the host never resolves anyway).
        const QByteArray previous = qgetenv("LAIDEAL_TEST");
        qputenv("LAIDEAL_TEST", "1");
        VerifactuConfig::setEndpointOverride("http://laideal-tripwire.invalid/Invoices");
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression("TestHostGuard.*laideal-tripwire.invalid"));
        QCOMPARE(mgr.testConnection().status, VerifactuResult::NETWORK_ERROR);
        VerifactuConfig::setEndpointOverride(m_server.baseUrl());
        qputenv("LAIDEAL_TEST", previous);
        QVERIFY(m_server.requests().isEmpty());
    }

    void init()
    {
        exec("DELETE FROM ingresos");
        exec("DELETE FROM gastos");
        TestSchema::resetClosures(m_db);
        m_server.clear();
        m_popups->clear();
    }

    // Happy path: Cobrar -> AEAT accepts -> the rows carry the CSV, QR link and Huella.
    void test_payment_acceptedByAeat()
    {
        seedGarment("100", "h100a", "10.00");
        seedGarment("100", "h100b", "15.00");
        QVERIFY(payThroughDialog("100", QDate(2026, 2, 10)));

        const auto creates = m_server.requestsTo("Create");
        QCOMPARE(creates.size(), 1);
        QCOMPARE(creates[0].json.value("InvoiceID").toString(), QStringLiteral("100"));   // first payment: bare n_recibo
        QCOMPARE(creates[0].json.value("ServiceKey").toString(), QStringLiteral("e2e-service-key"));
        QCOMPARE(scalar("SELECT COUNT(*) FROM ingresos WHERE n_recibo='100' AND pagado='SI' "
                        "AND verifactu_estado='ENVIADA'"), QStringLiteral("2"));
        QCOMPARE(scalar("SELECT verifactu_csv FROM ingresos WHERE hash='h100a'"), QStringLiteral("A-FAKE0001"));
        QCOMPARE(scalar("SELECT verifactu_hash FROM ingresos WHERE hash='h100a'"), QStringLiteral("ABCDEF0123456789"));
        QCOMPARE(scalar("SELECT fecha_pago FROM ingresos WHERE hash='h100b'"), QStringLiteral("10-02-2026"));
        QVERIFY(m_popups->messages().isEmpty());
    }

    // Two payment events on one ticket: each is its own AEAT invoice (seq 0, seq 1).
    void test_partialPayments_eachEventIsItsOwnInvoice()
    {
        seedGarment("200", "h200a", "10.00");
        seedGarment("200", "h200b", "20.00");
        QVERIFY(payThroughDialog("200", QDate(2026, 2, 10), {"h200b"}));
        QCOMPARE(scalar("SELECT pagado FROM ingresos WHERE hash='h200b'"), QStringLiteral("NO"));
        QVERIFY(payThroughDialog("200", QDate(2026, 2, 12)));

        const auto creates = m_server.requestsTo("Create");
        QCOMPARE(creates.size(), 2);
        QCOMPARE(creates[0].json.value("InvoiceID").toString(), QStringLiteral("200"));
        QCOMPARE(creates[1].json.value("InvoiceID").toString(), QStringLiteral("200-1"));
        QCOMPARE(scalar("SELECT verifactu_invoice_seq FROM ingresos WHERE hash='h200b'"), QStringLiteral("1"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h200b'"), QStringLiteral("ENVIADA"));
    }

    // AEAT is slow: after PayDialog's 5 s bound the payment is kept PENDIENTE (never
    // lost, never marked failed). The retry is answered "already exists"; querying
    // AEAT returns the accepted record and its CSV is adopted -> ENVIADA.
    void test_noReplyInTime_staysPendiente_thenReconciledFromAeat()
    {
        seedGarment("300", "h300a", "25.00");
        FakeVerifactuServer::Reply slow;
        slow.delayMs = 7000;
        m_server.enqueue("Create", slow);
        QVERIFY(payThroughDialog("300", QDate(2026, 2, 10)));
        QCOMPARE(scalar("SELECT pagado FROM ingresos WHERE hash='h300a'"), QStringLiteral("SI"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h300a'"), QStringLiteral("PENDIENTE"));

        // Retry from the recovery list: AEAT already has it.
        FakeVerifactuServer::Reply dup;
        dup.body = FakeVerifactuServer::rejectedReply("3000", "Registro de factura duplicado: ya existe");
        m_server.enqueue("Create", dup);
        const PendingVerifactuEvent ev = verifactuEventFor(m_db, "300", 0);
        QCOMPARE(ev.nRecibo, QStringLiteral("300"));
        QSignalSpy submitted(m_verifactu, &VerifactuIntegration::requestFinished);
        const QString retryId = m_verifactu->submitSimplifiedInvoiceAsync(
            "300", QDate::fromString(ev.fechaPago, "dd-MM-yyyy"), ev.importe / 1.21, 21.0, "Servicios de lavanderia");
        VerifactuResult retry;
        QTRY_VERIFY_WITH_TIMEOUT([&]() {
            for (const QList<QVariant> &args : submitted)
                if (args.at(0).toString() == retryId) { retry = args.at(1).value<VerifactuResult>(); return true; }
            return false;
        }(), 15000);
        QVERIFY(!retry.isSuccess());
        QVERIFY(verifactuErrorIsDuplicate(retry.errorCode, retry.errorDescription));

        // "Consultar en AEAT": the accepted record comes back and matches the local row.
        FakeVerifactuServer::Reply found;
        found.body = FakeVerifactuServer::queryReply("300", "2026-02-10", 25.0, "A-LATE0001");
        m_server.enqueue("GetFilteredList", found);
        QSignalSpy queried(m_verifactu, &VerifactuIntegration::queryFinished);
        m_verifactu->queryInvoiceAsync("300");
        QTRY_COMPARE_WITH_TIMEOUT(queried.count(), 1, 15000);
        const VerifactuRemoteRecord rec = queried.at(0).at(1).value<VerifactuRemoteRecord>();
        QVERIFY(rec.hasUsableCsv());
        QVERIFY(verifactuRemoteMatches(rec, "300", "10-02-2026", 25.0));
        QCOMPARE(reconcileVerifactuFromAeat(m_db, "300", 0, rec.csv, rec.validationUrl), 1);
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h300a'"), QStringLiteral("ENVIADA"));
        QCOMPARE(scalar("SELECT verifactu_csv FROM ingresos WHERE hash='h300a'"), QStringLiteral("A-LATE0001"));
    }

    // A definitive AEAT rejection is recorded as ERROR with its reason; the sale stays paid.
    void test_aeatRejection_marksError()
    {
        seedGarment("400", "h400a", "12.00");
        FakeVerifactuServer::Reply rejected;
        rejected.body = FakeVerifactuServer::rejectedReply("4102", "NIF del emisor no identificado");
        m_server.enqueue("Create", rejected);
        QVERIFY(payThroughDialog("400", QDate(2026, 2, 10)));
        QCOMPARE(scalar("SELECT pagado FROM ingresos WHERE hash='h400a'"), QStringLiteral("SI"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h400a'"), QStringLiteral("ERROR"));
        QVERIFY(scalar("SELECT verifactu_error FROM ingresos WHERE hash='h400a'").contains("NIF del emisor"));
    }

    void test_gateway_http500_staysPendiente()
    {
        seedGarment("410", "h410a", "11.00");
        FakeVerifactuServer::Reply down;
        down.status = 500;   // with a body that would be accepted: the HTTP status alone must decide
        down.body = FakeVerifactuServer::acceptedReply("A-FAKE500");
        m_server.enqueue("Create", down);
        QVERIFY(payThroughDialog("410", QDate(2026, 2, 10)));
        expectLeftPendiente("410", "h410a");
    }

    void test_gateway_http401_staysPendiente()
    {
        seedGarment("411", "h411a", "11.00");
        FakeVerifactuServer::Reply refused;
        refused.status = 401;
        refused.body = FakeVerifactuServer::acceptedReply("A-FAKE401");
        m_server.enqueue("Create", refused);
        QVERIFY(payThroughDialog("411", QDate(2026, 2, 10)));
        expectLeftPendiente("411", "h411a");
    }

    void test_gateway_malformedJson_staysPendiente()
    {
        seedGarment("412", "h412a", "11.00");
        FakeVerifactuServer::Reply garbage;
        garbage.body = "{\"ResultCode\": 0, \"Return\": {\"CSV\": ";   // truncated body
        m_server.enqueue("Create", garbage);
        QVERIFY(payThroughDialog("412", QDate(2026, 2, 10)));
        expectLeftPendiente("412", "h412a");
    }

    void test_gateway_droppedConnection_staysPendiente()
    {
        seedGarment("413", "h413a", "11.00");
        FakeVerifactuServer::Reply dropped;
        dropped.drop = true;
        m_server.enqueue("Create", dropped);
        m_server.enqueue("Create", dropped);   // Qt resends once on a connection closed before any answer
        QVERIFY(payThroughDialog("413", QDate(2026, 2, 10)));
        expectLeftPendiente("413", "h413a");
    }

    // ResultCode 0 without the CSV AEAT returns for a registered invoice is not a success.
    void test_gateway_successWithoutCsv_staysPendiente()
    {
        seedGarment("414", "h414a", "11.00");
        FakeVerifactuServer::Reply noCsv;
        noCsv.body = R"({"ResultCode":0,"Return":{}})";
        m_server.enqueue("Create", noCsv);
        QVERIFY(payThroughDialog("414", QDate(2026, 2, 10)));
        expectLeftPendiente("414", "h414a");
    }

    // A stalled gateway ends at the client's 30 s transfer timeout as a network error (the
    // caller then keeps the row pending); without the timeout the request would wait for the
    // fake's reply, sent after 36 s.
    void test_gateway_stalledReply_endsAsNetworkErrorAtTheTransferTimeout()
    {
        FakeVerifactuServer::Reply stalled;
        stalled.delayMs = 36000;
        m_server.enqueue("Create", stalled);
        QSignalSpy finished(m_verifactu, &VerifactuIntegration::requestFinished);
        QElapsedTimer clock;
        clock.start();
        const QString id = m_verifactu->submitSimplifiedInvoiceAsync("415", QDate(2026, 2, 10), 10.0, 21.0, "Servicios");
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() && finished.last().at(0).toString() == id, 45000);
        const VerifactuResult result = finished.last().at(1).value<VerifactuResult>();
        QCOMPARE(result.status, VerifactuResult::NETWORK_ERROR);
        QVERIFY2(clock.elapsed() >= 28000 && clock.elapsed() < 35000, qPrintable(QString::number(clock.elapsed())));
        QCOMPARE(verifactuEstadoForResult(result.status), VerifactuEstado::NotSubmitted);
    }

    // A payment dated in a closed quarter is refused in the dialog's result panel, and nothing is sent.
    void test_paymentIntoClosedQuarter_refusedAndNothingSent()
    {
        exec("INSERT INTO ingresos (edit_lock, n_recibo, fecha_pago, importe, pagado, hash, verifactu_estado) "
             "VALUES ('0', '499', '10-01-2026', '5.00', 'SI', 'h499', 'ENVIADA')");
        QVERIFY(closeQuarter(m_db, 1, 2026, ""));                                   // Q1 closed
        seedGarment("500", "h500a", "18.00");
        QString result;
        QVERIFY(!payThroughDialog("500", QDate(2026, 1, 20), {}, 2000, &result));
        QVERIFY2(result.contains("Trimestre bloqueado"), qPrintable(result));   // in the window
        QVERIFY(m_popups->messages().isEmpty());
        QVERIFY(m_server.requestsTo("Create").isEmpty());
        QCOMPARE(scalar("SELECT pagado FROM ingresos WHERE hash='h500a'"), QStringLiteral("NO"));
    }

    // Paid in Q1, Q1 closed, cancelled at AEAT in Q2: Q1 keeps the income (the filed
    // report never changes), Q2 subtracts it; the unpaid garment of the same ticket
    // stays chargeable and is invoiced on its own as 600-1.
    void test_cancelAfterClose_regularisedInLaterQuarter_remainderStillChargeable()
    {
        seedGarment("600", "h600a", "50.00");
        seedGarment("600", "h600b", "30.00");
        QVERIFY(payThroughDialog("600", QDate(2026, 2, 10), {"h600b"}));
        QVERIFY(closeQuarter(m_db, 1, 2026, ""));

        QSignalSpy cancelled(m_verifactu, &VerifactuIntegration::requestFinished);
        const QString cancelId = m_verifactu->cancelInvoiceAsync("600", QDate(2026, 2, 10));
        QTRY_VERIFY_WITH_TIMEOUT(!cancelled.isEmpty() && cancelled.last().at(0).toString() == cancelId, 15000);
        QVERIFY(cancelled.last().at(1).value<VerifactuResult>().isSuccess());
        QCOMPARE(m_server.requestsTo("Cancel").size(), 1);
        QCOMPARE(m_server.requestsTo("Cancel")[0].json.value("InvoiceID").toString(), QStringLiteral("600"));
        QVERIFY(markInvoiceSeqCancelled(m_db, "600", 0, QDate(2026, 5, 15)));

        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h600a'"), QStringLiteral("ANULADA"));
        QCOMPARE(scalar("SELECT fecha_anulacion FROM ingresos WHERE hash='h600a'"), QStringLiteral("15-05-2026"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h600b'"), QStringLiteral("SIN COBRAR"));

        const QDate q1(2026, 1, 1), q2(2026, 4, 1), q3(2026, 7, 1);
        const auto fq1 = Contabilidad::figuresFromDetails(incomeTicketsBetweenDates(m_db, q1, q2),
                                                          regularizationsBetweenDates(m_db, q1, q2), {}, 21.0, q1, q2);
        QVERIFY(qAbs(fq1.ingImporte - 50.0) < 0.01);
        QCOMPARE(fq1.ingTickets, 1);

        // The remainder is charged in Q2 as its own invoice.
        QVERIFY(payThroughDialog("600", QDate(2026, 5, 20)));
        QCOMPARE(m_server.requestsTo("Create").last().json.value("InvoiceID").toString(), QStringLiteral("600-1"));
        const auto fq2 = Contabilidad::figuresFromDetails(incomeTicketsBetweenDates(m_db, q2, q3),
                                                          regularizationsBetweenDates(m_db, q2, q3), {}, 21.0, q2, q3);
        QVERIFY2(qAbs(fq2.ingImporte - (30.0 - 50.0)) < 0.01, qPrintable(QString::number(fq2.ingImporte)));
        QVERIFY(qAbs(fq2.ingRegularizacion - 50.0) < 0.01);
        QCOMPARE(fq2.ingTickets, 1);                                  // the Q2 sale counts; Q1's cancelled payment does not offset it
    }
};

QTEST_MAIN(TestE2eVerifactu)
#include "test_e2e_verifactu.moc"
