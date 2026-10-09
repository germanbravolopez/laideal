// End-to-end scenarios at screen level (test bench, phase 2).
//
// Drives the application's own windows - MainWindow and the Anular prendas /
// Anular factura / Rectificar / Envíos pendientes dialogs, plus the Contabilidad
// form, and Recogida de prendas with its Verifactu / AEAT comparison dialogs - the
// way an operator would: fill the widgets, press the buttons (or invoke
// the slot a button is wired to), then assert on the database, on what reached
// the fake AEAT and on the pop-ups shown. Same safety rules as phase 1: every
// Verifactu call goes to FakeVerifactuServer on 127.0.0.1, the database is a
// throwaway file and the settings a throwaway JSON (see tests/support/e2efixture.h).

#include <QtTest>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QStatusBar>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QBuffer>
#include <QXmlStreamReader>

#include "aeatexport.h"
#include "appsettings.h"
#include "cancelinvoicedialog.h"
#include "contabilidad.h"
#include "e2efixture.h"
#include "add_garment.h"
#include "fakeverifactuserver.h"
#include "imprimir.h"
#include "mainwindow.h"
#include "modalautocloser.h"
#include "modaldriver.h"
#include "pay_dialog.h"
#include "recog_prendas.h"
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
    ModalDriver *m_driver = nullptr;

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

    // Recogida: searches the ticket by number and selects the garment `hash`, as a
    // click on its row in the table does.
    static bool selectRow(RecogPrendas &rp, const QString &ticketNum, const QString &hash)
    {
        rp.findChild<QLineEdit *>("le_search")->setText(ticketNum);
        QMetaObject::invokeMethod(&rp, "on_pb_search_clicked");
        QAbstractItemModel *view = rp.findChild<QTableView *>("tableView")->model();
        for (int r = 0; r < view->rowCount(); ++r) {
            const QModelIndex index = view->index(r, 0);
            const int sourceRow = rp.proxyModel->mapToSource(index).row();
            if (rp.sqlQueryModel->data(rp.sqlQueryModel->index(sourceRow, INGRESOS_COL_HASH)).toString() == hash) {
                QMetaObject::invokeMethod(&rp, "on_tableView_clicked", Q_ARG(QModelIndex, index));
                return true;
            }
        }
        return false;
    }

    // What the Verifactu dialog and the AEAT comparison dialog offered.
    struct AeatQuery {
        bool hadRetry = false, hadQuery = false;
        bool compared = false;               // the comparison dialog was shown
        QString summary, aeatCsv, applyTip;
        bool applyEnabled = false;
    };

    static void recordComparison(QWidget *w, AeatQuery &q)
    {
        q.compared = true;
        q.summary = w->findChild<QLabel *>("lblSummary")->text();
        q.aeatCsv = w->findChild<QTableWidget *>("tableCompare")->item(3, 1)->text();
        auto *apply = w->findChild<QPushButton *>("btnApply");
        q.applyEnabled = apply->isEnabled();
        q.applyTip = apply->toolTip();
    }

    // Recogida -> Verifactu button on the selected row. With `query`, presses
    // "Consultar en AEAT" and records the comparison dialog, closing it without
    // adopting; otherwise just records which buttons were offered.
    AeatQuery openVerifactuDialog(RecogPrendas &rp, bool query)
    {
        AeatQuery q;
        m_driver->expect(ModalDriver::named("verifactuDialog"), [&q, query](QWidget *w) {
            q.hadRetry = w->findChild<QPushButton *>("btnRetry") != nullptr;
            auto *btnQuery = w->findChild<QPushButton *>("btnQuery");
            q.hadQuery = btnQuery != nullptr;
            if (query && btnQuery)
                btnQuery->click();
            else
                qobject_cast<QDialog *>(w)->reject();
        });
        if (query)
            m_driver->expect(ModalDriver::named("aeatReconcileDialog"), [&q](QWidget *w) {
                recordComparison(w, q);
                qobject_cast<QDialog *>(w)->reject();
            });
        rp.findChild<QPushButton *>("pb_verifactu")->click();
        if (query && q.hadQuery) {
            QElapsedTimer t;
            t.start();
            while (!q.compared && t.elapsed() < 10000)
                QTest::qWait(30);
        }
        m_driver->clear();
        return q;
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
        m_driver = new ModalDriver(this);
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
        m_driver->clear();
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

    // Anular factura: AEAT accepts the cancellation -> ANULADA with today's date. The
    // cancellation names each invoice by the date it was issued under - its payment
    // date - not the ticket's reception date: here the first payment was made days
    // after reception and a second garment was paid later as its own invoice "850-1".
    void test_cancelInvoice_markedAnulada()
    {
        const QDate received = QDate::currentDate().addDays(-20);
        const QDate firstPaid = QDate::currentDate().addDays(-10);
        QVERIFY(E2e::seedSentGarment(m_db, "800", "h800a", "12.00", firstPaid.toString("dd-MM-yyyy"), "A-ORIG0800"));
        QVERIFY(E2e::seedSentGarment(m_db, "800", "h800b", "6.00", today(), "A-ORIG0801"));
        QVERIFY(E2e::exec(m_db, QStringLiteral("UPDATE ingresos SET fecha_recepcion = '%1' WHERE n_recibo = '800'")
                                    .arg(received.toString("dd-MM-yyyy"))));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_invoice_seq = 1, verifactu_invoice_id = '800-1' "
                                "WHERE hash = 'h800b'"));

        CancelInvoiceDialog dlg(m_db);
        dlg.m_verifactu = m_verifactu;
        dlg.findChild<QLineEdit *>("leTicketNum")->setText("800");
        QMetaObject::invokeMethod(&dlg, "onSearchClicked");
        QCOMPARE(dlg.findChild<QTableWidget *>("table")->rowCount(), 2);
        auto *result = dlg.findChild<QLabel *>("lblResult");

        QMetaObject::invokeMethod(&dlg, "onCancelClicked", Q_ARG(int, 0));
        QTRY_VERIFY_WITH_TIMEOUT(result->text().contains("Anulación confirmada"), 10000);
        QMetaObject::invokeMethod(&dlg, "onCancelClicked", Q_ARG(int, 1));
        QTRY_VERIFY_WITH_TIMEOUT(result->text().contains("Anulación confirmada para 800-1"), 10000);

        const auto cancels = m_server.requestsTo("Cancel");
        QCOMPARE(cancels[0].json.value("InvoiceID").toString(), QStringLiteral("800"));
        QCOMPARE(cancels[0].json.value("InvoiceDate").toString(), firstPaid.toString(Qt::ISODate));
        QCOMPARE(cancels[1].json.value("InvoiceID").toString(), QStringLiteral("800-1"));
        QCOMPARE(cancels[1].json.value("InvoiceDate").toString(), QDate::currentDate().toString(Qt::ISODate));
        QCOMPARE(scalar("SELECT verifactu_estado || '|' || fecha_anulacion FROM ingresos WHERE hash='h800a'"),
                 QStringLiteral("ANULADA|%1").arg(today()));
        QCOMPARE(scalar("SELECT verifactu_estado || '|' || fecha_anulacion FROM ingresos WHERE hash='h800b'"),
                 QStringLiteral("ANULADA|%1").arg(today()));
        // AEAT's cancellation record is kept for the Hacienda export.
        QVERIFY(scalar("SELECT verifactu_cancel_xml FROM ingresos WHERE hash='h800a'").contains("Huella"));
        QCOMPARE(dlg.findChild<QTableWidget *>("table")->item(0, 1)->text(), firstPaid.toString("dd-MM-yyyy"));
    }

    // Anular factura on older data. (1) A seq-0 invoice whose garments were paid on
    // different days was registered under the first payment, so that date is sent.
    // (2) Before 10.9 a failed submission was re-sent with the reception date, so
    // AEAT may hold the invoice under it: when AEAT rejects the cancellation, it is
    // tried once more with the reception date. (3) An invoice with no readable
    // payment date is refused locally and nothing is sent.
    void test_cancelInvoice_olderDataDates()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "860", "h860a", "10.00", "28-01-2026", "A-ORIG0860"));
        QVERIFY(E2e::seedSentGarment(m_db, "860", "h860b", "5.00", "05-03-2026", "A-ORIG0860"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET fecha_recepcion = '20-01-2026' WHERE n_recibo = '860'"));
        QVERIFY(E2e::seedSentGarment(m_db, "870", "h870a", "8.00", "garbage", "A-ORIG0870"));

        FakeVerifactuServer::Reply notFound;
        notFound.body = FakeVerifactuServer::rejectedReply("3002", "No existe el registro de facturacion");
        m_server.enqueue("Cancel", notFound);

        CancelInvoiceDialog dlg(m_db);
        dlg.m_verifactu = m_verifactu;
        auto *result = dlg.findChild<QLabel *>("lblResult");
        dlg.findChild<QLineEdit *>("leTicketNum")->setText("860");
        QMetaObject::invokeMethod(&dlg, "onSearchClicked");
        QCOMPARE(dlg.findChild<QTableWidget *>("table")->rowCount(), 1);
        QMetaObject::invokeMethod(&dlg, "onCancelClicked", Q_ARG(int, 0));
        QTRY_VERIFY_WITH_TIMEOUT(result->text().contains("Anulación confirmada para 860"), 10000);

        const auto cancels = m_server.requestsTo("Cancel");
        QCOMPARE(cancels.size(), 2);
        QCOMPARE(cancels[0].json.value("InvoiceDate").toString(), QStringLiteral("2026-01-28"));   // earliest payment
        QCOMPARE(cancels[1].json.value("InvoiceDate").toString(), QStringLiteral("2026-01-20"));   // reception, after the rejection
        QCOMPARE(scalar("SELECT COUNT(*) FROM ingresos WHERE n_recibo='860' AND verifactu_estado='ANULADA'"),
                 QStringLiteral("2"));

        m_server.clear();
        dlg.findChild<QLineEdit *>("leTicketNum")->setText("870");
        QMetaObject::invokeMethod(&dlg, "onSearchClicked");
        QMetaObject::invokeMethod(&dlg, "onCancelClicked", Q_ARG(int, 0));
        QVERIFY(result->text().contains("no tiene fecha de pago"));
        QTest::qWait(300);
        QVERIFY(m_server.requestsTo("Cancel").isEmpty());
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h870a'"), QStringLiteral("ENVIADA"));

        // (4) Both attempts rejected: the operator sees AEAT's answer to each, the
        // first one (the real cause) included, and the invoice stays ENVIADA.
        QVERIFY(E2e::seedSentGarment(m_db, "880", "h880a", "9.00", "05-03-2026", "A-ORIG0880"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET fecha_recepcion = '01-03-2026' WHERE n_recibo = '880'"));
        FakeVerifactuServer::Reply first, second;
        first.body  = FakeVerifactuServer::rejectedReply("4118", "Factura ya anulada");
        second.body = FakeVerifactuServer::rejectedReply("3002", "No existe el registro de facturacion");
        m_server.enqueue("Cancel", first);
        m_server.enqueue("Cancel", second);
        dlg.findChild<QLineEdit *>("leTicketNum")->setText("880");
        QMetaObject::invokeMethod(&dlg, "onSearchClicked");
        QMetaObject::invokeMethod(&dlg, "onCancelClicked", Q_ARG(int, 0));
        QTRY_VERIFY_WITH_TIMEOUT(result->text().contains("Con la fecha de recepción"), 10000);
        QVERIFY(result->text().contains("Factura ya anulada"));
        QVERIFY(result->text().contains("No existe"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h880a'"), QStringLiteral("ENVIADA"));
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

    // Anular factura is offered only on an ENVIADA invoice (a PENDIENTE one has nothing
    // to cancel at AEAT), an unpaid ticket has no invoice at all, and Rectificar
    // refuses an unpaid ticket.
    void test_cancelAndRectify_offeredOnlyForSentInvoices()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "820", "h820a", "12.00", today(), ""));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_estado = 'PENDIENTE' WHERE hash='h820a'"));
        QVERIFY(E2e::seedGarment(m_db, "830", "h830a", "5.00", today()));

        CancelInvoiceDialog cancel(m_db);
        cancel.m_verifactu = m_verifactu;
        auto *table = cancel.findChild<QTableWidget *>("table");
        cancel.findChild<QLineEdit *>("leTicketNum")->setText("820");
        QMetaObject::invokeMethod(&cancel, "onSearchClicked");
        QCOMPARE(table->rowCount(), 1);
        QVERIFY(!qobject_cast<QPushButton *>(table->cellWidget(0, 5))->isEnabled());

        cancel.findChild<QLineEdit *>("leTicketNum")->setText("830");
        QMetaObject::invokeMethod(&cancel, "onSearchClicked");
        QCOMPARE(table->rowCount(), 0);
        QVERIFY(cancel.findChild<QLabel *>("lblResult")->text().contains("no tiene envíos"));

        RectifyInvoiceDialog rectify(m_db);
        rectify.m_verifactu = m_verifactu;
        rectify.findChild<QLineEdit *>("leTicketNum")->setText("830");
        QMetaObject::invokeMethod(&rectify, "onSearchClicked");
        QVERIFY(rectify.findChild<QLabel *>("lblResult")->text().contains("no fue enviado"));
        QVERIFY(!rectify.findChild<QDoubleSpinBox *>("sbAmount")->isEnabled());
        QVERIFY(m_server.requests().isEmpty());
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

    // Recogida, Cobrar: AEAT answers after PayDialog's 5 s wait. The payment is kept
    // PENDIENTE, Recogida takes over the in-flight request, and the late reply marks
    // the rows ENVIADA and says the factura with QR can now be printed.
    void test_recogida_lateReplyAdoptedAfterPayDialogGivesUp()
    {
        QVERIFY(E2e::seedGarment(m_db, "1100", "h1100a", "10.00", today()));
        FakeVerifactuServer::Reply slow;
        slow.delayMs = 7000;
        m_server.enqueue("Create", slow);

        RecogPrendas rp(m_db);
        rp.m_verifactuIntegration = m_verifactu;
        QVERIFY(selectRow(rp, "1100", "h1100a"));
        m_driver->expect(ModalDriver::ofType<PayDialog>(), [](QWidget *w) {
            QMetaObject::invokeMethod(w, "onCobrarClicked");
        });
        rp.findChild<QPushButton *>("pb_pay_all")->click();     // returns when PayDialog gives up (5 s)

        QCOMPARE(m_driver->handled(), 1);
        QCOMPARE(scalar("SELECT pagado || '|' || verifactu_estado FROM ingresos WHERE hash='h1100a'"),
                 QStringLiteral("SI|PENDIENTE"));
        QTRY_COMPARE_WITH_TIMEOUT(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h1100a'"),
                                  QStringLiteral("ENVIADA"), 10000);
        QVERIFY(scalar("SELECT verifactu_csv FROM ingresos WHERE hash='h1100a'").startsWith("A-FAKE"));
        QVERIFY2(rp.statusBar()->currentMessage().contains("ya se puede imprimir la factura con QR"),
                 qPrintable(rp.statusBar()->currentMessage()));
        QCOMPARE(m_server.requestsTo("Create").size(), 1);
    }

    // Recogida, Verifactu dialog on an ERROR row: Reintentar is answered "duplicate",
    // the app asks AEAT by itself, the comparison dialog shows the matching record,
    // and "Actualizar con los datos de AEAT" adopts its CSV.
    void test_recogida_duplicateRetry_comparisonDialogAdoptsAeatCsv()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "1200", "h1200a", "25.00", "10-09-2026", ""));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_estado = 'ERROR', "
                                "verifactu_error = 'Tiempo de espera agotado' WHERE hash='h1200a'"));
        FakeVerifactuServer::Reply dup;
        dup.body = FakeVerifactuServer::rejectedReply("3000", "Registro de factura duplicado: ya existe");
        m_server.enqueue("Create", dup);
        FakeVerifactuServer::Reply found;
        found.body = FakeVerifactuServer::queryReply("1200", "2026-09-10", 25.0, "A-AEAT1200");
        m_server.enqueue("GetFilteredList", found);

        RecogPrendas rp(m_db);
        rp.m_verifactuIntegration = m_verifactu;
        QVERIFY(selectRow(rp, "1200", "h1200a"));
        AeatQuery q;
        m_driver->expect(ModalDriver::named("verifactuDialog"), [&q](QWidget *w) {
            q.hadRetry = w->findChild<QPushButton *>("btnRetry") != nullptr;
            q.hadQuery = w->findChild<QPushButton *>("btnQuery") != nullptr;
            if (auto *retry = w->findChild<QPushButton *>("btnRetry"))
                retry->click();
        });
        m_driver->expect(ModalDriver::named("aeatReconcileDialog"), [&q](QWidget *w) {
            recordComparison(w, q);
            w->findChild<QPushButton *>("btnApply")->click();
        });
        rp.findChild<QPushButton *>("pb_verifactu")->click();

        QVERIFY(q.hadRetry);
        QVERIFY(q.hadQuery);
        QTRY_COMPARE_WITH_TIMEOUT(scalar("SELECT verifactu_estado || '|' || verifactu_csv FROM ingresos "
                                         "WHERE hash='h1200a'"),
                                  QStringLiteral("ENVIADA|A-AEAT1200"), 15000);
        QVERIFY(q.compared);
        QVERIFY2(q.summary.contains("coinciden con los del ticket"), qPrintable(q.summary));
        QCOMPARE(q.aeatCsv, QStringLiteral("A-AEAT1200"));
        QVERIFY(q.applyEnabled);
        QCOMPARE(m_server.requestsTo("Create").size(), 1);
        QCOMPARE(m_server.requestsTo("GetFilteredList").size(), 1);
        QTRY_VERIFY(m_popups->sawMessageContaining("se ha actualizado con el CSV de AEAT"));
    }

    // Recogida, "Consultar en AEAT" when nothing can be adopted: AEAT does not hold
    // the invoice, holds it with different data, or the row is already ENVIADA
    // (informational only). And the buttons each row offers: Reintentar only on
    // ERROR, Consultar on any paid row, neither on an unpaid one.
    void test_recogida_aeatQuery_noAdoptionUnlessItMatches()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "1300", "h1300a", "25.00", "10-09-2026", ""));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_estado = 'ERROR' WHERE hash='h1300a'"));
        QVERIFY(E2e::seedSentGarment(m_db, "1400", "h1400a", "18.00", "11-09-2026", "A-ORIG1400"));
        QVERIFY(E2e::seedGarment(m_db, "1500", "h1500a", "7.00", today()));

        RecogPrendas rp(m_db);
        rp.m_verifactuIntegration = m_verifactu;

        // Not found: the default GetFilteredList reply is an empty list.
        QVERIFY(selectRow(rp, "1300", "h1300a"));
        const AeatQuery notFound = openVerifactuDialog(rp, true);
        QVERIFY(notFound.hadRetry && notFound.hadQuery);
        QVERIFY(notFound.compared);
        QVERIFY2(notFound.summary.contains("no ha devuelto ninguna factura"), qPrintable(notFound.summary));
        QVERIFY(!notFound.applyEnabled);

        // Found, but the amount differs from the ticket.
        FakeVerifactuServer::Reply other;
        other.body = FakeVerifactuServer::queryReply("1300", "2026-09-10", 99.0, "A-OTHER1300");
        m_server.enqueue("GetFilteredList", other);
        QVERIFY(selectRow(rp, "1300", "h1300a"));
        const AeatQuery mismatch = openVerifactuDialog(rp, true);
        QVERIFY(mismatch.compared);
        QVERIFY2(mismatch.summary.contains("NO coinciden"), qPrintable(mismatch.summary));
        QVERIFY(!mismatch.applyEnabled);

        // Already ENVIADA: no Reintentar; the query matches but is informational only.
        FakeVerifactuServer::Reply same;
        same.body = FakeVerifactuServer::queryReply("1400", "2026-09-11", 18.0, "A-ORIG1400");
        m_server.enqueue("GetFilteredList", same);
        QVERIFY(selectRow(rp, "1400", "h1400a"));
        const AeatQuery settled = openVerifactuDialog(rp, true);
        QVERIFY(!settled.hadRetry && settled.hadQuery);
        QVERIFY(settled.compared);
        QVERIFY(!settled.applyEnabled);
        QVERIFY2(settled.applyTip.contains("solo informativa"), qPrintable(settled.applyTip));

        // Unpaid: no invoice, so neither button.
        QVERIFY(selectRow(rp, "1500", "h1500a"));
        const AeatQuery unpaid = openVerifactuDialog(rp, false);
        QVERIFY(!unpaid.hadRetry && !unpaid.hadQuery);

        QVERIFY(m_server.requestsTo("Create").isEmpty());
        QCOMPARE(m_server.requestsTo("GetFilteredList").size(), 3);
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h1300a'"), QStringLiteral("ERROR"));
    }

    // Exportar registros AEAT, as the menu runs it (aeatExportRecords + the XML
    // writer): one <Registro> per invoice AEAT holds - a two-garment payment is one
    // record with the event total, a later partial payment its own "<n>-1" record,
    // an invoice known only by its CSV is marked sinPayload, an earlier invoice
    // cancelled in the period carries fechaAnulacion - each stored payload inlined
    // once, and the document well-formed.
    void test_aeatExport_oneRegistroPerPaymentEvent()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "1600", "h1600a", "10.00", "05-03-2026", "CSV-1600"));
        QVERIFY(E2e::seedSentGarment(m_db, "1600", "h1600b", "4.50", "05-03-2026", "CSV-1600"));
        QVERIFY(E2e::seedSentGarment(m_db, "1600", "h1600c", "6.00", "20-03-2026", "CSV-1600-1"));
        QVERIFY(E2e::seedGarment(m_db, "1600", "h1600d", "3.00", "05-03-2026"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_invoice_seq = 1, verifactu_invoice_id = '1600-1' "
                                "WHERE hash='h1600c'"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_xml = "
                                "'<?xml version=\"1.0\"?><RegistroAlta><IDFactura>' || verifactu_invoice_id || "
                                "'</IDFactura></RegistroAlta>' WHERE pagado = 'SI'"));

        // Recovered with "Consultar en AEAT": CSV only. Issued in January, cancelled in
        // March with AEAT's cancellation record stored; issued in March, cancelled in
        // April (after the period).
        QVERIFY(E2e::seedSentGarment(m_db, "1700", "h1700a", "5.00", "10-03-2026", "CSV-1700"));
        QVERIFY(E2e::seedSentGarment(m_db, "1800", "h1800a", "12.00", "05-01-2026", "CSV-1800"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_xml = "
                                "'<RegistroAlta><IDFactura>1800</IDFactura></RegistroAlta>', "
                                "verifactu_cancel_xml = '<?xml version=\"1.0\"?><RegistroAnulacion>"
                                "<IDFacturaAnulada>1800</IDFacturaAnulada></RegistroAnulacion>', "
                                "verifactu_estado = 'ANULADA', fecha_anulacion = '15-03-2026' WHERE hash='h1800a'"));
        QVERIFY(E2e::seedSentGarment(m_db, "1900", "h1900a", "9.00", "12-03-2026", "CSV-1900"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_xml = "
                                "'<RegistroAlta><IDFactura>1900</IDFactura></RegistroAlta>', "
                                "verifactu_estado = 'ANULADA', fecha_anulacion = '02-04-2026' WHERE hash='h1900a'"));

        QVector<AeatExportRecord> records;
        QVERIFY(aeatExportRecords(m_db, QDate(2026, 3, 1), QDate(2026, 3, 31), records));
        QBuffer buffer;
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        const int written = writeAeatExportXml(&buffer, records, QDate(2026, 3, 1), QDate(2026, 3, 31),
                                               "B00000000", "Tintoreria E2E");
        buffer.close();
        QCOMPARE(written, 5);

        QXmlStreamReader xml(buffer.data());
        QStringList registros, anulaciones, payloadIds;
        while (!xml.atEnd()) {
            if (xml.readNext() != QXmlStreamReader::StartElement)
                continue;
            if (xml.name() == QLatin1String("Registro"))
                registros << xml.attributes().value("invoiceId").toString() + "|"
                             + xml.attributes().value("fechaPago").toString() + "|"
                             + xml.attributes().value("importe").toString() + "|"
                             + xml.attributes().value("csv").toString() + "|"
                             + xml.attributes().value("fechaAnulacion").toString() + "|"
                             + xml.attributes().value("sinPayload").toString();
            else if (xml.name() == QLatin1String("Anulacion"))
                anulaciones << xml.attributes().value("invoiceId").toString() + "|"
                               + xml.attributes().value("fechaAnulacion").toString() + "|"
                               + xml.attributes().value("sinPayload").toString();
            else if (xml.name() == QLatin1String("IDFactura") || xml.name() == QLatin1String("IDFacturaAnulada"))
                payloadIds << xml.name().toString() + ":" + xml.readElementText();
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        // Issued in March: Registro. Cancelled in March: Anulacion with AEAT's
        // record; the January invoice itself is not repeated here.
        QCOMPARE(registros, QStringList({ "1600|05-03-2026|14.50|CSV-1600||",
                                          "1700|10-03-2026|5.00|CSV-1700||1",
                                          "1900|12-03-2026|9.00|CSV-1900|02-04-2026|",
                                          "1600-1|20-03-2026|6.00|CSV-1600-1||" }));
        QCOMPARE(anulaciones, QStringList({ "1800|15-03-2026|" }));
        QCOMPARE(payloadIds, QStringList({ "IDFacturaAnulada:1800", "IDFactura:1600", "IDFactura:1900",
                                           "IDFactura:1600-1" }));
    }

    // Exportar registros AEAT on awkward stored data: a payload that is not a
    // well-formed fragment is written as text and the file stays readable; an
    // invoice AEAT registered under another date than its payment says so
    // (fechaExpedicion, from the payload); a cancellation made before 10.12 (no
    // date) goes with its invoice, marked sinFecha; an undated invoice is counted.
    void test_aeatExport_awkwardStoredData()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "2100", "h2100a", "10.00", "05-03-2026", "CSV-2100"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_xml = "
                                "'<!DOCTYPE x><RegistroAlta>2100</RegistroAlta>' WHERE hash='h2100a'"));
        QVERIFY(E2e::seedSentGarment(m_db, "2200", "h2200a", "12.00", "06-03-2026", "CSV-2200"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET fecha_recepcion = '02-03-2026', verifactu_estado = 'ANULADA', "
                                "verifactu_xml = '<sum1:RegistroAlta xmlns:sum1=\"urn:sum1\"><sum1:IDFactura>"
                                "<sum1:FechaExpedicionFactura>02-03-2026</sum1:FechaExpedicionFactura>"
                                "</sum1:IDFactura><sum1:Encadenamiento><sum1:FechaExpedicionFactura>01-03-2026"
                                "</sum1:FechaExpedicionFactura></sum1:Encadenamiento></sum1:RegistroAlta>', "
                                "verifactu_cancel_xml = '<sum1:RegistroAnulacion xmlns:sum1=\"urn:sum1\"><sum1:FechaExpedicionFacturaAnulada>"
                                "02-03-2026</sum1:FechaExpedicionFacturaAnulada></sum1:RegistroAnulacion>', "
                                "fecha_anulacion = '20-03-2026' WHERE hash='h2200a'"));
        QVERIFY(E2e::seedSentGarment(m_db, "2300", "h2300a", "7.00", "07-03-2026", "CSV-2300"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_estado = 'ANULADA', verifactu_xml = "
                                "'<sum1:FechaExpedicionFactura xmlns:sum1=\"urn:sum1\">07-03-2026</sum1:FechaExpedicionFactura>' "
                                "WHERE hash='h2300a'"));
        QVERIFY(E2e::seedSentGarment(m_db, "2350", "h2350a", "3.00", "07-03-2026", "CSV-2350"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_estado = 'ANULADA', fecha_anulacion = '3/20' "
                                "WHERE hash='h2350a'"));
        QVERIFY(E2e::seedSentGarment(m_db, "2400", "h2400a", "9.00", "", "CSV-2400"));
        QVERIFY(E2e::seedSentGarment(m_db, "2450", "h2450a", "4.00", "08-03-2026", "CSV-2450"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_xml = '<sum1:Huella>AB</sum1:Huella>' "
                                "WHERE hash='h2450a'"));

        QVector<AeatExportRecord> records;
        int undated = 0;
        QVERIFY(aeatExportRecords(m_db, QDate(2026, 3, 1), QDate(2026, 3, 31), records, &undated));
        QCOMPARE(undated, 1);
        QBuffer buffer;
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QCOMPARE(writeAeatExportXml(&buffer, records, QDate(2026, 3, 1), QDate(2026, 3, 31),
                                    "B00000000", "Tintoreria E2E"), 8);
        buffer.close();

        QXmlStreamReader xml(buffer.data());
        QStringList found;
        while (!xml.atEnd()) {
            if (xml.readNext() != QXmlStreamReader::StartElement)
                continue;
            if (xml.name() != QLatin1String("Registro") && xml.name() != QLatin1String("Anulacion"))
                continue;
            const QXmlStreamAttributes a = xml.attributes();
            const QString kind = xml.name().toString();
            const QString text = a.value("payloadComoTexto") == QLatin1String("1") ? xml.readElementText() : QString();
            found << kind + ":" + a.value("invoiceId").toString() + "|" + a.value("fechaExpedicion").toString()
                     + "|" + a.value("fechaAnulacion").toString() + "|" + a.value("sinFecha").toString()
                     + "|" + text;
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QCOMPARE(found, QStringList({
            "Registro:2100||||<!DOCTYPE x><RegistroAlta>2100</RegistroAlta>",
            "Registro:2200|02-03-2026|20-03-2026||",
            "Anulacion:2200|02-03-2026|20-03-2026||",
            "Registro:2300||||",
            "Anulacion:2300|||1|",
            "Registro:2350||||",
            "Anulacion:2350|||1|",
            "Registro:2450||||<sum1:Huella>AB</sum1:Huella>" }));
    }

    // Recogida -> Separar prendas on a paid garment of a sent invoice: the split-off
    // garment stays in that invoice (same seq, estado, CSV) and the two rows keep
    // its total, whatever the price list says now.
    void test_recogida_splitPaidGarment_staysInItsInvoice()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "2500", "h2500a", "10.00", "05-03-2026", "CSV-2500"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET cantidad = '3', verifactu_invoice_seq = 1, "
                                "verifactu_invoice_id = '2500-1' WHERE hash='h2500a'"));
        RecogPrendas rp(m_db);
        QVERIFY(selectRow(rp, "2500", "h2500a"));
        m_driver->expect(ModalDriver::ofType<QInputDialog>(), [](QWidget *w) {
            auto *dlg = qobject_cast<QInputDialog *>(w);
            dlg->setIntValue(1);
            dlg->accept();
        });
        rp.findChild<QPushButton *>("pb_separ_garm")->click();

        QCOMPARE(m_driver->handled(), 1);
        QCOMPARE(scalar("SELECT GROUP_CONCAT(cantidad || ':' || importe || ':' || verifactu_estado || ':' || "
                        "verifactu_csv || ':' || verifactu_invoice_seq || ':' || verifactu_invoice_id, ' ') "
                        "FROM (SELECT * FROM ingresos WHERE n_recibo = '2500' ORDER BY CAST(cantidad AS INTEGER))"),
                 QStringLiteral("1:3.33:ENVIADA:CSV-2500:1:2500-1 2:6.67:ENVIADA:CSV-2500:1:2500-1"));
    }

    // A reply that lands once the invoice is already settled (here: a duplicate
    // rejection for rows another path registered meanwhile) changes nothing, and
    // Recogida says so instead of "Error al enviar", without asking AEAT again.
    void test_recogida_replyForSettledInvoiceIgnored()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "2600", "h2600a", "25.00", "10-09-2026", ""));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_estado = 'ERROR', "
                                "verifactu_error = 'Tiempo de espera agotado' WHERE hash='h2600a'"));
        FakeVerifactuServer::Reply dup;
        dup.body = FakeVerifactuServer::rejectedReply("3000", "Registro de factura duplicado: ya existe");
        dup.delayMs = 1500;
        m_server.enqueue("Create", dup);

        RecogPrendas rp(m_db);
        rp.m_verifactuIntegration = m_verifactu;
        QVERIFY(selectRow(rp, "2600", "h2600a"));
        m_driver->expect(ModalDriver::named("verifactuDialog"), [](QWidget *w) {
            w->findChild<QPushButton *>("btnRetry")->click();
        });
        rp.findChild<QPushButton *>("pb_verifactu")->click();
        QCOMPARE(m_driver->handled(), 1);
        bool compared = false;     // must not open: closed if it does, so the test fails instead of hanging
        m_driver->expect(ModalDriver::named("aeatReconcileDialog"), [&compared](QWidget *w) {
            compared = true;
            qobject_cast<QDialog *>(w)->reject();
        });
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_estado = 'ENVIADA', verifactu_csv = 'A-SETTLED', "
                                "verifactu_error = '' WHERE hash='h2600a'"));

        QTRY_VERIFY_WITH_TIMEOUT(rp.statusBar()->currentMessage().contains("la factura ya estaba registrada"), 10000);
        QCOMPARE(scalar("SELECT verifactu_estado || '|' || verifactu_csv FROM ingresos WHERE hash='h2600a'"),
                 QStringLiteral("ENVIADA|A-SETTLED"));
        QCOMPARE(m_server.requestsTo("Create").size(), 1);
        QCOMPARE(m_server.requestsTo("GetFilteredList").size(), 0);
        QVERIFY(!compared);
    }

    // Añadir nuevas prendas: a garment added as already paid is that ticket's first
    // invoice (seq 0, InvoiceID = the ticket number), submitted to AEAT right away
    // instead of waiting for the next startup's recovery dialog; the ticket's unpaid
    // garment stays SIN COBRAR and chargeable.
    void test_addGarmentPaid_submittedAtOnce()
    {
        QVERIFY(E2e::seedGarment(m_db, "2000", "h2000a", "10.00", today()));
        MainWindow mw;
        QMetaObject::invokeMethod(&mw, "on_actionAnadir_nuevas_prendas_triggered");
        auto *add = mw.findChild<AddGarment *>();
        QVERIFY(add);
        add->findChild<QLineEdit *>("le_n_recibo")->setText("2000");
        QMetaObject::invokeMethod(add, "on_pb_search_pressed");
        QVERIFY(add->ticketFound);
        add->findChild<QComboBox *>("cb_prenda")->setCurrentText("Camisa");
        add->findChild<QLineEdit *>("le_cantidad")->setText("2");
        add->findChild<QPushButton *>("pb_pagado")->setChecked(true);
        add->findChild<QDialogButtonBox *>("buttonBox")->button(QDialogButtonBox::Save)->click();

        QTRY_COMPARE_WITH_TIMEOUT(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo='2000' AND pagado='SI'"),
                                  QStringLiteral("ENVIADA"), 10000);
        const auto creates = m_server.requestsTo("Create");
        QCOMPARE(creates.size(), 1);
        QCOMPARE(creates[0].json.value("InvoiceID").toString(), QStringLiteral("2000"));
        QCOMPARE(creates[0].json.value("InvoiceDate").toString(), QDate::currentDate().toString(Qt::ISODate));
        QCOMPARE(creates[0].json.value("TotalAmount").toDouble(), 7.0);
        QCOMPARE(scalar("SELECT verifactu_invoice_seq FROM ingresos WHERE n_recibo='2000' AND pagado='SI'"),
                 QStringLiteral("0"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h2000a'"), QStringLiteral("SIN COBRAR"));
    }

    // Añadir nuevas prendas: the number retyped after the search (here into an already
    // sent ticket) was never checked, so the save is refused - nothing is inserted into
    // that ticket and nothing is sent to AEAT under its InvoiceID.
    void test_addGarment_retypedTicketNumberRefused()
    {
        QVERIFY(E2e::seedGarment(m_db, "2100", "h2100a", "10.00", today()));
        QVERIFY(E2e::seedSentGarment(m_db, "2200", "h2200a", "12.00", today(), "A-ORIG2200"));
        MainWindow mw;
        QMetaObject::invokeMethod(&mw, "on_actionAnadir_nuevas_prendas_triggered");
        auto *add = mw.findChild<AddGarment *>();
        add->findChild<QLineEdit *>("le_n_recibo")->setText("2100");
        QMetaObject::invokeMethod(add, "on_pb_search_pressed");
        QVERIFY(add->ticketFound);
        add->findChild<QLineEdit *>("le_n_recibo")->setText("2200");
        add->findChild<QComboBox *>("cb_prenda")->setCurrentText("Camisa");
        add->findChild<QLineEdit *>("le_cantidad")->setText("1");
        add->findChild<QPushButton *>("pb_pagado")->setChecked(true);
        add->findChild<QDialogButtonBox *>("buttonBox")->button(QDialogButtonBox::Save)->click();

        QTRY_VERIFY(m_popups->sawMessageContaining("No se ha buscado"));
        QTest::qWait(300);
        QCOMPARE(scalar("SELECT COUNT(*) FROM ingresos WHERE n_recibo='2200'"), QStringLiteral("1"));
        QCOMPARE(scalar("SELECT verifactu_estado || '|' || verifactu_csv FROM ingresos WHERE hash='h2200a'"),
                 QStringLiteral("ENVIADA|A-ORIG2200"));
        QVERIFY(m_server.requestsTo("Create").isEmpty());
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
