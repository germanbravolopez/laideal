// End to end over the direct AEAT connection: the app's VerifactuIntegration chosen
// "Directa con la AEAT" in the settings, the real Cobrar window (PayDialog), and the
// fake AEAT SOAP service (support/fakeaeatserver) instead of AEAT - never the real one.

#include <QtTest>
#include <QDateEdit>
#include <QLabel>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QScopeGuard>
#include <QTableWidget>
#include <QTemporaryDir>

#include <memory>

#include "aeatdirectbackend.h"
#include "aeatstore.h"
#include "appsettings.h"
#include "fakeaeatserver.h"
#include "imprimir.h"
#include "modalautocloser.h"
#include "pay_dialog.h"
#include "sql_lite.h"
#include "testschema.h"
#include "verifactuintegration.h"

class TestE2eAeatDirect : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir         m_dir;
    QSqlDatabase          m_db;
    FakeAeatServer        m_server;
    VerifactuIntegration *m_verifactu = nullptr;
    ModalAutoCloser      *m_popups = nullptr;

    void exec(const QString &sql)
    {
        QVERIFY(m_db.open());
        QSqlQuery q(m_db);
        QVERIFY2(q.exec(sql), qPrintable(q.lastError().text()));
        m_db.close();
    }

    QString scalar(const QString &sql)
    {
        m_db.open();
        QSqlQuery q(m_db);
        QString out;
        if (q.exec(sql) && q.next())
            out = q.value(0).toString();
        m_db.close();
        return out;
    }

    void seedGarment(const QString &nRecibo, const QString &hash, const QString &importe)
    {
        exec(QStringLiteral("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, "
                            "pagado, estado, cantidad, prenda, size, servicio, observaciones, edit_lock, hash, "
                            "verifactu_estado, verifactu_invoice_seq) VALUES ('%1', 'Cliente E2E', '05-02-2026', '', '', "
                            "'%3', 'NO', 'En tienda', '1', 'Camisa', '', 'Limp.', '', 0, '%2', 'SIN COBRAR', 0)")
                 .arg(nRecibo, hash, importe));
    }

    // Charges through the real PayDialog; garments in `skip` are left unticked. Once
    // the chain is ready the dialog prints the factura at once and hands AEAT's answer
    // over (submitAdopted): settled here as Recogida does.
    bool pay(const QString &nRecibo, const QDate &date, const QStringList &skip = {})
    {
        PayDialog dlg(m_db);
        dlg.m_verifactu = m_verifactu;
        connect(&dlg, &PayDialog::submitAdopted, this,
                [this](const QString &reqId, const QString &ticketNum, int seq, bool) {
            auto *conn = new QMetaObject::Connection;
            *conn = connect(m_verifactu, &VerifactuIntegration::requestFinished, this,
                            [this, reqId, ticketNum, seq, conn](const QString &id, const VerifactuResult &result) {
                if (id != reqId)
                    return;
                updateTicketVerifactuFields(m_db, ticketNum, result, seq);
                disconnect(*conn);
                delete conn;
            });
        });
        if (!dlg.loadTicket(nRecibo))
            return false;
        dlg.findChild<QDateEdit *>()->setDate(date);
        auto *table = dlg.findChild<QTableWidget *>();
        for (int r = 0; r < table->rowCount(); ++r)
            if (skip.contains(table->item(r, 7)->text()))             // COL_HASH
                table->item(r, 0)->setCheckState(Qt::Unchecked);
        QMetaObject::invokeMethod(&dlg, "onCobrarClicked");
        QElapsedTimer t;
        t.start();
        while (dlg.result() != QDialog::Accepted && t.elapsed() < 15000)
            QTest::qWait(50);
        return dlg.result() == QDialog::Accepted;
    }

    // The direct client in PRODUCCION on this database, its chain already synced as on an
    // installation that runs (nothing goes to a first sync). Null if that could not be set up.
    std::unique_ptr<VerifactuIntegration> productionClient()
    {
        AppSettings *s = AppSettings::instance();
        const QString nif = s->verifactuNif();
        AeatStore store(m_db, "produccion");
        if (!store.ensureSchema() || !store.markChainSynced(nif))
            return nullptr;
        const QString previousMarker = s->aeatChainMarker();
        s->setAeatChainMarker(QStringLiteral("produccion|%1|%2").arg(nif, store.chainHead(nif).hash));
        s->setAeatDirectProduction(true);
        auto client = std::make_unique<VerifactuIntegration>();
        const bool ready = client->initialize(m_db);
        s->setAeatDirectProduction(false);
        s->setAeatChainMarker(previousMarker);
        return ready && client->environmentName() == QLatin1String("produccion") ? std::move(client) : nullptr;
    }

private slots:
    // An accepted row without a stored verification URL (an older gateway row) is reprinted
    // without QR: the direct client never rebuilds the URL for today's environment.
    void test_reprintWithoutStoredUrlHasNoQr()
    {
        exec("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, "
             "estado, cantidad, prenda, size, servicio, observaciones, edit_lock, hash, verifactu_csv, verifactu_estado, "
             "verifactu_invoice_seq, verifactu_invoice_id) VALUES ('7300', 'Cliente E2E', '05-02-2026', '05-02-2026', '', "
             "'10.00', 'SI', 'En tienda', '1', 'Camisa', '', 'Limp.', '', 0, 'h7300', 'A-GW7300', 'ENVIADA', 0, '7300')");
        Imprimir dlg(m_db);
        dlg.verifactuIntegration = m_verifactu;
        dlg.isRecibo = false;
        dlg.le_n_ticket->setText("7300");
        dlg.getTicketInfo();
        QVERIFY(dlg.resolveQrCode().isNull());
    }

    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(m_server.start());
        AeatDirectBackend::setEndpointOverride(m_server.url());
        // Safety net: never post test invoices to AEAT.
        QVERIFY2(AeatDirectBackend::endpointUrl(false).startsWith(QLatin1String("http://127.0.0.1:")),
                 "AEAT endpoint is not the local fake server - aborting before any request");

        const QString settingsPath = m_dir.filePath("settings.json");
        QFile f(settingsPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
        f.close();
        AppSettings *s = AppSettings::instance();
        s->loadFrom(settingsPath);
        s->setVerifactuNif("B00000000");
        s->setVerifactuName("Tintoreria E2E");
        s->setVerifactuProduction(false);
        s->setVerifactuDirectAeat(true);
        s->setEnablePrinting(false);

        m_db = QSqlDatabase::addDatabase("QSQLITE", "e2e_direct");
        m_db.setDatabaseName(m_dir.filePath("laideal_direct.db"));
        QVERIFY(TestSchema::create(m_db));
        m_verifactu = new VerifactuIntegration(this);
        QVERIFY2(m_verifactu->initialize(m_db), qPrintable(m_verifactu->getLastError()));
        QVERIFY(m_verifactu->directBackend());
        m_popups = new ModalAutoCloser(this);
    }

    void cleanupTestCase()
    {
        AeatDirectBackend::setEndpointOverride(QString());
    }

    // Cobrar over the direct connection: one record for the payment event, the rows
    // ENVIADA with AEAT's CSV, the record's own huella and XML stored on them.
    void test_paymentRegistered()
    {
        seedGarment("100", "h100a", "10.00");
        seedGarment("100", "h100b", "15.00");
        QVERIFY(pay("100", QDate(2026, 2, 10)));
        const QList<FakeAeatServer::SentRecord> sent = m_server.sentRecords();
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent[0].invoiceNumber, QStringLiteral("100"));
        QCOMPARE(sent[0].issueDate, QStringLiteral("10-02-2026"));
        QCOMPARE(sent[0].total, QStringLiteral("25.00"));
        QTRY_COMPARE(scalar("SELECT COUNT(*) FROM ingresos WHERE n_recibo='100' AND verifactu_estado='ENVIADA'"),
                     QStringLiteral("2"));
        QVERIFY(scalar("SELECT verifactu_csv FROM ingresos WHERE hash='h100a'").startsWith("A-FAKE"));
        QCOMPARE(scalar("SELECT verifactu_hash FROM ingresos WHERE hash='h100a'"), sent[0].hash);
        QVERIFY(scalar("SELECT verifactu_xml FROM ingresos WHERE hash='h100b'").contains("<sf:RegistroAlta"));
        QVERIFY(scalar("SELECT verifactu_url_qr FROM ingresos WHERE hash='h100a'").contains("prewww2.aeat.es"));
    }

    // A second payment event is its own invoice (100-1), chained after the first record.
    void test_partialPaymentChained()
    {
        seedGarment("200", "h200a", "10.00");
        seedGarment("200", "h200b", "5.00");
        QVERIFY(pay("200", QDate(2026, 2, 11), { "h200b" }));
        QVERIFY(pay("200", QDate(2026, 2, 12)));
        // Never last() of an empty list: on a slow runner nothing may have been sent yet.
        QTRY_VERIFY(!m_server.sentRecords().isEmpty()
                    && m_server.sentRecords().last().invoiceNumber == QStringLiteral("200-1"));
        const QList<FakeAeatServer::SentRecord> sent = m_server.sentRecords();
        QCOMPARE(sent.last().invoiceNumber, QStringLiteral("200-1"));
        QCOMPARE(sent.last().total, QStringLiteral("5.00"));
        QCOMPARE(sent.last().previousHash, sent.at(sent.size() - 2).hash);
        QTRY_COMPARE(scalar("SELECT COUNT(*) FROM ingresos WHERE n_recibo='200' AND verifactu_estado='ENVIADA'"),
                     QStringLiteral("2"));
    }

    // The gateway's PRODUCCIÓN box never sends the direct client to production: only
    // its own explicit setting does.
    void test_productionOnlyOnPurpose()
    {
        AppSettings *s = AppSettings::instance();
        s->setVerifactuProduction(true);
        VerifactuIntegration gatewayProduction;
        QVERIFY(gatewayProduction.initialize(m_db));
        QVERIFY2(gatewayProduction.directBackend()->configurationInfo().contains("PRUEBAS"),
                 qPrintable(gatewayProduction.directBackend()->configurationInfo()));
        s->setAeatDirectProduction(true);
        VerifactuIntegration explicitProduction;
        explicitProduction.initialize(m_db);
        QVERIFY(explicitProduction.directBackend()->configurationInfo().contains("PRODUCCION"));
        s->setAeatDirectProduction(false);
        s->setVerifactuProduction(false);
    }

    // A ticket dated on the day production starts is production's: a retry on the switch
    // day goes to the real AEAT; only an earlier date is refused as before production.
    void test_switchDayTicketIsNotBeforeProduction()
    {
        const auto client = productionClient();
        QVERIFY(client);
        QCOMPARE(client->productionStartDate(), QDate::currentDate());
        QVERIFY(client->submitSimplifiedInvoiceAsync("9101", QDate::currentDate().addDays(-1), 10.0, 21.0).isEmpty());
        QVERIFY2(client->getLastError().contains("anterior al paso a PRODUCCI"), qPrintable(client->getLastError()));
        QVERIFY(client->dateBeforeProductionReason(QDate::currentDate()).isEmpty());
        QVERIFY(!client->submitSimplifiedInvoiceAsync("9101", QDate::currentDate(), 10.0, 21.0).isEmpty());
    }

    // The gateway registered the invoice itself at the real AEAT when it was in PRODUCTION:
    // its stored QR points there, so it is not one the real AEAT never held, and Anular
    // cancels it at AEAT through the direct client.
    void test_invoiceTheGatewayRegisteredInProductionIsCancelledAtAeat()
    {
        const auto client = productionClient();
        QVERIFY(client);
        const QDate before = QDate::currentDate().addDays(-30);
        const QString testUrl = "https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR?nif=B00000000&numserie=8201";
        const QString realUrl = "https://www2.agenciatributaria.gob.es/wlpl/TIKE-CONT/ValidarQR?nif=B00000000&numserie=8201";
        QVERIFY(client->invoicePredatesProduction("8201", before));
        QVERIFY(client->invoicePredatesProduction("8201", before, QString()));
        QVERIFY(client->invoicePredatesProduction("8201", before, testUrl));
        QVERIFY(!client->invoicePredatesProduction("8201", before, realUrl));
        QVERIFY(!client->invoicePredatesProduction("8201", before, "https://www2.aeat.es/wlpl/TIKE-CONT/ValidarQR?x=1"));

        QVERIFY(client->cancelInvoiceAsync("8201", before, testUrl).isEmpty());
        QVERIFY2(client->getLastError().contains("anterior al paso a PRODUCCI"), qPrintable(client->getLastError()));
        QVERIFY(client->cancelInvoiceAsync("8201", before).isEmpty());
        const int sentBefore = m_server.sentRecords().size();
        const QString requestId = client->cancelInvoiceAsync("8201", before, realUrl);
        QVERIFY2(!requestId.isEmpty(), qPrintable(client->getLastError()));
        QTRY_VERIFY_WITH_TIMEOUT(m_server.sentRecords().size() > sentBefore, 20000);
        const FakeAeatServer::SentRecord sent = m_server.sentRecords().last();
        QCOMPARE(sent.operation, QStringLiteral("Anulacion"));
        QCOMPARE(sent.invoiceNumber, QStringLiteral("8201"));
    }

    // Production whose start day cannot be read (its table is gone): nothing dated before
    // today passes, with a reason that says so; today's date still does.
    void test_productionWithUnreadableStartDayRefusesEarlierDates()
    {
        const auto client = productionClient();
        QVERIFY(client);
        exec("DROP TABLE aeat_environment_start");
        const auto restore = qScopeGuard([this] {
            AeatStore store(m_db, "produccion");
            QVERIFY(store.ensureSchema());
            QVERIFY(store.markStarted());
        });
        QVERIFY(!client->productionStartDate().isValid());
        const QDate yesterday = QDate::currentDate().addDays(-1);
        QVERIFY(client->submitSimplifiedInvoiceAsync("9301", yesterday, 10.0, 21.0).isEmpty());
        QVERIFY2(client->getLastError().contains("No se pudo leer el d"), qPrintable(client->getLastError()));
        QVERIFY2(client->getLastError().contains("PRODUCCI"), qPrintable(client->getLastError()));
        QVERIFY(client->submitRectificationAsync("R-9301", yesterday, VerifactuInvoice::RECTIFICATION_R5,
                                                 VerifactuInvoice::BY_DIFFERENCES, -1.0, -0.21, 10.0, 2.10, 21.0,
                                                 "x", "9301", yesterday).isEmpty());
        QVERIFY2(client->getLastError().contains("No se pudo leer el d"), qPrintable(client->getLastError()));
        const QString reason = client->dateBeforeProductionReason(yesterday);
        QVERIFY2(reason.contains("No se pudo leer el d") && reason.contains("PRODUCCI"), qPrintable(reason));
        QVERIFY(client->dateBeforeProductionReason(QDate::currentDate()).isEmpty());
        QVERIFY(!client->submitSimplifiedInvoiceAsync("9302", QDate::currentDate(), 10.0, 21.0).isEmpty());
    }

    // AEAT refuses the record: the rows are ERROR with AEAT's code.
    void test_rejection()
    {
        seedGarment("300", "h300a", "10.00");
        m_server.rejectNext("300", "1100", "Valor o tipo incorrecto del campo: ImporteTotal");
        QVERIFY(pay("300", QDate(2026, 2, 13)));
        QTRY_COMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h300a'"), QStringLiteral("ERROR"));
        QVERIFY(scalar("SELECT verifactu_error FROM ingresos WHERE hash='h300a'").contains("ImporteTotal"));
    }
};

QTEST_MAIN(TestE2eAeatDirect)
#include "test_e2e_aeat_direct.moc"
