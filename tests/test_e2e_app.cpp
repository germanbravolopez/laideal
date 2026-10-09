// End-to-end scenarios at screen level (test bench, phase 2).
//
// Drives the application's own windows - MainWindow and the Anular prendas /
// Anular factura / Rectificar / Envíos pendientes dialogs, plus the Contabilidad
// form - the way an operator would: fill the widgets, press the buttons (or invoke
// the slot a button is wired to), then assert on the database, on what reached
// the fake AEAT and on the pop-ups shown. Same safety rules as phase 1: every
// Verifactu call goes to FakeVerifactuServer on 127.0.0.1, the database is a
// throwaway file and the settings a throwaway JSON (see tests/support/e2efixture.h).

#include <QtTest>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QTemporaryDir>

#include "appsettings.h"
#include "cancelinvoicedialog.h"
#include "contabilidad.h"
#include "e2efixture.h"
#include "fakeverifactuserver.h"
#include "imprimir.h"
#include "mainwindow.h"
#include "modalautocloser.h"
#include "pendingsubmitsdialog.h"
#include "rectifyinvoicedialog.h"
#include "sql_lite.h"
#include "verifactuconfig.h"
#include "verifactuintegration.h"
#include "voidgarmentsdialog.h"

class TestE2eApp : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QSqlDatabase m_db;
    FakeVerifactuServer m_server;
    VerifactuIntegration *m_verifactu = nullptr;
    ModalAutoCloser *m_popups = nullptr;

    QString scalar(const QString &sql) { return E2e::scalar(m_db, sql); }
    static QString today() { return QDate::currentDate().toString("dd-MM-yyyy"); }

    // Types one garment into row 0 of the ticket table as the operator does: the
    // quantity first, then the garment name, which looks up the price.
    static void enterGarment(MainWindow &mw, const QString &garment, const QString &quantity)
    {
        auto *table = mw.findChild<QTableWidget *>("table_ticket");
        table->setCurrentCell(0, 1);
        table->setItem(0, 0, new QTableWidgetItem(quantity));
        // From the cell, not by name: a form reset leaves the previous ticket's
        // "cb_prenda_0" alive until its deferred deletion.
        qobject_cast<QComboBox *>(table->cellWidget(0, 1))->setCurrentText(garment);
    }

    static void clickSave(MainWindow &mw)
    {
        mw.findChild<QDialogButtonBox *>("bb_save_reset")->button(QDialogButtonBox::Save)->click();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(m_server.start());
        VerifactuConfig::setEndpointOverride(m_server.baseUrl());
        // Safety net: refuse to run unless every Verifactu endpoint is the local fake.
        QVERIFY2(VerifactuConfig().getEndpointUrl().startsWith(QLatin1String("http://127.0.0.1:")),
                 "Verifactu endpoint is not the local fake server - aborting before any request");

        QVERIFY(E2e::configureSettings(QDir(m_dir.path())));
        AppSettings::instance()->setVerifactuPendingRecoveryFloorDate("2000-01-01");
        Contabilidad::setOpenGeneratedReports(false);

        // MainWindow opens DB_PATH as the default connection; the test reads the same file.
        const QString dbFile = m_dir.filePath("laideal_e2e_app.db");
        setDbPath(dbFile);
        m_db = QSqlDatabase::addDatabase("QSQLITE", "e2e_app");
        m_db.setDatabaseName(dbFile);
        QVERIFY(E2e::createSchema(m_db));
        QVERIFY(E2e::exec(m_db, "INSERT INTO prendas VALUES ('Camisa', '3.50', '2.00')"));

        m_verifactu = new VerifactuIntegration(this);
        QVERIFY2(m_verifactu->initialize(), "Verifactu client must initialise from the test settings");
        m_popups = new ModalAutoCloser(this);
    }

    void cleanupTestCase()
    {
        VerifactuConfig::setEndpointOverride(QString());
    }

    void init()
    {
        E2e::clearTables(m_db);
        AppSettings::instance()->setVerifactuPendingRecoveryEnabled(false);
        m_server.clear();
        m_popups->clear();
    }

    // MainWindow: an unpaid ticket is stored SIN COBRAR and nothing is sent; a paid
    // one is submitted at save time and ends ENVIADA with the CSV of the reply.
    void test_mainWindow_saveUnpaidThenPaidTicket()
    {
        MainWindow mw;
        auto *ticketNum = mw.findChild<QLineEdit *>("le_nr_ticket");
        const QString first = ticketNum->text();

        mw.findChild<QComboBox *>("cb_client")->setCurrentText("Cliente Pantalla");
        enterGarment(mw, "Camisa", "2");
        QCOMPARE(mw.findChild<QLineEdit *>("le_cost_total")->text(), QStringLiteral("7.00"));
        clickSave(mw);

        QCOMPARE(scalar(QStringLiteral("SELECT COUNT(*) FROM ingresos WHERE n_recibo='%1'").arg(first)),
                 QStringLiteral("1"));
        QCOMPARE(scalar(QStringLiteral("SELECT verifactu_estado FROM ingresos WHERE n_recibo='%1'").arg(first)),
                 QStringLiteral("SIN COBRAR"));
        QCOMPARE(scalar(QStringLiteral("SELECT importe FROM ingresos WHERE n_recibo='%1'").arg(first)),
                 QStringLiteral("7.00"));
        QCOMPARE(scalar("SELECT COUNT(*) FROM clientes WHERE nombre='Cliente Pantalla'"), QStringLiteral("1"));
        QVERIFY(m_server.requestsTo("Create").isEmpty());

        // The form was reset for the next ticket.
        const QString second = ticketNum->text();
        QCOMPARE(second.toInt(), first.toInt() + 1);

        mw.findChild<QComboBox *>("cb_client")->setCurrentText("Cliente Pantalla");
        enterGarment(mw, "Camisa", "1");
        mw.findChild<QPushButton *>("pb_payment")->setChecked(true);
        clickSave(mw);

        const auto creates = m_server.requestsTo("Create");
        QCOMPARE(creates.size(), 1);
        QCOMPARE(creates[0].json.value("InvoiceID").toString(), second);
        QCOMPARE(scalar(QStringLiteral("SELECT pagado || '|' || fecha_pago || '|' || verifactu_estado || '|' "
                                       "|| verifactu_csv FROM ingresos WHERE n_recibo='%1'").arg(second)),
                 QStringLiteral("SI|%1|ENVIADA|A-FAKE0001").arg(today()));
        QVERIFY2(m_popups->messages().isEmpty(), qPrintable(m_popups->messages().join(" / ")));
    }

    // MainWindow: Save without a client is refused with a message and stores nothing.
    void test_mainWindow_saveWithoutClient_refused()
    {
        MainWindow mw;
        enterGarment(mw, "Camisa", "1");
        clickSave(mw);
        QTRY_VERIFY(m_popups->sawMessageContaining("No se ha introducido ningún cliente"));
        QCOMPARE(scalar("SELECT COUNT(*) FROM ingresos"), QStringLiteral("0"));
    }

    // Anular prendas: the ticked unpaid garment is voided locally (no AEAT request),
    // dated in fecha_anulacion with empty payment / pick-up dates; the other stays.
    void test_voidGarments_voidsOnlyTheTickedGarment()
    {
        QVERIFY(E2e::seedGarment(m_db, "900", "h900a", "10.00"));
        QVERIFY(E2e::seedGarment(m_db, "900", "h900b", "5.00"));

        VoidGarmentsDialog dlg(m_db);
        dlg.findChild<QLineEdit *>("leTicketNum")->setText("900");
        QMetaObject::invokeMethod(&dlg, "onSearchClicked");
        auto *table = dlg.findChild<QTableWidget *>("table");
        QCOMPARE(table->rowCount(), 2);
        table->item(0, 0)->setCheckState(Qt::Checked);
        QMetaObject::invokeMethod(&dlg, "onVoidSelectedClicked");     // confirmation answered Yes

        QVERIFY(m_popups->sawMessageContaining("Anular prendas"));
        QCOMPARE(scalar("SELECT estado || '|' || pagado || '|' || verifactu_estado || '|' || fecha_anulacion "
                        "|| '|' || fecha_pago || '|' || fecha_recogida FROM ingresos WHERE hash='h900a'"),
                 QStringLiteral("Anulado|NO|ANULADA|%1||").arg(today()));
        QCOMPARE(scalar("SELECT estado || '|' || verifactu_estado FROM ingresos WHERE hash='h900b'"),
                 QStringLiteral("En tienda|SIN COBRAR"));
        QVERIFY(m_server.requests().isEmpty());
        // Reloaded: the voided garment can no longer be ticked.
        QCOMPARE(table->item(0, 0)->flags() & Qt::ItemIsUserCheckable, Qt::ItemFlags());
    }

    // Anular factura: AEAT accepts the cancellation -> ANULADA with today's date.
    void test_cancelInvoice_markedAnulada()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "800", "h800a", "12.00", today(), "A-ORIG0800"));

        CancelInvoiceDialog dlg(m_db);
        dlg.m_verifactu = m_verifactu;
        dlg.findChild<QLineEdit *>("leTicketNum")->setText("800");
        QMetaObject::invokeMethod(&dlg, "onSearchClicked");
        QCOMPARE(dlg.findChild<QTableWidget *>("table")->rowCount(), 1);
        QMetaObject::invokeMethod(&dlg, "onCancelClicked", Q_ARG(int, 0));

        auto *result = dlg.findChild<QLabel *>("lblResult");
        QTRY_VERIFY_WITH_TIMEOUT(result->text().contains("Anulación confirmada"), 10000);
        const auto cancels = m_server.requestsTo("Cancel");
        QCOMPARE(cancels.size(), 1);
        QCOMPARE(cancels[0].json.value("InvoiceID").toString(), QStringLiteral("800"));
        QCOMPARE(scalar("SELECT verifactu_estado || '|' || fecha_anulacion FROM ingresos WHERE hash='h800a'"),
                 QStringLiteral("ANULADA|%1").arg(today()));
    }

    // Anular factura while today's quarter is closed: refused before any request.
    void test_cancelInvoice_refusedWhileCurrentQuarterClosed()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "810", "h810a", "12.00", today(), "A-ORIG0810"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET edit_lock = 1"));

        CancelInvoiceDialog dlg(m_db);
        dlg.m_verifactu = m_verifactu;
        dlg.findChild<QLineEdit *>("leTicketNum")->setText("810");
        QMetaObject::invokeMethod(&dlg, "onSearchClicked");
        QMetaObject::invokeMethod(&dlg, "onCancelClicked", Q_ARG(int, 0));

        QVERIFY(dlg.findChild<QLabel *>("lblResult")->text().contains("trimestre actual"));
        QTest::qWait(300);
        QVERIFY(m_server.requestsTo("Cancel").isEmpty());
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h810a'"), QStringLiteral("ENVIADA"));
    }

    // Rectificar por sustitución: a date before the original payment, or in a closed
    // quarter, is refused; with a valid date the rectificativa is a new ENVIADA ticket and the original
    // becomes RECTIFICADA.
    void test_rectifySubstitution_dateRuleThenRectified()
    {
        const QDate paid = QDate::currentDate();
        QVERIFY(E2e::seedSentGarment(m_db, "700", "h700a", "10.00", paid.toString("dd-MM-yyyy"), "A-ORIG0700"));

        RectifyInvoiceDialog dlg(m_db);
        dlg.m_verifactu = m_verifactu;
        dlg.findChild<QLineEdit *>("leTicketNum")->setText("700");
        QMetaObject::invokeMethod(&dlg, "onSearchClicked");
        dlg.findChild<QRadioButton *>("rbSubstitution")->setChecked(true);
        dlg.findChild<QDoubleSpinBox *>("sbAmount")->setValue(8.00);
        auto *date = dlg.findChild<QDateEdit *>("deRectifyDate");
        auto *result = dlg.findChild<QLabel *>("lblResult");

        date->setDate(paid.addDays(-1));
        QMetaObject::invokeMethod(&dlg, "onRectifyClicked");
        QVERIFY(result->text().contains("no puede ser anterior"));
        QCOMPARE(scalar("SELECT COUNT(*) FROM ingresos"), QStringLiteral("1"));

        date->setDate(paid);
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET edit_lock = 1"));
        QMetaObject::invokeMethod(&dlg, "onRectifyClicked");
        QVERIFY(result->text().contains("contabilidad cerrada"));
        QCOMPARE(scalar("SELECT COUNT(*) FROM ingresos"), QStringLiteral("1"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET edit_lock = 0"));

        QMetaObject::invokeMethod(&dlg, "onRectifyClicked");
        QTRY_VERIFY_WITH_TIMEOUT(result->text().contains("Rectificativa enviada"), 10000);

        const auto creates = m_server.requestsTo("Create");
        QCOMPARE(creates.size(), 1);
        QCOMPARE(creates[0].json.value("InvoiceID").toString(), QStringLiteral("701"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h700a'"), QStringLiteral("RECTIFICADA"));
        QCOMPARE(scalar("SELECT importe || '|' || verifactu_estado || '|' || verifactu_rectifies_n_recibo "
                        "FROM ingresos WHERE n_recibo='701'"),
                 QStringLiteral("8.00|ENVIADA|700"));
    }

    // Startup recovery: a payment left PENDIENTE by a previous session is offered by
    // the Envíos pendientes dialog about 4 s after MainWindow opens; Reintentar
    // re-submits it under the same InvoiceID and the reply makes it ENVIADA.
    void test_startupRecovery_retryPendingSubmission()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "500", "h500a", "9.00", today(), ""));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_estado = 'PENDIENTE' WHERE hash='h500a'"));
        QVERIFY(E2e::seedGarment(m_db, "510", "h510a", "4.00", today()));   // unpaid: never offered
        AppSettings::instance()->setVerifactuPendingRecoveryEnabled(true);

        MainWindow mw;
        QPointer<PendingSubmitsDialog> dlg;
        QTRY_VERIFY_WITH_TIMEOUT((dlg = mw.findChild<PendingSubmitsDialog *>()) && dlg->isVisible(), 8000);
        QCOMPARE(dlg->findChild<QTableWidget *>("table")->rowCount(), 1);
        QCOMPARE(dlg->findChild<QTableWidget *>("table")->item(0, 0)->text(), QStringLiteral("500"));
        QMetaObject::invokeMethod(dlg, "onRetryClicked", Q_ARG(int, 0));

        QTRY_COMPARE_WITH_TIMEOUT(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h500a'"),
                                  QStringLiteral("ENVIADA"), 10000);
        const auto creates = m_server.requestsTo("Create");
        QCOMPARE(creates.size(), 1);
        QCOMPARE(creates[0].json.value("InvoiceID").toString(), QStringLiteral("500"));
        QTRY_VERIFY(dlg.isNull());                                        // last row handled: it closes itself
    }

    // Reprint of one payment event (Imprimir, as Recogida de prendas uses it): only
    // that event's paid garments are loaded, the QR is requested under its literal
    // InvoiceID and payment date, and no QR is requested once the event is ANULADA.
    void test_reprintPaymentEvent_scopedRowsAndQrGating()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "600", "h600a", "10.00", "10-02-2026", "A-ORIG0600"));
        QVERIFY(E2e::seedSentGarment(m_db, "600", "h600b", "4.00", "10-02-2026", "A-ORIG0600"));
        QVERIFY(E2e::seedSentGarment(m_db, "600", "h600c", "6.00", "12-02-2026", "A-ORIG0601"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_invoice_seq = 1, verifactu_invoice_id = '600-1' "
                                "WHERE hash='h600c'"));
        QVERIFY(E2e::seedGarment(m_db, "600", "h600d", "3.00"));          // unpaid, seq 0 by default

        Imprimir second(m_db);
        second.verifactuIntegration = m_verifactu;
        second.invoiceSeq = 1;
        second.le_n_ticket->setText("600");
        second.getTicketInfo();
        QCOMPARE(second.sqlQueryModel->rowCount(), 1);
        QVERIFY(!second.resolveQrCode().isNull());
        auto qrs = m_server.requestsTo("GetQrCode");
        QCOMPARE(qrs.size(), 1);
        QCOMPARE(qrs[0].json.value("InvoiceID").toString(), QStringLiteral("600-1"));
        QCOMPARE(qrs[0].json.value("InvoiceDate").toString(), QStringLiteral("2026-02-12"));

        Imprimir first(m_db);
        first.verifactuIntegration = m_verifactu;
        first.invoiceSeq = 0;
        first.le_n_ticket->setText("600");
        first.getTicketInfo();
        QCOMPARE(first.sqlQueryModel->rowCount(), 2);                    // the unpaid garment is not on it

        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_estado = 'ANULADA' "
                                "WHERE n_recibo='600' AND verifactu_invoice_seq = 0 AND pagado='SI'"));
        first.getTicketInfo();
        QVERIFY(first.resolveQrCode().isNull());
        QCOMPARE(m_server.requestsTo("GetQrCode").size(), 1);              // no second request
    }

    // Contabilidad trimestral: generating with "bloquear" writes the PDF and locks
    // the quarter's rows; Revertir contabilidad unlocks them again.
    void test_contabilidad_generateLockThenRevert()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "400", "h400a", "12.10", "10-02-2026", "A-ORIG0400"));
        const QString pdf = AppSettings::instance()->contabilidadPath()
                            + "/contabilidad_trimestral_2026_1.pdf";
        QFile::remove(pdf);

        QPointer<Contabilidad> form = new Contabilidad(m_db);
        form->findChild<QComboBox *>("cb_config")->setCurrentIndex(1);   // Trimestral
        form->findChild<QSpinBox *>("sb_trim")->setValue(1);
        form->findChild<QSpinBox *>("sb_year")->setValue(2026);
        form->findChild<QCheckBox *>("checkBox_lock")->setChecked(true);
        QMetaObject::invokeMethod(form, "on_bb_ok_cancel_accepted");

        QVERIFY(QFile::exists(pdf));
        QVERIFY(QFileInfo(pdf).size() > 0);
        QVERIFY(m_popups->sawMessageContaining("se ha bloqueado"));
        QCOMPARE(scalar("SELECT edit_lock FROM ingresos WHERE hash='h400a'"), QStringLiteral("1"));
        QTRY_VERIFY(form.isNull());                                       // closed (WA_DeleteOnClose)

        QPointer<Contabilidad> revert = new Contabilidad(m_db);
        revert->revertirOn = true;
        revert->resetAllContents();
        revert->findChild<QSpinBox *>("sb_trim")->setValue(1);
        revert->findChild<QSpinBox *>("sb_year")->setValue(2026);
        QMetaObject::invokeMethod(revert, "on_bb_ok_cancel_accepted");

        QVERIFY(m_popups->sawMessageContaining("se ha revertido"));
        QCOMPARE(scalar("SELECT edit_lock FROM ingresos WHERE hash='h400a'"), QStringLiteral("0"));
        QTRY_VERIFY(revert.isNull());
    }
};

QTEST_MAIN(TestE2eApp)
#include "test_e2e_app.moc"
