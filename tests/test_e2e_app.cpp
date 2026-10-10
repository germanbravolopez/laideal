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
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
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
#include <QSqlTableModel>
#include <QXmlStreamReader>

#include "aeatexport.h"
#include "aeatexportdialog.h"
#include "appsettings.h"
#include "cancelinvoicedialog.h"
#include "contabilidad.h"
#include "e2efixture.h"
#include "facturas.h"
#include "genlistado.h"
#include "insertnewitem.h"
#include "listado.h"
#include "add_garment.h"
#include "fakeverifactuserver.h"
#include "imprimir.h"
#include "mainwindow.h"
#include "modalautocloser.h"
#include "modaldriver.h"
#include "pay_dialog.h"
#include "recog_prendas.h"
#include "reporthtml.h"
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
        mw.findChild<QPushButton *>("pb_save")->click();
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
        ReportHtml::setOpenGeneratedReports(false);

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
        mw.findChild<QCheckBox *>("pb_payment")->setChecked(true);
        clickSave(mw);

        const auto creates = m_server.requestsTo("Create");
        QCOMPARE(creates.size(), 1);
        QCOMPARE(creates[0].json.value("InvoiceID").toString(), second);
        QCOMPARE(scalar(QStringLiteral("SELECT pagado || '|' || fecha_pago || '|' || verifactu_estado || '|' "
                                       "|| verifactu_csv FROM ingresos WHERE n_recibo='%1'").arg(second)),
                 QStringLiteral("SI|%1|ENVIADA|A-FAKE0001").arg(today()));
        QVERIFY2(m_popups->messages().isEmpty(), qPrintable(m_popups->messages().join(" / ")));
    }

    // MainWindow: Save without a client is refused in the result panel and stores nothing.
    void test_mainWindow_saveWithoutClient_refused()
    {
        MainWindow mw;
        enterGarment(mw, "Camisa", "1");
        clickSave(mw);
        QVERIFY(mw.findChild<QLabel *>("lblResult")->text().contains("No se ha introducido ningún cliente"));
        QVERIFY2(m_popups->messages().isEmpty(), qPrintable(m_popups->messages().join(" / ")));
        QCOMPARE(scalar("SELECT COUNT(*) FROM ingresos"), QStringLiteral("0"));
    }

    // MainWindow, the new layout: a garment picked before its quantity counts one and
    // is priced; the total is read-only; an m2 garment can be saved before it is
    // measured (any "m2", not only Alfombra); the save is summarised in the result
    // panel; the quick buttons open Recogida; a closed quarter is refused there too.
    void test_mainWindow_entryFlowAndMessages()
    {
        QVERIFY(E2e::exec(m_db, "INSERT INTO prendas VALUES ('Jarapa (m2)', '9.5', '0')"));
        MainWindow mw;
        auto *table = mw.findChild<QTableWidget *>("table_ticket");
        QCOMPARE(table->rowCount(), kInitialTicketRows);
        QVERIFY(mw.findChild<QLineEdit *>("le_cost_total")->isReadOnly());
        QVERIFY(mw.findChild<QLineEdit *>("le_nr_ticket")->isReadOnly());

        // Garment on row 1 without touching the current cell (a combo does not move it).
        qobject_cast<QComboBox *>(table->cellWidget(1, 1))->setCurrentText("Camisa");
        QCOMPARE(table->item(1, 0)->text(), QStringLiteral("1"));
        QCOMPARE(table->item(1, 5)->text(), QStringLiteral("3.50"));
        QVERIFY(!table->item(0, 5) || table->item(0, 5)->text().isEmpty());   // row 0 untouched
        QCOMPARE(mw.findChild<QLineEdit *>("le_cost_total")->text(), QStringLiteral("3.50"));
        // A hand-typed price is kept and counted, with a decimal comma.
        table->item(1, 5)->setText("3,00");
        QCOMPARE(mw.findChild<QLineEdit *>("le_cost_total")->text(), QStringLiteral("3.00"));

        // An m2 garment alone, not measured yet: saved with 0.
        mw.findChild<QPushButton *>("pb_reset")->click();
        const QString ticket = mw.findChild<QLineEdit *>("le_nr_ticket")->text();
        mw.findChild<QComboBox *>("cb_client")->setCurrentText("Cliente Jarapa");
        qobject_cast<QComboBox *>(table->cellWidget(0, 1))->setCurrentText("Jarapa (m2)");
        clickSave(mw);
        QCOMPARE(scalar(QStringLiteral("SELECT prenda || '|' || importe FROM ingresos WHERE n_recibo='%1'").arg(ticket)),
                 QStringLiteral("Jarapa (m2)|0.00"));
        const QString result = mw.findChild<QLabel *>("lblResult")->text();
        QVERIFY2(result.contains(QStringLiteral("Ticket %1 guardado: Cliente Jarapa, 1 prenda(s)").arg(ticket))
                 && result.contains("sin cobrar"), qPrintable(result));

        // Paid with the m2 garment still unmeasured: refused (it would be invoiced at 0).
        const QString paidTry = mw.findChild<QLineEdit *>("le_nr_ticket")->text();
        mw.findChild<QComboBox *>("cb_client")->setCurrentText("Cliente Jarapa");
        qobject_cast<QComboBox *>(table->cellWidget(0, 1))->setCurrentText("Jarapa (m2)");
        mw.findChild<QCheckBox *>("pb_payment")->setChecked(true);
        clickSave(mw);
        QVERIFY2(mw.findChild<QLabel *>("lblResult")->text().contains("sin medir"),
                 qPrintable(mw.findChild<QLabel *>("lblResult")->text()));
        QCOMPARE(scalar(QStringLiteral("SELECT COUNT(*) FROM ingresos WHERE n_recibo='%1'").arg(paidTry)), QStringLiteral("0"));
        QVERIFY(m_server.requestsTo("Create").isEmpty());
        mw.findChild<QPushButton *>("pb_reset")->click();

        // A quantity typed in a slot without a garment is not saved as a garment row.
        const QString withSlot = mw.findChild<QLineEdit *>("le_nr_ticket")->text();
        mw.findChild<QComboBox *>("cb_client")->setCurrentText("Cliente Jarapa");
        enterGarment(mw, "Camisa", "1");
        table->setItem(3, 0, new QTableWidgetItem("2"));
        clickSave(mw);
        QCOMPARE(scalar(QStringLiteral("SELECT COUNT(*) || '|' || SUM(importe) FROM ingresos WHERE n_recibo='%1'").arg(withSlot)),
                 QStringLiteral("1|3.5"));

        // Closed quarter: refused in the panel, nothing saved.
        QVERIFY(E2e::exec(m_db, "INSERT INTO gastos (id, n_factura, servicio, descripcion, empresa, fecha, iva, "
                                "importe, edit_lock) VALUES (9, 'X', '', '', '', '05-01-2026', '21', '1.00', 1)"));
        const QString next = mw.findChild<QLineEdit *>("le_nr_ticket")->text();
        mw.findChild<QComboBox *>("cb_client")->setCurrentText("Cliente Jarapa");
        mw.findChild<QDateEdit *>("de_date_recep")->setDate(QDate(2026, 2, 10));
        enterGarment(mw, "Camisa", "1");
        clickSave(mw);
        QVERIFY(mw.findChild<QLabel *>("lblResult")->text().contains("contabilidad cerrada"));
        QCOMPARE(scalar(QStringLiteral("SELECT COUNT(*) FROM ingresos WHERE n_recibo='%1'").arg(next)), QStringLiteral("0"));

        mw.findChild<QPushButton *>("pb_quick_recogida")->click();
        QTRY_VERIFY(mw.findChild<RecogPrendas *>());
        QVERIFY2(m_popups->messages().isEmpty(), qPrintable(m_popups->messages().join(" / ")));
        E2e::exec(m_db, "DELETE FROM prendas WHERE nombre = 'Jarapa (m2)'");
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

    // Recogida -> Añadir prendas…: the Añadir nuevas prendas window opens on the
    // selected ticket; a garment added as paid is sent to AEAT as the ticket's first
    // invoice; closing it refreshes Recogida, which reports the addition. Herramientas
    // keeps the entry, now grouped by function.
    void test_recogida_addGarmentFromTheWindow()
    {
        QVERIFY(E2e::seedGarment(m_db, "920", "h920a", "10.00", today()));
        RecogPrendas rp(m_db);
        rp.m_verifactuIntegration = m_verifactu;
        QVERIFY(selectRow(rp, "920", "h920a"));
        rp.findChild<QPushButton *>("pb_add")->click();
        QPointer<AddGarment> add = rp.findChild<AddGarment *>();
        QVERIFY(add);
        QCOMPARE(add->findChild<QLineEdit *>("leNRecibo")->text(), QStringLiteral("920"));
        QVERIFY(add->ticketFound);
        add->findChild<QComboBox *>("cbPrenda")->setCurrentText("Camisa");
        add->findChild<QLineEdit *>("leCantidad")->setText("1");
        add->findChild<QCheckBox *>("chkPagado")->setChecked(true);
        add->findChild<QPushButton *>("btnSave")->click();
        add->findChild<QPushButton *>("btnClose")->click();

        QTRY_COMPARE_WITH_TIMEOUT(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo='920' AND pagado='SI'"),
                                  QStringLiteral("ENVIADA"), 10000);
        const auto creates = m_server.requestsTo("Create");
        QCOMPARE(creates.size(), 1);
        QCOMPARE(creates[0].json.value("InvoiceID").toString(), QStringLiteral("920"));
        QVERIFY2(rp.findChild<QLabel *>("lblResult")->text().contains("1 prenda(s) añadidas al ticket 920"),
                 qPrintable(rp.findChild<QLabel *>("lblResult")->text()));
        QTRY_VERIFY(add.isNull());                                        // deletes itself on close

        MainWindow mw;
        QMenu *tools = mw.findChild<QMenu *>("menuHerramientas");
        QVERIFY(tools);
        QStringList order;
        for (QAction *a : tools->actions())
            order << (a->isSeparator() ? QStringLiteral("|") : a->objectName().isEmpty() ? a->text() : a->objectName());
        QCOMPARE(order.mid(0, 2), QStringList({ "actionRecogida_de_prendas", "actionAnadir_nuevas_prendas" }));
        QVERIFY(order.indexOf("actionAnular_factura_verifactu") < order.indexOf("actionFormulario_facturas"));
        QVERIFY(order.indexOf("actionFormulario_facturas") < order.indexOf("actionGenerar_contabilidad"));
    }

    // Recogida -> Anular prendas…: opens the void dialog on the selected garment's
    // ticket, the voided garment then shows its Anulación date in Recogida, and the
    // Herramientas menu no longer carries the entry.
    void test_recogida_voidGarmentsFromTheWindow()
    {
        QVERIFY(E2e::seedGarment(m_db, "910", "h910a", "10.00", today()));
        QVERIFY(E2e::seedGarment(m_db, "910", "h910b", "5.00", today()));
        RecogPrendas rp(m_db);
        QVERIFY(selectRow(rp, "910", "h910a"));
        QCOMPARE(rp.findChild<QDateEdit *>("de_date_anul")->text(), QStringLiteral("-"));   // not voided
        QString loadedTicket;
        m_driver->expect(ModalDriver::ofType<VoidGarmentsDialog>(), [&loadedTicket](QWidget *w) {
            loadedTicket = w->findChild<QLineEdit *>("leTicketNum")->text();
            auto *table = w->findChild<QTableWidget *>("table");
            for (int r = 0; r < table->rowCount(); ++r)
                if (table->item(r, 4)->text() != QLatin1String("Anulado")
                        && table->item(r, 3)->text().startsWith("10"))
                    table->item(r, 0)->setCheckState(Qt::Checked);
            w->findChild<QPushButton *>("btnVoid")->click();               // confirmation answered Yes
            qobject_cast<QDialog *>(w)->accept();
        });
        rp.findChild<QPushButton *>("pb_void")->click();

        QCOMPARE(m_driver->handled(), 1);
        QCOMPARE(loadedTicket, QStringLiteral("910"));
        QCOMPARE(scalar("SELECT estado || '|' || fecha_anulacion FROM ingresos WHERE hash='h910a'"),
                 QStringLiteral("Anulado|%1").arg(today()));
        QVERIFY2(rp.findChild<QLabel *>("lblResult")->text().contains("1 prenda(s) del ticket 910 anuladas"),
                 qPrintable(rp.findChild<QLabel *>("lblResult")->text()));
        QVERIFY(selectRow(rp, "910", "h910a"));
        QCOMPARE(rp.findChild<QDateEdit *>("de_date_anul")->date(), QDate::currentDate());
        // A voided garment: "Anulada" in green, no NO / En tienda labels, no pickup date.
        const QString anulBadge = rp.findChild<QLabel *>("lbl_anul_badge")->text();
        QVERIFY2(anulBadge.contains("Anulada") && anulBadge.contains("green"), qPrintable(anulBadge));
        QVERIFY(rp.findChild<QLabel *>("lbl_payment_badge")->text().isEmpty());
        QVERIFY(rp.findChild<QLabel *>("lbl_state_badge")->text().isEmpty());
        QCOMPARE(rp.findChild<QDateEdit *>("de_date_pickup")->text(), QStringLiteral("-"));
        // The other garment of the ticket, unpaid and in the shop, shows both labels again.
        QVERIFY(selectRow(rp, "910", "h910b"));
        QVERIFY(rp.findChild<QLabel *>("lbl_payment_badge")->text().contains("NO"));
        QVERIFY(rp.findChild<QLabel *>("lbl_state_badge")->text().contains("En tienda"));
        QCOMPARE(rp.findChild<QDateEdit *>("de_date_pickup")->date(), QDate::currentDate());

        MainWindow mw;
        QVERIFY(!mw.findChild<QAction *>("actionAnular_prendas"));
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

    // Imprimir from the menu, all in the window (printing is off in the test settings):
    // an unknown number; a recibo with the shop copy; a full invoice with the typed
    // DNI and address on the ticket; a ticket with two partial-payment invoices, where
    // the first press lists them and the second prints the chosen one. No pop-ups.
    void test_imprimirDialog_reciboFacturaCompletaAndPartialPayments()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "650", "h650a", "10.00", "10-02-2026", "A-650"));
        QVERIFY(E2e::seedSentGarment(m_db, "651", "h651a", "8.00", "10-02-2026", "A-651"));
        QVERIFY(E2e::seedSentGarment(m_db, "651", "h651b", "5.00", "12-02-2026", "A-651-1"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_invoice_seq = 1, verifactu_invoice_id = '651-1' "
                                "WHERE hash='h651b'"));
        const auto open = [this](bool recibo, bool complete) {
            auto *dlg = new Imprimir(m_db);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->isRecibo = recibo;
            dlg->isCompleteInvoice = complete;
            dlg->show();
            return QPointer<Imprimir>(dlg);
        };
        const auto press = [](Imprimir *dlg, const QString &ticket) {
            dlg->le_n_ticket->setText(ticket);
            dlg->findChild<QPushButton *>("btnPrint")->click();
            return dlg->findChild<QLabel *>("lblResult")->text();
        };

        QPointer<Imprimir> recibo = open(true, false);
        QVERIFY(press(recibo, "999").contains("No se ha encontrado el recibo"));
        QString text = press(recibo, "650");
        QVERIFY2(text.contains("Recibo Nº 650 impreso (copia del cliente y del establecimiento)")
                 && text.contains("impresión está desactivada"), qPrintable(text));
        recibo->close();

        QPointer<Imprimir> complete = open(false, true);
        QVERIFY(complete->findChild<QLineEdit *>("leDni")->isVisible());
        complete->findChild<QLineEdit *>("leDni")->setText("12345678Z");
        complete->findChild<QLineEdit *>("leAddress")->setText("Calle Mayor 1");
        text = press(complete, "650");
        QVERIFY2(text.contains("Factura completa 650 impresa"), qPrintable(text));
        QVERIFY(complete->ticketBytes().contains("DNI: 12345678Z"));
        QVERIFY(complete->ticketBytes().contains("Calle Mayor 1"));
        complete->close();

        QPointer<Imprimir> factura = open(false, false);
        QVERIFY(!factura->findChild<QLineEdit *>("leDni")->isVisible());
        text = press(factura, "651");
        QVERIFY2(text.contains("tiene 2 facturas en AEAT"), qPrintable(text));
        auto *events = factura->findChild<QComboBox *>("cbEvent");
        QVERIFY(events->isVisible());
        QCOMPARE(events->count(), 3);                                       // 651, 651-1, all
        events->setCurrentIndex(1);
        text = press(factura, "651");
        QVERIFY2(text.contains("Facturas impresas: 651-1."), qPrintable(text));
        QVERIFY(factura->ticketBytes().contains("651-1"));
        QVERIFY2(m_popups->messages().isEmpty(), qPrintable(m_popups->messages().join(" | ")));
        factura->close();
        QTRY_VERIFY(factura.isNull());
    }

    // The Imprimir window stays open between prints, so a QR fetched for one invoice
    // must never be printed on the next one (here a voided ticket, which has none).
    void test_imprimirDialog_qrNotReusedForTheNextTicket()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "660", "h660a", "10.00", "10-02-2026", "A-660"));
        QVERIFY(E2e::seedSentGarment(m_db, "661", "h661a", "8.00", "10-02-2026", "A-661"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_estado = 'ANULADA' WHERE n_recibo='661'"));
        Imprimir dlg(m_db);
        dlg.verifactuIntegration = m_verifactu;
        dlg.isRecibo = false;
        dlg.isCompleteInvoice = false;
        const auto press = [&dlg](const QString &ticket) {
            dlg.le_n_ticket->setText(ticket);
            dlg.findChild<QPushButton *>("btnPrint")->click();
            return dlg.findChild<QLabel *>("lblResult")->text();
        };
        QString text = press("660");
        QVERIFY2(!text.contains("Sin código QR"), qPrintable(text));
        QVERIFY(!dlg.qrCode.isNull());
        text = press("661");
        QVERIFY2(text.contains("Sin código QR"), qPrintable(text));
        QVERIFY(dlg.qrCode.isNull());
        QCOMPARE(m_server.requestsTo("GetQrCode").size(), 1);
    }

    // Recogida reports a refused write instead of a success, after its table refresh:
    // Separar on a row locked by Contabilidad, and a negative hand-typed price.
    void test_recogida_refusedWritesAreReported()
    {
        QVERIFY(E2e::seedGarment(m_db, "940", "h940a", "9.00", today()));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET cantidad = '3', edit_lock = 1 WHERE hash='h940a'"));
        QVERIFY(E2e::seedGarment(m_db, "941", "h941a", "7.00", today()));
        RecogPrendas rp(m_db);
        const auto result = [&rp]() { return rp.findChild<QLabel *>("lblResult")->text(); };

        QVERIFY(selectRow(rp, "940", "h940a"));
        rp.findChild<QPushButton *>("pb_separ_garm")->click();
        QVERIFY2(result().contains("bloqueado") && !result().contains("separadas"), qPrintable(result()));
        QCOMPARE(scalar("SELECT COUNT(*) || '|' || MAX(cantidad) FROM ingresos WHERE n_recibo='940'"),
                 QStringLiteral("1|3"));

        QVERIFY(selectRow(rp, "941", "h941a"));
        rp.findChild<QLineEdit *>("le_price")->setText("-5");
        QMetaObject::invokeMethod(&rp, "on_le_price_editingFinished");
        QVERIFY2(result().contains("negativo"), qPrintable(result()));
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash='h941a'"), QStringLiteral("7.00"));
    }

    // Cobrar stores the amount it shows and sends: an older row stored with three
    // decimals is charged, sent and kept as the same two-decimal figure.
    void test_payDialog_storesTheAmountItSends()
    {
        QVERIFY(E2e::seedGarment(m_db, "950", "h950a", "12.345", today()));
        PayDialog dlg(m_db);
        dlg.m_verifactu = m_verifactu;
        QVERIFY(dlg.loadTicket("950"));
        QMetaObject::invokeMethod(&dlg, "onCobrarClicked");
        QTRY_COMPARE_WITH_TIMEOUT(scalar("SELECT pagado FROM ingresos WHERE hash='h950a'"), QStringLiteral("SI"), 10000);
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash='h950a'"), QStringLiteral("12.35"));
        const auto creates = m_server.requestsTo("Create");
        QCOMPARE(creates.size(), 1);
        QCOMPARE(creates[0].json.value("TotalAmount").toDouble(), 12.35);
    }

    // A garment locked by Contabilidad while its invoice is on the way to AEAT is not
    // stored as paid, and the dialog counts it so Recogida can report it.
    void test_payDialog_reportsAGarmentLockedDuringTheSubmit()
    {
        QVERIFY(E2e::seedGarment(m_db, "955", "h955a", "5.00", today()));
        QVERIFY(E2e::seedGarment(m_db, "955", "h955b", "7.00", today()));
        PayDialog dlg(m_db);
        dlg.m_verifactu = m_verifactu;
        QVERIFY(dlg.loadTicket("955"));
        QMetaObject::invokeMethod(&dlg, "onCobrarClicked");
        // The lock check before submitting has passed; the reply has not arrived yet.
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET edit_lock = 1 WHERE hash='h955b'"));
        QTRY_COMPARE_WITH_TIMEOUT(dlg.result(), int(QDialog::Accepted), 10000);
        QCOMPARE(dlg.unstoredGarments(), 1);
        QCOMPARE(scalar("SELECT pagado FROM ingresos WHERE hash='h955a'"), QStringLiteral("SI"));
        QCOMPARE(scalar("SELECT pagado FROM ingresos WHERE hash='h955b'"), QStringLiteral("NO"));
    }

    // A ticket whose garments cannot all be stored keeps none of them (a retry would
    // otherwise duplicate the first ones), is not sent to AEAT nor printed, and the
    // window says so instead of announcing it as saved.
    void test_mainWindow_failedInsertStopsTheSave()
    {
        MainWindow mw;
        mw.findChild<QComboBox *>("cb_client")->setCurrentText("Cliente Fallo");
        enterGarment(mw, "Camisa", "1");
        auto *table = mw.findChild<QTableWidget *>("table_ticket");
        table->setCurrentCell(1, 1);
        table->setItem(1, 0, new QTableWidgetItem("1"));
        qobject_cast<QComboBox *>(table->cellWidget(1, 1))->setCurrentText("Camisa");
        mw.findChild<QCheckBox *>("pb_payment")->setChecked(true);
        // The first garment goes in, the second one is refused.
        QVERIFY(E2e::exec(m_db, "CREATE TRIGGER e2e_block BEFORE INSERT ON ingresos "
                                "WHEN (SELECT COUNT(*) FROM ingresos) >= 1 "
                                "BEGIN SELECT RAISE(ABORT, 'blocked by test'); END"));
        clickSave(mw);
        E2e::exec(m_db, "DROP TRIGGER e2e_block");
        const QString result = mw.findChild<QLabel *>("lblResult")->text();
        QVERIFY2(result.contains("No se pudo guardar el ticket") && !result.contains("guardado:"), qPrintable(result));
        QVERIFY(m_server.requestsTo("Create").isEmpty());
        QCOMPARE(scalar("SELECT COUNT(*) FROM ingresos"), QStringLiteral("0"));
    }

    // Cobrar never charges an m2 garment that has no size: its invoice amount would be
    // 0 and frozen. Refused in the dialog; nothing paid, nothing sent.
    void test_payDialog_refusesUnmeasuredM2Garment()
    {
        QVERIFY(E2e::seedGarment(m_db, "930", "h930a", "0.00", today()));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET prenda = 'Jarapa (m2)', size = '' WHERE hash='h930a'"));
        PayDialog dlg(m_db);
        dlg.m_verifactu = m_verifactu;
        QVERIFY(dlg.loadTicket("930"));
        QMetaObject::invokeMethod(&dlg, "onCobrarClicked");
        QVERIFY2(dlg.findChild<QLabel *>("lblResult")->text().contains("no tiene tamaño"),
                 qPrintable(dlg.findChild<QLabel *>("lblResult")->text()));
        QCOMPARE(scalar("SELECT pagado FROM ingresos WHERE hash='h930a'"), QStringLiteral("NO"));
        QVERIFY(m_server.requestsTo("Create").isEmpty());
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
        QTRY_VERIFY(rp.findChild<QLabel *>("lblResult")->text().contains("se ha actualizado con el CSV de AEAT"));
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

    // Herramientas -> Exportar registros AEAT, as the operator uses it: the file name
    // follows the dates, a reversed range is refused, the export is reported in the
    // window with the record count and a link to the file. No pop-ups.
    void test_aeatExportDialog_exportsAndReportsInWindow()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "2800", "h2800a", "10.00", "05-03-2026", "CSV-2800"));
        QPointer<AeatExportDialog> dlg = new AeatExportDialog(m_db);
        const auto result = [&dlg]() { return dlg->findChild<QLabel *>("lblResult")->text(); };
        dlg->findChild<QDateEdit *>("deFrom")->setDate(QDate(2026, 3, 31));
        dlg->findChild<QDateEdit *>("deTo")->setDate(QDate(2026, 3, 1));
        dlg->findChild<QPushButton *>("btnExport")->click();
        QVERIFY2(result().contains("anterior o igual"), qPrintable(result()));

        dlg->findChild<QDateEdit *>("deFrom")->setDate(QDate(2026, 3, 1));
        dlg->findChild<QDateEdit *>("deTo")->setDate(QDate(2026, 3, 31));
        const QString file = dlg->findChild<QLineEdit *>("leFile")->text();
        QVERIFY2(file.endsWith("AEAT/aeat_registros_20260301_20260331.xml"), qPrintable(file));
        QFile::remove(file);
        dlg->findChild<QPushButton *>("btnExport")->click();
        QVERIFY2(result().contains("1 registros exportados"), qPrintable(result()));
        QVERIFY(result().contains("aeat_registros_20260301_20260331.xml"));
        QVERIFY(QFile::exists(file));
        QVERIFY2(m_popups->messages().isEmpty(), qPrintable(m_popups->messages().join(" | ")));
        dlg->findChild<QPushButton *>("btnClose")->click();
        QTRY_VERIFY(dlg.isNull());
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
        auto *count = rp.findChild<QSpinBox *>("sb_separ");
        QCOMPARE(count->maximum(), 2);                                      // 3 garments: up to 2 split off
        count->setValue(1);
        rp.findChild<QPushButton *>("pb_separ_garm")->click();
        QVERIFY2(rp.findChild<QLabel *>("lblResult")->text().contains("1 prenda(s) separadas"),
                 qPrintable(rp.findChild<QLabel *>("lblResult")->text()));
        QCOMPARE(scalar("SELECT GROUP_CONCAT(cantidad || ':' || importe || ':' || verifactu_estado || ':' || "
                        "verifactu_csv || ':' || verifactu_invoice_seq || ':' || verifactu_invoice_id, ' ') "
                        "FROM (SELECT * FROM ingresos WHERE n_recibo = '2500' ORDER BY CAST(cantidad AS INTEGER))"),
                 QStringLiteral("1:3.33:ENVIADA:CSV-2500:1:2500-1 2:6.67:ENVIADA:CSV-2500:1:2500-1"));
    }

    // Recogida, an m2 garment measured at pickup: 2,99 m2 of a 9.50 garment is
    // stored as 28.41, the two-decimal amount AEAT will receive, not 28.405.
    void test_recogida_m2SizeStoredInCents()
    {
        QVERIFY(E2e::exec(m_db, "INSERT INTO prendas (nombre, precio_limpieza, precio_plancha) "
                                "VALUES ('Jarapa (m2)', '9.5', '0')"));
        QVERIFY(E2e::seedGarment(m_db, "2700", "h2700a", "0.00", today()));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET prenda = 'Jarapa (m2)' WHERE hash='h2700a'"));
        RecogPrendas rp(m_db);
        QVERIFY(selectRow(rp, "2700", "h2700a"));
        rp.findChild<QLineEdit *>("le_size")->setText("0");               // not measured yet: not priced
        QMetaObject::invokeMethod(&rp, "on_le_size_editingFinished");
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash='h2700a'"), QStringLiteral("0.00"));
        rp.findChild<QLineEdit *>("le_size")->setText("2,99");
        QMetaObject::invokeMethod(&rp, "on_le_size_editingFinished");

        QCOMPARE(scalar("SELECT size || '|' || importe FROM ingresos WHERE hash='h2700a'"),
                 QStringLiteral("2.99|28.41"));
        QCOMPARE(rp.findChild<QLineEdit *>("le_price")->text(), QStringLiteral("28.41"));
        E2e::exec(m_db, "DELETE FROM prendas WHERE nombre = 'Jarapa (m2)'");
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
        add->findChild<QLineEdit *>("leNRecibo")->setText("2000");
        add->findChild<QPushButton *>("btnSearch")->click();
        QVERIFY(add->ticketFound);
        add->findChild<QComboBox *>("cbPrenda")->setCurrentText("Camisa");
        add->findChild<QLineEdit *>("leCantidad")->setText("2");
        add->findChild<QCheckBox *>("chkPagado")->setChecked(true);
        add->findChild<QPushButton *>("btnSave")->click();
        const QString result = add->findChild<QLabel *>("lblResult")->text();
        QVERIFY2(result.contains("Prenda añadida al recibo Nº 2000") && result.contains("se envía a AEAT"),
                 qPrintable(result));
        QVERIFY(!add->ticketFound);                                        // ready for another receipt
        QVERIFY2(m_popups->messages().isEmpty(), qPrintable(m_popups->messages().join(" | ")));

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
        add->findChild<QLineEdit *>("leNRecibo")->setText("2100");
        add->findChild<QPushButton *>("btnSearch")->click();
        QVERIFY(add->ticketFound);
        add->findChild<QLineEdit *>("leNRecibo")->setText("2200");
        add->findChild<QComboBox *>("cbPrenda")->setCurrentText("Camisa");
        add->findChild<QLineEdit *>("leCantidad")->setText("1");
        add->findChild<QCheckBox *>("chkPagado")->setChecked(true);
        add->findChild<QPushButton *>("btnSave")->click();

        QVERIFY(add->findChild<QLabel *>("lblResult")->text().contains("No se ha buscado"));
        // Searching the sent ticket itself is refused before the form opens.
        add->findChild<QPushButton *>("btnSearch")->click();
        QVERIFY(!add->ticketFound);
        QVERIFY(add->findChild<QLabel *>("lblResult")->text().contains("ya tiene prendas pagadas"));
        QTest::qWait(300);
        QCOMPARE(scalar("SELECT COUNT(*) FROM ingresos WHERE n_recibo='2200'"), QStringLiteral("1"));
        QCOMPARE(scalar("SELECT verifactu_estado || '|' || verifactu_csv FROM ingresos WHERE hash='h2200a'"),
                 QStringLiteral("ENVIADA|A-ORIG2200"));
        QVERIFY(m_server.requestsTo("Create").isEmpty());
    }

    // Formulario de facturas de gastos: the IVA split follows the amount; a complete
    // invoice is saved and reported in the window; an unknown supplier and a date in
    // a closed quarter are refused there too, with nothing written. No pop-ups.
    void test_facturas_saveAndRefusals()
    {
        QVERIFY(E2e::exec(m_db, "INSERT INTO proveedores VALUES ('Proveedor E2E', 'B1', '', ''), "
                                "('Zurita', 'B2', '', ''), ('álvarez', 'B3', '', ''), ('Beta', 'B4', '', '')"));
        QVERIFY(E2e::exec(m_db, "INSERT INTO servicios VALUES ('Luz'), ('agua'), ('Gas')"));
        QVERIFY(E2e::exec(m_db, "INSERT INTO gastos (id, n_factura, servicio, descripcion, empresa, fecha, "
                                "importe, iva, edit_lock) VALUES (1, 'OLD', 'Luz', '', 'Proveedor E2E', "
                                "'10-01-2026', '10.00', 21, 1)"));
        QPointer<Facturas> form = new Facturas(m_db);
        form->populateEmpresas();
        form->populateServicios();
        const auto result = [&form]() { return form->findChild<QLabel *>("lblResult")->text(); };
        // Both lists in Spanish alphabetical order: case-insensitive, accents in place.
        const auto items = [](QComboBox *cb) {
            QStringList l;
            for (int i = 0; i < cb->count(); ++i)
                l << cb->itemText(i);
            return l;
        };
        QCOMPARE(items(form->findChild<QComboBox *>("cbEmpresa")),
                 QStringList({ "álvarez", "Beta", "Proveedor E2E", "Zurita" }));
        QCOMPARE(items(form->findChild<QComboBox *>("cbServicio")), QStringList({ "agua", "Gas", "Luz" }));
        const auto fill = [&form](const QString &empresa, const QDate &date) {
            form->findChild<QLineEdit *>("leFra")->setText("F-77");
            form->findChild<QDateEdit *>("deFecha")->setDate(date);
            form->findChild<QComboBox *>("cbEmpresa")->setCurrentText(empresa);
            form->findChild<QComboBox *>("cbServicio")->setCurrentText("Luz");
            auto *importe = form->findChild<QLineEdit *>("leImporte");
            importe->setText("121,00");
            emit importe->textEdited(importe->text());
        };

        fill("Proveedor E2E", QDate::currentDate());
        QCOMPARE(form->findChild<QLineEdit *>("leBase")->text(), QStringLiteral("100.00"));
        QCOMPARE(form->findChild<QLineEdit *>("leIva")->text(), QStringLiteral("21.00"));
        form->findChild<QPushButton *>("btnSave")->click();
        QVERIFY2(result().contains("Factura F-77 de Proveedor E2E guardada"), qPrintable(result()));
        QCOMPARE(scalar("SELECT importe || '|' || iva || '|' || edit_lock FROM gastos WHERE n_factura = 'F-77'"),
                 QStringLiteral("121.00|21|0"));
        QVERIFY(form->findChild<QLineEdit *>("leFra")->text().isEmpty());          // ready for the next one

        fill("Otro", QDate::currentDate());
        form->findChild<QPushButton *>("btnSave")->click();
        QVERIFY2(result().contains("no está en la lista de proveedores"), qPrintable(result()));
        fill("Proveedor E2E", QDate(2026, 1, 20));
        form->findChild<QPushButton *>("btnSave")->click();
        QVERIFY2(result().contains("Trimestre bloqueado"), qPrintable(result()));
        QCOMPARE(scalar("SELECT COUNT(*) FROM gastos WHERE n_factura = 'F-77'"), QStringLiteral("1"));
        QVERIFY2(m_popups->messages().isEmpty(), qPrintable(m_popups->messages().join(" | ")));

        form->findChild<QPushButton *>("btnClose")->click();
        QTRY_VERIFY(form.isNull());
        E2e::exec(m_db, "DELETE FROM proveedores");
        E2e::exec(m_db, "DELETE FROM servicios");
    }

    // Listado de gastos -> Generar PDF: the year's expenses grouped by supplier, with
    // a subtotal per supplier, written to Listados/Gastos and linked in the window;
    // a year without expenses is reported there, nothing written. No pop-ups.
    void test_genListadoGastos_pdfAndEmptyYear()
    {
        QVERIFY(E2e::exec(m_db, "INSERT INTO gastos (id, n_factura, servicio, descripcion, empresa, fecha, "
                                "iva, importe, edit_lock) VALUES "
                                "(1, 'A-1', 'Luz', '', 'Beta', '10-02-2026', '21', '121.00', 1), "
                                "(2, 'A-2', 'Agua', '', 'Alfa', '11-02-2026', '10', '11.00', 0), "
                                "(3, 'A-3', 'Luz', '', 'Beta', '12-03-2025', '21', '50.00', 0)"));
        QPointer<GenListado> dlg = new GenListado(m_db);   // reads the years, then closes the connection
        // A SQLite model reads its rows from the live query, so open it after the dialog.
        QVERIFY(m_db.open());
        QSqlTableModel model(nullptr, m_db);
        model.setTable("gastos");
        QVERIFY2(model.select(), qPrintable(model.lastError().text()));
        dlg->model = &model;
        const auto result = [&dlg]() { return dlg->findChild<QLabel *>("lblResult")->text(); };
        dlg->findChild<QComboBox *>("cbYear")->setCurrentText("2026");
        dlg->findChild<QComboBox *>("cbGroup")->setCurrentText(C_PROVEEDORES);
        dlg->findChild<QPushButton *>("btnGenerate")->click();

        QVERIFY2(result().contains("Listado de gastos generado"), qPrintable(result()));
        const QString html = ReportHtml::lastReportHtml();
        QVERIFY(html.contains("A-1") && html.contains("A-2") && !html.contains("A-3"));   // only 2026
        QVERIFY(html.indexOf("Alfa") < html.indexOf("Beta"));                              // by supplier
        QVERIFY(html.contains(ReportHtml::formatEuro(121.0)) && html.contains("IMPORTE TOTAL"));
        const QString file = AppSettings::instance()->listadosGastosPath() + "/listado_gastos_"
                             + QDate::currentDate().toString("yyyy-MM-dd_")
                             + GenListado::filenameSuffix(C_PROVEEDORES, C_INCL_TODOS, false, "2026") + ".pdf";
        QVERIFY(QFile::exists(file));
        QVERIFY(result().contains(QFileInfo(file).fileName().toHtmlEscaped()));

        dlg->findChild<QComboBox *>("cbType")->setCurrentText(C_CONTAB_CERR);
        dlg->findChild<QComboBox *>("cbYear")->setCurrentText("2025");               // nothing closed there
        dlg->findChild<QPushButton *>("btnGenerate")->click();
        QVERIFY2(result().contains("No hay gastos"), qPrintable(result()));
        QVERIFY2(m_popups->messages().isEmpty(), qPrintable(m_popups->messages().join(" | ")));
        dlg->close();
        delete dlg;
    }

    // Listado windows: the buttons each table offers, and every message in the result
    // panel - a new gasto points to Formulario facturas, a row of a closed quarter is
    // not editable, a new client is entered in the Nuevo cliente form. No pop-ups.
    void test_listado_buttonsAndMessagesInWindow()
    {
        QVERIFY(E2e::exec(m_db, "INSERT INTO gastos (id, n_factura, servicio, descripcion, empresa, fecha, "
                                "iva, importe, edit_lock) VALUES (1, 'L-1', 'Luz', '', 'Beta', '10-02-2026', "
                                "'21', '121.00', 1)"));
        MainWindow mw;
        const auto listado = [&mw](const QString &name) {
            for (Listado *l : mw.findChildren<Listado *>())
                if (l->objectName() == name)
                    return l;
            return static_cast<Listado *>(nullptr);
        };
        const auto result = [](Listado *l) { return l->findChild<QLabel *>("lblResult")->text(); };

        QMetaObject::invokeMethod(&mw, "on_actionGastos_triggered");
        Listado *gastos = listado("Gastos");
        QVERIFY(gastos);
        QVERIFY(!gastos->findChild<QPushButton *>("btnAdd")->isVisibleTo(gastos));
        QVERIFY(gastos->findChild<QPushButton *>("btnPdf")->isVisibleTo(gastos));
        gastos->actionAnadir_fila->trigger();                               // the menu entry still works
        QVERIFY2(result(gastos).contains("Formulario facturas"), qPrintable(result(gastos)));
        emit gastos->table_listado->doubleClick(gastos->table_listado->model()->index(0, 1));
        QTRY_VERIFY2(result(gastos).contains("Edición bloqueada"), qPrintable(result(gastos)));
        // The locked row can be neither edited in place nor deleted.
        const QModelIndex locked = gastos->table_listado->model()->index(0, 1);
        QVERIFY(!(gastos->table_listado->model()->flags(locked) & Qt::ItemIsEditable));
        gastos->table_listado->setCurrentIndex(locked);
        gastos->actionEliminar_fila->trigger();
        QVERIFY2(result(gastos).contains("Fila bloqueada"), qPrintable(result(gastos)));
        QCOMPARE(scalar("SELECT COUNT(*) FROM gastos"), QStringLiteral("1"));

        QMetaObject::invokeMethod(&mw, "on_actionListado_de_clientes_triggered");
        Listado *clientes = listado("Listado de clientes");
        QVERIFY(clientes);
        QVERIFY(!clientes->findChild<QPushButton *>("btnPdf")->isVisibleTo(clientes));
        m_driver->expect(ModalDriver::ofType<InsertNewItem>(), [](QWidget *w) {
            w->findChild<QPushButton *>("btnSave")->click();             // no name: refused in the form
            QVERIFY(w->findChild<QLabel *>("lblResult")->text().contains("nombre"));
            w->findChild<QLineEdit *>("leName")->setText("Ana E2E");
            w->findChild<QLineEdit *>("leMobile")->setText("600000000");
            w->findChild<QPushButton *>("btnSave")->click();
        });
        clientes->findChild<QPushButton *>("btnAdd")->click();
        QCOMPARE(m_driver->handled(), 1);
        QCOMPARE(scalar("SELECT movil FROM clientes WHERE nombre = 'Ana E2E'"), QStringLiteral("600000000"));
        QVERIFY2(result(clientes).contains("Cliente añadido"), qPrintable(result(clientes)));
        QVERIFY2(m_popups->messages().isEmpty(), qPrintable(m_popups->messages().join(" | ")));
    }

    // Contabilidad trimestral: generating with "bloquear" writes the PDF and locks
    // the quarter's rows; Revertir contabilidad unlocks them again.
    void test_contabilidad_generateLockThenRevert()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "400", "h400a", "12.10", "10-02-2026", "A-ORIG0400"));
        const QString pdf = AppSettings::instance()->contabilidadPath()
                            + "/contabilidad_trimestral_2026_1.pdf";
        const QString detailedPdf = AppSettings::instance()->contabilidadPath()
                                    + "/contabilidad_trimestral_2026_1_detalle.pdf";
        QFile::remove(pdf);
        QFile::remove(detailedPdf);

        QPointer<Contabilidad> form = new Contabilidad(m_db);
        const auto result = [](Contabilidad *c) { return c->findChild<QLabel *>("lblResult")->text(); };
        form->findChild<QComboBox *>("cbConfig")->setCurrentIndex(1);    // Trimestral
        form->findChild<QSpinBox *>("sbPeriod")->setValue(1);
        form->findChild<QSpinBox *>("sbYear")->setValue(2026);
        form->findChild<QCheckBox *>("chkLock")->setChecked(true);
        form->findChild<QPushButton *>("btnGenerate")->click();

        QVERIFY(QFile::exists(pdf));
        QVERIFY(QFileInfo(pdf).size() > 0);
        QVERIFY(!QFile::exists(detailedPdf));                             // the detail is opt-in
        QVERIFY(!ReportHtml::lastReportHtml().contains("Detalle del periodo"));
        // Every outcome is shown in the window, never in a pop-up.
        QVERIFY2(result(form).contains("El trimestre se ha bloqueado"), qPrintable(result(form)));
        QVERIFY(result(form).contains("contabilidad_trimestral_2026_1.pdf"));
        QVERIFY2(m_popups->messages().isEmpty(), qPrintable(m_popups->messages().join(" | ")));
        QCOMPARE(scalar("SELECT edit_lock FROM ingresos WHERE hash='h400a'"), QStringLiteral("1"));

        // The dialog stays open: check the lock, then the detailed report, kept apart.
        QTest::qWait(50);
        QVERIFY(!form.isNull());
        form->findChild<QPushButton *>("btnCheckLock")->click();
        QVERIFY2(result(form).contains("El trimestre 1 de 2026 está bloqueado"), qPrintable(result(form)));
        form->findChild<QCheckBox *>("chkDetail")->setChecked(true);
        form->findChild<QPushButton *>("btnGenerate")->click();
        QVERIFY(QFile::exists(detailedPdf));
        QVERIFY(ReportHtml::lastReportHtml().contains("Detalle del periodo"));
        QVERIFY(ReportHtml::lastReportHtml().contains("Trimestre 1 · 2026 · Detalle"));
        QVERIFY(result(form).contains("ya estaba realizada"));
        QVERIFY(result(form).contains("contabilidad_trimestral_2026_1_detalle.pdf"));
        form->findChild<QPushButton *>("btnClose")->click();
        QTRY_VERIFY(form.isNull());                                       // Cerrar closes it (WA_DeleteOnClose)

        QPointer<Contabilidad> revert = new Contabilidad(m_db);
        revert->revertirOn = true;
        revert->resetAllContents();
        QCOMPARE(revert->windowTitle(), QStringLiteral("Revertir contabilidad"));
        QVERIFY(!revert->findChild<QCheckBox *>("chkDetail")->isEnabled());
        revert->findChild<QSpinBox *>("sbPeriod")->setValue(1);
        revert->findChild<QSpinBox *>("sbYear")->setValue(2026);
        revert->findChild<QPushButton *>("btnGenerate")->click();

        QVERIFY2(result(revert).contains("revertida"), qPrintable(result(revert)));
        QCOMPARE(scalar("SELECT edit_lock FROM ingresos WHERE hash='h400a'"), QStringLiteral("0"));
        revert->findChild<QPushButton *>("btnCheckLock")->click();
        QVERIFY(result(revert).contains("El trimestre 1 de 2026 no está bloqueado"));
        revert->findChild<QPushButton *>("btnGenerate")->click();        // nothing left to revert
        QVERIFY(result(revert).contains("no hay nada que revertir"));
        QVERIFY2(m_popups->messages().isEmpty(), qPrintable(m_popups->messages().join(" | ")));
        revert->close();
        QTRY_VERIFY(revert.isNull());
    }

    // A report that cannot be written (here its path is taken by a folder) is shown as
    // an error and the quarter is not closed without it.
    void test_contabilidad_unwrittenReportDoesNotLock()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "410", "h410a", "12.10", "10-05-2026", "A-ORIG0410"));
        const QString pdf = AppSettings::instance()->contabilidadPath() + "/contabilidad_trimestral_2026_2.pdf";
        QFile::remove(pdf);
        QVERIFY(QDir().mkpath(pdf));
        QPointer<Contabilidad> form = new Contabilidad(m_db);
        form->findChild<QComboBox *>("cbConfig")->setCurrentIndex(1);    // Trimestral
        form->findChild<QSpinBox *>("sbPeriod")->setValue(2);
        form->findChild<QSpinBox *>("sbYear")->setValue(2026);
        form->findChild<QCheckBox *>("chkLock")->setChecked(true);
        form->findChild<QPushButton *>("btnGenerate")->click();
        const QString text = form->findChild<QLabel *>("lblResult")->text();
        QDir(pdf).removeRecursively();
        QVERIFY2(text.contains("No se pudo guardar el informe") && !text.contains("realizada"), qPrintable(text));
        QCOMPARE(scalar("SELECT edit_lock FROM ingresos WHERE hash='h410a'"), QStringLiteral("0"));
        form->close();
        QTRY_VERIFY(form.isNull());
    }
};

QTEST_MAIN(TestE2eApp)
#include "test_e2e_app.moc"
