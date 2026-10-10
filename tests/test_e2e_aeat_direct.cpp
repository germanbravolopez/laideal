// End to end over the direct AEAT connection: the app's VerifactuIntegration chosen
// "Directa con la AEAT" in the settings, the real Cobrar window (PayDialog), and the
// fake AEAT SOAP service (support/fakeaeatserver) instead of AEAT - never the real one.

#include <QtTest>
#include <QDateEdit>
#include <QLabel>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>

#include "aeatdirectbackend.h"
#include "appsettings.h"
#include "fakeaeatserver.h"
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

    // Charges through the real PayDialog; garments in `skip` are left unticked.
    bool pay(const QString &nRecibo, const QDate &date, const QStringList &skip = {})
    {
        PayDialog dlg(m_db);
        dlg.m_verifactu = m_verifactu;
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

private slots:
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
