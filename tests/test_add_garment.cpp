// AddGarment (Tickets -> Anadir nuevas prendas) on a throwaway shop-shaped SQLite DB, offscreen:
// the validation order, the closed-quarter refusals, the m2 size, the row written for an
// unpaid / paid garment and the seq emitted with a paid one.

#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "add_garment.h"
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
    static void allowInvoicing(AddGarment &dlg) { dlg.invoicingBlocked = [] { return QString(); }; }
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

    void test_save_paidWithoutInvoicingCheck_refused()
    {
        seedTicket();
        AddGarment dlg(m_db);                                       // invoicingBlocked unset
        searchTicket(dlg);
        fillGarment(dlg);
        pay(dlg);
        save(dlg);
        QVERIFY2(panel(dlg).contains("Verifactu no está disponible"), qPrintable(panel(dlg)));
        dlg.invoicingBlocked = [] { return QStringLiteral("Motivo de bloqueo"); };
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
        dlg.aeatEnvironment = [] { return QStringLiteral("pruebas"); };
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
};

QTEST_MAIN(TestAddGarment)
#include "test_add_garment.moc"
