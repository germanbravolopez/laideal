// AddGarment (Tickets -> Anadir nuevas prendas) on a throwaway shop-shaped SQLite DB, offscreen:
// the validation order, the closed-quarter refusals, the m2 size, the row written for an
// unpaid / paid garment and the seq emitted with a paid one.

#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "add_garment.h"
#include "aeatreconcilesummary.cpp"   // Recogida's "Consultar en AEAT" text, a pure function: compiled in, this test does not link recog_prendas
#include "sql_lite.h"
#include "support/testschema.h"

namespace {
constexpr const char *kConn = "test_add_garment_conn";
}

class TestAddGarment : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QSqlDatabase  m_db;

    void exec(const QString &sql)
    {
        QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));
        QSqlQuery q(m_db);
        QVERIFY2(q.exec(sql), qPrintable(q.lastError().text()));
        m_db.close();
    }

    QString scalar(const QString &sql)
    {
        QString out;
        if (m_db.open()) {
            QSqlQuery q(m_db);
            if (q.exec(sql) && q.next())
                out = q.value(0).toString();
            m_db.close();
        }
        return out;
    }

    static QString day(const QDate &date) { return date.toString("dd-MM-yyyy"); }
    static int quarterOf(const QDate &date) { return (date.month() + 2) / 3; }

    // An unpaid ticket "100" of Ana received on `reception`.
    void seedTicket(const QDate &reception = QDate::currentDate())
    {
        exec("INSERT INTO ingresos (edit_lock, n_recibo, cliente, fecha_recepcion, importe, pagado, estado, "
             "cantidad, prenda, servicio, hash, verifactu_estado) VALUES ('0', '100', 'Ana', '" + day(reception)
             + "', '4.00', 'NO', 'En tienda', '1', 'Camisa', 'Limp.', 'seedhash', 'SIN COBRAR')");
    }

    void searchTicket(AddGarment &dlg, const QString &ticket = "100")
    {
        dlg.findChild<QLineEdit *>("leNRecibo")->setText(ticket);
        dlg.findChild<QPushButton *>("btnSearch")->click();
    }

    void fillGarment(AddGarment &dlg, const QString &garment = "Camisa", const QString &quantity = "2")
    {
        dlg.findChild<QComboBox *>("cbPrenda")->setCurrentText(garment);
        dlg.findChild<QLineEdit *>("leCantidad")->setText(quantity);
    }

    void pay(AddGarment &dlg, bool paid = true) { dlg.findChild<QCheckBox *>("chkPagado")->setChecked(paid); }
    void save(AddGarment &dlg) { dlg.findChild<QPushButton *>("btnSave")->click(); }
    QString panel(AddGarment &dlg) { return dlg.findChild<QLabel *>("lblResult")->text(); }
    static void allowInvoicing(AddGarment &dlg) { dlg.invoicing.invoicingBlocked = [] { return QString(); }; }
    int garmentRows() { return scalar("SELECT COUNT(*) FROM ingresos WHERE n_recibo = '100'").toInt(); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_db = QSqlDatabase::addDatabase("QSQLITE", kConn);
        m_db.setDatabaseName(m_dir.filePath("test.db"));
        QVERIFY(TestSchema::create(m_db));
    }

    void cleanupTestCase()
    {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(kConn);
    }

    void init()
    {
        exec("DELETE FROM ingresos");
        exec("DELETE FROM prendas");
        exec("INSERT INTO prendas (nombre, precio_limpieza, precio_plancha) VALUES "
             "('Camisa', '5.00', '3.50'), ('Alfombra m2', '10.00', '8.00')");
        exec("DROP TABLE IF EXISTS aeat_records");
        TestSchema::resetClosures(m_db);
    }

    // Validation order: the search comes first, then a ticket paid meanwhile, then the form.
    void test_save_withoutSearch_refused()
    {
        seedTicket();
        AddGarment dlg(m_db);
        save(dlg);
        QVERIFY2(panel(dlg).contains("No se ha buscado"), qPrintable(panel(dlg)));
        QCOMPARE(garmentRows(), 1);
    }

    void test_save_retypedNumber_refused()
    {
        seedTicket();
        AddGarment dlg(m_db);
        searchTicket(dlg);
        fillGarment(dlg);
        dlg.findChild<QLineEdit *>("leNRecibo")->setText("101");
        save(dlg);
        QVERIFY2(panel(dlg).contains("No se ha buscado"), qPrintable(panel(dlg)));
        QCOMPARE(garmentRows(), 1);
    }

    void test_save_ticketPaidMeanwhile_beforeFormChecks()
    {
        seedTicket();
        AddGarment dlg(m_db);
        searchTicket(dlg);
        exec("UPDATE ingresos SET pagado = 'SI' WHERE n_recibo = '100'");
        save(dlg);                                                  // the form is empty too
        QVERIFY2(panel(dlg).contains("ya tiene prendas pagadas"), qPrintable(panel(dlg)));
        QVERIFY(!panel(dlg).contains("Formulario incompleto"));
    }

    void test_save_incompleteForm_refused()
    {
        seedTicket();
        AddGarment dlg(m_db);
        searchTicket(dlg);
        QVERIFY(dlg.ticketFound());
        save(dlg);
        QVERIFY2(panel(dlg).contains("Formulario incompleto"), qPrintable(panel(dlg)));
        dlg.findChild<QComboBox *>("cbPrenda")->setCurrentText("Camisa");   // quantity still missing
        save(dlg);
        QVERIFY2(panel(dlg).contains("Formulario incompleto"), qPrintable(panel(dlg)));
        QCOMPARE(garmentRows(), 1);
    }

    // Closed quarters: the payment date refuses a paid garment only; the reception date
    // (the quarter Contabilidad groups the row under) refuses paid and unpaid.
    void test_save_closedQuarterOnPaymentDate_refusesPaidOnly()
    {
        seedTicket(QDate::currentDate().addYears(-1));
        const QDate today = QDate::currentDate();
        QVERIFY(closeQuarter(m_db, quarterOf(today), today.year(), ""));
        AddGarment dlg(m_db);
        allowInvoicing(dlg);
        searchTicket(dlg);
        fillGarment(dlg);
        pay(dlg);
        save(dlg);
        QVERIFY2(panel(dlg).contains("Trimestre bloqueado") && panel(dlg).contains("fecha de pago"),
                 qPrintable(panel(dlg)));
        QCOMPARE(garmentRows(), 1);
        pay(dlg, false);
        save(dlg);
        QVERIFY2(panel(dlg).contains("Prenda añadida"), qPrintable(panel(dlg)));
        QCOMPARE(garmentRows(), 2);
    }

    void test_save_closedQuarterOnReceptionDate_refusesPaidAndUnpaid()
    {
        const QDate reception = QDate::currentDate().addYears(-1);
        seedTicket(reception);
        QVERIFY(closeQuarter(m_db, quarterOf(reception), reception.year(), ""));
        AddGarment dlg(m_db);
        allowInvoicing(dlg);
        searchTicket(dlg);
        fillGarment(dlg);
        save(dlg);
        QVERIFY2(panel(dlg).contains("Trimestre bloqueado") && panel(dlg).contains("fecha de recepción"),
                 qPrintable(panel(dlg)));
        pay(dlg);
        save(dlg);
        QVERIFY2(panel(dlg).contains("fecha de recepción"), qPrintable(panel(dlg)));
        QCOMPARE(garmentRows(), 1);
        QVERIFY(reopenQuarter(m_db, quarterOf(reception), reception.year()));
        save(dlg);
        QVERIFY2(panel(dlg).contains("Prenda añadida"), qPrintable(panel(dlg)));
        QCOMPARE(garmentRows(), 2);
    }

    void test_save_m2GarmentWithoutSize_refused()
    {
        seedTicket();
        AddGarment dlg(m_db);
        searchTicket(dlg);
        fillGarment(dlg, "Alfombra m2", "1");
        save(dlg);
        QVERIFY2(panel(dlg).contains("m2"), qPrintable(panel(dlg)));
        QCOMPARE(garmentRows(), 1);
    }

    void test_save_m2GarmentCommaSize_storedWithDot()
    {
        seedTicket();
        AddGarment dlg(m_db);
        searchTicket(dlg);
        fillGarment(dlg, "Alfombra m2", "1");
        dlg.findChild<QLineEdit *>("leSize")->setText("2,5");
        QCOMPARE(dlg.findChild<QLineEdit *>("leImporte")->text(), displayMoney(25.0));
        save(dlg);
        QCOMPARE(scalar("SELECT size || '|' || importe FROM ingresos WHERE prenda = 'Alfombra m2'"),
                 QStringLiteral("2.5|25.00"));
    }

    void test_save_unpaid_rowShape()
    {
        seedTicket();
        AddGarment dlg(m_db);
        QSignalSpy saved(&dlg, &AddGarment::garmentSaved);
        QSignalSpy paid(&dlg, &AddGarment::paidGarmentSaved);
        searchTicket(dlg);
        fillGarment(dlg);
        save(dlg);
        QCOMPARE(saved.count(), 1);
        QCOMPARE(paid.count(), 0);
        QCOMPARE(scalar("SELECT pagado || '|' || fecha_pago || '|' || estado || '|' || verifactu_estado || '|' || "
                        "verifactu_invoice_seq || '|' || edit_lock || '|' || length(hash) || '|' || importe || '|' || cliente "
                        "FROM ingresos WHERE hash != 'seedhash'"),
                 QStringLiteral("NO||En tienda|SIN COBRAR|0|0|16|10.00|Ana"));
    }

    // An unpaid save keeps the ticket loaded so the next garment goes to it; only the garment fields clear.
    void test_save_unpaid_keepsTicketForNextGarment()
    {
        seedTicket();
        AddGarment dlg(m_db);
        searchTicket(dlg);
        fillGarment(dlg);
        save(dlg);
        QCOMPARE(garmentRows(), 2);
        QCOMPARE(dlg.findChild<QLineEdit *>("leNRecibo")->text(), QStringLiteral("100"));
        QCOMPARE(dlg.findChild<QLineEdit *>("leCliente")->text(), QStringLiteral("Ana"));
        QVERIFY(dlg.findChild<QLineEdit *>("leCantidad")->text().isEmpty());
        QVERIFY(dlg.findChild<QComboBox *>("cbPrenda")->currentText().isEmpty());
        fillGarment(dlg, "Camisa", "1");
        save(dlg);
        QCOMPARE(garmentRows(), 3);
    }

    // A paid save still resets the form: the ticket is refused from then on.
    void test_save_paid_resetsTicket()
    {
        seedTicket();
        AddGarment dlg(m_db);
        allowInvoicing(dlg);
        searchTicket(dlg);
        fillGarment(dlg);
        pay(dlg);
        save(dlg);
        QCOMPARE(garmentRows(), 2);
        QVERIFY(dlg.findChild<QLineEdit *>("leNRecibo")->text().isEmpty());
        fillGarment(dlg);
        save(dlg);
        QCOMPARE(garmentRows(), 2);
    }

    void test_save_paidWithoutInvoicingCheck_refused()
    {
        seedTicket();
        AddGarment dlg(m_db);                                       // invoicing.invoicingBlocked unset
        searchTicket(dlg);
        fillGarment(dlg);
        pay(dlg);
        save(dlg);
        QVERIFY2(panel(dlg).contains("Verifactu no está disponible"), qPrintable(panel(dlg)));
        dlg.invoicing.invoicingBlocked = [] { return QStringLiteral("Motivo de bloqueo"); };
        save(dlg);
        QVERIFY2(panel(dlg).contains("Motivo de bloqueo"), qPrintable(panel(dlg)));
        QCOMPARE(garmentRows(), 1);
    }

    // Paid: PENDIENTE at the seq of its payment event, 0 for the ticket's first invoice.
    void test_save_paid_rowShape_andSeqZero()
    {
        seedTicket();
        AddGarment dlg(m_db);
        allowInvoicing(dlg);
        QSignalSpy paid(&dlg, &AddGarment::paidGarmentSaved);
        searchTicket(dlg);
        fillGarment(dlg);
        pay(dlg);
        save(dlg);
        QCOMPARE(paid.count(), 1);
        QCOMPARE(paid.at(0).at(0).toString(), QStringLiteral("100"));
        QCOMPARE(paid.at(0).at(1).toDate(), QDate::currentDate());
        QCOMPARE(paid.at(0).at(2).toDouble(), 10.0);
        QCOMPARE(paid.at(0).at(3).toInt(), 0);
        QCOMPARE(scalar("SELECT pagado || '|' || fecha_pago || '|' || verifactu_estado || '|' || verifactu_invoice_seq "
                        "FROM ingresos WHERE hash != 'seedhash'"),
                 "SI|" + day(QDate::currentDate()) + "|PENDIENTE|0");
    }

    // The invoice is dated by the payment, not the reception: a ticket received before
    // production and paid after it is accepted; a payment dated before it is refused.
    void test_save_paid_receivedBeforeProduction_paidAfter_accepted()
    {
        const QDate start = QDate::currentDate().addDays(-2);
        seedTicket(QDate::currentDate().addDays(-5));
        AddGarment dlg(m_db);
        allowInvoicing(dlg);
        dlg.invoicing.invoiceDateRefused = [start](const QDate &date) {
            return date < start ? QStringLiteral("Fecha anterior al paso a PRODUCCION") : QString();
        };
        dlg.invoicing.productionStartDate = [start] { return start; };
        searchTicket(dlg);
        fillGarment(dlg);
        pay(dlg);                                                   // paid today
        save(dlg);
        QVERIFY2(panel(dlg).contains("Prenda añadida"), qPrintable(panel(dlg)));
        QCOMPARE(garmentRows(), 2);
    }

    void test_save_paymentDateBeforeProduction_refusedAndNothingStored()
    {
        const QDate start = QDate::currentDate().addDays(-2);
        seedTicket(QDate::currentDate().addDays(-5));
        AddGarment dlg(m_db);
        allowInvoicing(dlg);
        dlg.invoicing.invoiceDateRefused = [start](const QDate &date) {
            return date < start ? QStringLiteral("Fecha anterior al paso a PRODUCCION") : QString();
        };
        dlg.invoicing.productionStartDate = [start] { return start; };
        searchTicket(dlg);
        auto *payDate = dlg.findChild<QDateEdit *>("deFechaPago");
        QCOMPARE(payDate->minimumDate(), start);                    // not offered earlier
        payDate->setMinimumDate(QDate::currentDate().addDays(-5));  // as if the limit were not there
        payDate->setDate(start.addDays(-1));
        fillGarment(dlg);
        pay(dlg);
        save(dlg);
        QVERIFY2(panel(dlg).contains("PRODUCCION"), qPrintable(panel(dlg)));
        QCOMPARE(garmentRows(), 1);
    }

    // A ticket without a readable reception date never keeps the previous search's minimum.
    void test_search_invalidReceptionDate_dropsThePreviousMinimum()
    {
        const QDate reception = QDate::currentDate().addDays(-5);
        seedTicket(reception);
        exec("INSERT INTO ingresos (edit_lock, n_recibo, cliente, fecha_recepcion, importe, pagado, estado, "
             "cantidad, prenda, servicio, hash, verifactu_estado) VALUES ('0', '101', 'Eva', 'nunca', '4.00', 'NO', "
             "'En tienda', '1', 'Camisa', 'Limp.', 'seedhash101', 'SIN COBRAR')");
        AddGarment dlg(m_db);
        searchTicket(dlg);
        auto *payDate = dlg.findChild<QDateEdit *>("deFechaPago");
        auto *pickupDate = dlg.findChild<QDateEdit *>("deFechaRecogida");
        QCOMPARE(payDate->minimumDate(), reception);
        searchTicket(dlg, "101");
        QVERIFY2(dlg.ticketFound(), qPrintable(panel(dlg)));
        QVERIFY(payDate->minimumDate() < reception);
        QVERIFY(pickupDate->minimumDate() < reception);
    }

    // The invoice number or the paid check cannot be read: refused, with its own message, nothing stored.
    void test_save_unreadableInvoiceNumber_refusedNothingStored()
    {
        seedTicket();
        AddGarment dlg(m_db);
        allowInvoicing(dlg);
        QSignalSpy paid(&dlg, &AddGarment::paidGarmentSaved);
        searchTicket(dlg);
        fillGarment(dlg);
        pay(dlg);
        exec("ALTER TABLE ingresos RENAME COLUMN verifactu_invoice_seq TO verifactu_invoice_seq_off");
        save(dlg);
        const QString text = panel(dlg);
        exec("ALTER TABLE ingresos RENAME COLUMN verifactu_invoice_seq_off TO verifactu_invoice_seq");
        QVERIFY2(text.contains("numeración"), qPrintable(text));
        QCOMPARE(garmentRows(), 1);
        QCOMPARE(paid.count(), 0);
    }

    void test_unreadablePaidCheck_isNotReportedAsPaid()
    {
        seedTicket();
        AddGarment dlg(m_db);
        allowInvoicing(dlg);
        searchTicket(dlg);
        fillGarment(dlg);
        exec("ALTER TABLE ingresos RENAME COLUMN pagado TO pagado_off");
        save(dlg);
        const QString atSave = panel(dlg);
        searchTicket(dlg);
        const QString atSearch = panel(dlg);
        const bool found = dlg.ticketFound();
        exec("ALTER TABLE ingresos RENAME COLUMN pagado_off TO pagado");
        QVERIFY2(atSave.contains("No se pudo comprobar el recibo") && !atSave.contains("ya tiene prendas pagadas"),
                 qPrintable(atSave));
        QVERIFY2(atSearch.contains("No se pudo comprobar el recibo") && !atSearch.contains("ya tiene prendas pagadas"),
                 qPrintable(atSearch));
        QVERIFY(!found);
        QCOMPARE(garmentRows(), 1);
    }

    void test_search_unreadableHeader_isNotReportedAsNotFound()
    {
        seedTicket();
        AddGarment dlg(m_db);
        exec("ALTER TABLE ingresos RENAME COLUMN cliente TO cliente_off");
        searchTicket(dlg);
        const QString text = panel(dlg);
        const bool found = dlg.ticketFound();
        exec("ALTER TABLE ingresos RENAME COLUMN cliente_off TO cliente");
        QVERIFY2(text.contains("No se pudo comprobar el recibo") && !text.contains("No se ha encontrado"),
                 qPrintable(text));
        QVERIFY(!found);
    }

    void test_search_emptyNumber_saysItIsMissing()
    {
        AddGarment dlg(m_db);
        searchTicket(dlg, "  ");
        QVERIFY2(panel(dlg).contains("Falta el Nº de recibo") && !panel(dlg).contains("No se ha encontrado"),
                 qPrintable(panel(dlg)));
    }

    // A number the direct client already used for an invoice of the ticket is skipped, and
    // the same seq is on the row and in the signal (the receivers submit "<n>-<seq>").
    void test_save_paid_skipsNumbersUsedAtAeat()
    {
        seedTicket();
        exec("CREATE TABLE aeat_records (id INTEGER PRIMARY KEY, environment TEXT, kind TEXT, invoice_number TEXT)");
        exec("INSERT INTO aeat_records (environment, kind, invoice_number) VALUES "
             "('pruebas', 'alta', '100'), ('produccion', 'alta', '100-4')");
        AddGarment dlg(m_db);
        allowInvoicing(dlg);
        dlg.invoicing.aeatEnvironment = [] { return QStringLiteral("pruebas"); };
        QSignalSpy paid(&dlg, &AddGarment::paidGarmentSaved);
        searchTicket(dlg);
        fillGarment(dlg);
        pay(dlg);
        save(dlg);
        QCOMPARE(paid.count(), 1);
        QCOMPARE(paid.at(0).at(3).toInt(), 1);
        QCOMPARE(scalar("SELECT verifactu_invoice_seq FROM ingresos WHERE hash != 'seedhash'"), QStringLiteral("1"));

        // Without an environment every record counts.
        exec("DELETE FROM ingresos WHERE hash != 'seedhash'");
        AddGarment all(m_db);
        allowInvoicing(all);
        QSignalSpy paidAll(&all, &AddGarment::paidGarmentSaved);
        searchTicket(all);
        fillGarment(all);
        pay(all);
        save(all);
        QCOMPARE(paidAll.count(), 1);
        QCOMPARE(paidAll.at(0).at(3).toInt(), 5);
    }

    // The text above the "Consultar en AEAT" table, one case per branch.
    void test_aeatReconcileSummary_everyCase()
    {
        VerifactuRemoteRecord rec;
        const auto summary = [&](bool matches, bool predates) {
            return aeatReconcileSummaryHtml(rec, matches, "24417-0", "10-02-2026", predates, "01-07-2026");
        };
        // The answer could not be read: not evidence of absence (the other flags do not matter).
        QCOMPARE(summary(true, true),
                 QString("<b>No se ha podido interpretar la respuesta de AEAT.</b><br>"
                         "Esto <i>no</i> significa que la factura no esté registrada. "
                         "Revisa el detalle de abajo y consulta la sede electrónica."));
        rec.parsed = true;
        // Not found, issued before production: the expected absence.
        QCOMPARE(summary(false, true),
                 QString("<b>La AEAT real no tiene la factura 24417-0, y es lo esperado.</b><br>"
                         "Es del 10-02-2026, anterior al paso a PRODUCCIÓN (01-07-2026): se registró en el entorno de "
                         "pruebas o con IreneSolutions. No hay que reenviarla."));
        // Not found otherwise.
        QCOMPARE(summary(false, false),
                 QString("<b>AEAT no ha devuelto ninguna factura con el número 24417-0.</b><br>"
                         "Lo más probable es que no llegara a registrarse. Aun así, "
                         "antes de reenviar conviene confirmarlo en la sede electrónica "
                         "de la AEAT: si ya constara allí, el reenvío se rechazaría por "
                         "duplicado."));
        rec.found = true;
        rec.recordCount = 1;
        // Found but different from the ticket.
        QCOMPARE(summary(false, false),
                 QString("<b>AEAT tiene registrada esta factura.</b><br>"
                         "<span style='color:#b00'>Los datos NO coinciden con los del "
                         "ticket - no se puede actualizar automáticamente.</span>"));
        // Matches but no accepted record to take a CSV from.
        QCOMPARE(summary(true, false),
                 QString("<b>AEAT tiene registrada esta factura.</b><br>"
                         "<span style='color:#b00'>Ninguno de los registros fue aceptado "
                         "por AEAT (no hay CSV que recuperar).</span>"));
        // Matches with a CSV; several attempts are mentioned.
        rec.csv = "ABC123";
        QCOMPARE(summary(true, false),
                 QString("<b>AEAT tiene registrada esta factura.</b><br>Los datos coinciden con los del ticket."));
        rec.recordCount = 3;
        QCOMPARE(summary(true, false),
                 QString("<b>AEAT tiene registrada esta factura.</b><br>Los datos coinciden con los del ticket."
                         "<br><i>AEAT ha devuelto 3 registros para este número (los reenvíos "
                         "rechazados quedan guardados). Se muestran los datos del registro "
                         "aceptado.</i>"));
    }
};

QTEST_MAIN(TestAddGarment)
#include "test_add_garment.moc"
