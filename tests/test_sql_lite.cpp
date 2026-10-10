// Integration tests for the sql_lite free functions. Each test runs against a
// throwaway SQLite database created in a QTemporaryDir, with the tables and
// fixture rows the function under test reads. The functions open/close the
// passed QSqlDatabase themselves, so we just hand them a configured connection.
//
// Fixtures deliberately use only success paths and dot-decimal importes: several
// sql_lite functions pop a modal QMessageBox on error (bad table, comma decimal),
// which would hang a headless run.

#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QVariantMap>

#include "sql_lite.h"
#include "support/testschema.h"
#include "ingresos_schema.h"
#include "verifactutypes.h"   // VerifactuResult, for the updateTicketVerifactuFields tests

namespace {
constexpr const char *kConn = "test_sql_lite_conn";
}

class TestSqlLite : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QSqlDatabase  m_db;

    // Open the connection, run one statement, close. Fails the test on SQL error.
    void exec(const QString &sql, const QVariantMap &binds = {})
    {
        QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));
        QSqlQuery q(m_db);
        QVERIFY2(q.prepare(sql), qPrintable(q.lastError().text()));
        for (auto it = binds.constBegin(); it != binds.constEnd(); ++it)
            q.bindValue(it.key(), it.value());
        QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
        m_db.close();
    }

    // Insert one ingresos row. Only the columns the tests care about are
    // parameterised; the rest default to empty / 0.
    void insertIngreso(const QString &nRecibo, const QString &fechaPago,
                       const QString &importe, const QString &pagado,
                       const QString &verifactuEstado, int editLock = 0,
                       int invoiceSeq = 0)
    {
        exec("INSERT INTO ingresos "
             "(n_recibo, cliente, fecha_recepcion, fecha_pago, importe, pagado, "
             " estado, edit_lock, verifactu_estado, verifactu_invoice_seq) "
             "VALUES (:n, '', :frec, :fp, :imp, :pag, '', :lock, :est, :seq)",
             { {":n", nRecibo}, {":frec", fechaPago}, {":fp", fechaPago},
               {":imp", importe}, {":pag", pagado}, {":lock", editLock},
               {":est", verifactuEstado}, {":seq", invoiceSeq} });
    }

    // Read one scalar string off the test DB (first row, first column). Empty on
    // miss / error (logged) - the asserting test compares against the expected value.
    QString scalar(const QString &sql, const QVariantMap &binds = {})
    {
        if (!m_db.open()) {
            qWarning() << "scalar open:" << m_db.lastError().text();
            return QString();
        }
        QSqlQuery q(m_db);
        q.prepare(sql);
        for (auto it = binds.constBegin(); it != binds.constEnd(); ++it)
            q.bindValue(it.key(), it.value());
        QString out;
        if (!q.exec())
            qWarning() << "scalar exec:" << q.lastError().text();
        else if (q.next())
            out = q.value(0).toString();
        m_db.close();
        return out;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_db = QSqlDatabase::addDatabase("QSQLITE", kConn);
        m_db.setDatabaseName(m_dir.filePath("test.db"));
        QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));
        m_db.close();
        QVERIFY(TestSchema::create(m_db));
    }

    void cleanupTestCase()
    {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(kConn);
    }

    // Fresh tables before every test so cases don't bleed into each other.
    void init()
    {
        exec("DELETE FROM ingresos");
        exec("DELETE FROM gastos");
        exec("DELETE FROM clientes");
    }

    void test_genHash16_shapeAndUniqueness()
    {
        const QString h = genHash16();
        QCOMPARE(h.size(), 16);
        QVERIFY2(QRegularExpression("^[0-9a-f]{16}$").match(h).hasMatch(),
                 qPrintable(h));
        QVERIFY(genHash16() != genHash16()); // overwhelmingly likely to differ
    }

    void test_readMaxValueInColumnFromTable()
    {
        exec("INSERT INTO gastos (id, fecha, importe, iva) VALUES (1, '', '0', 21)");
        exec("INSERT INTO gastos (id, fecha, importe, iva) VALUES (5, '', '0', 21)");
        exec("INSERT INTO gastos (id, fecha, importe, iva) VALUES (3, '', '0', 21)");
        QCOMPARE(readMaxValueInColumnFromTable(m_db, "id", "gastos"), 5);
    }

    // incomeTicketsBetweenDates: only pagado='SI', excludes ANULADA and
    // RECTIFICADA, includes ENVIADA / legacy-empty, half-open [start, end).
    void test_incomeTickets_filters()
    {
        insertIngreso("T1", "10-03-2026", "100.00", "SI", "ENVIADA");
        insertIngreso("T2", "12-03-2026", "50.00",  "SI", "");          // legacy, included
        insertIngreso("T3", "13-03-2026", "30.00",  "SI", "ANULADA");   // excluded
        insertIngreso("T4", "14-03-2026", "20.00",  "SI", "RECTIFICADA"); // excluded
        insertIngreso("T5", "15-03-2026", "70.00",  "NO", "ENVIADA");   // unpaid, excluded
        insertIngreso("T6", "01-04-2026", "999.00", "SI", "ENVIADA");   // == end, half-open excludes

        const QVector<IncomeTicketDetail> tickets =
            incomeTicketsBetweenDates(m_db, QDate(2026, 3, 1), QDate(2026, 4, 1));
        QCOMPARE(tickets.size(), 2);
        double sum = 0.0;
        for (const IncomeTicketDetail &t : tickets)
            sum += t.importe;
        QVERIFY2(qAbs(sum - 150.0) < 0.01, qPrintable(QString::number(sum)));
    }

    // A comma-decimal importe is listed but never summed: flagged so the report
    // can warn, in both listings.
    void test_listings_flagCommaDecimalAmounts()
    {
        insertIngreso("T1", "10-03-2026", "10.00", "SI", "ENVIADA");
        insertIngreso("T1", "10-03-2026", "10,50", "SI", "ENVIADA");
        exec("INSERT INTO gastos (id, n_factura, fecha, importe, iva) VALUES (1, 'F-1', '11-03-2026', '12,10', 21)");
        const QDate start(2026, 3, 1), end(2026, 4, 1);

        const QVector<IncomeTicketDetail> tickets = incomeTicketsBetweenDates(m_db, start, end);
        QCOMPARE(tickets.size(), 1);
        QCOMPARE(tickets[0].garments, 2);
        QCOMPARE(tickets[0].invalidAmounts, 1);
        QVERIFY(qAbs(tickets[0].importe - 10.0) < 0.001);              // only the valid row

        const QVector<ExpenseDetail> expenses = expensesBetweenDates(m_db, start, end);
        QCOMPARE(expenses.size(), 1);
        QVERIFY(expenses[0].invalidAmount);
        QCOMPARE(expenses[0].importe, 0.0);
    }

    // One entry per paid ticket, ordered by payment date, with its garments summed.
    void test_incomeTicketsBetweenDates_groupsByTicket()
    {
        insertIngreso("12", "10-03-2026", "10.00", "SI", "ENVIADA");    // 2 garments, one ticket
        insertIngreso("12", "10-03-2026", "15.50", "SI", "ENVIADA");
        insertIngreso("9",  "05-03-2026", "50.00", "SI", "");           // legacy, earlier date
        insertIngreso("13", "13-03-2026", "30.00", "SI", "ANULADA");    // excluded
        insertIngreso("14", "14-03-2026", "20.00", "SI", "RECTIFICADA");// excluded
        insertIngreso("15", "15-03-2026", "70.00", "NO", "ANULADA");    // voided in place, excluded
        insertIngreso("16", "01-04-2026", "99.00", "SI", "ENVIADA");    // == end, excluded
        exec("UPDATE ingresos SET cliente = 'García' WHERE n_recibo = '12'");

        const QVector<IncomeTicketDetail> tickets =
            incomeTicketsBetweenDates(m_db, QDate(2026, 3, 1), QDate(2026, 4, 1));
        QCOMPARE(tickets.size(), 2);
        QCOMPARE(tickets[0].nRecibo, QStringLiteral("9"));             // ordered by payment date
        QCOMPARE(tickets[1].nRecibo, QStringLiteral("12"));
        QCOMPARE(tickets[1].garments, 2);
        QCOMPARE(tickets[1].cliente, QStringLiteral("García"));
        QCOMPARE(tickets[1].fechaPago, QStringLiteral("10-03-2026"));
        QVERIFY(qAbs(tickets[0].importe + tickets[1].importe - 75.50) < 0.01);
    }

    void test_expensesBetweenDates_listsPeriodRows()
    {
        exec("INSERT INTO gastos (id, n_factura, empresa, servicio, fecha, importe, iva) "
             "VALUES (1, 'F-2', 'Iberdrola', 'Luz', '20-03-2026', '121.00', 21)");
        exec("INSERT INTO gastos (id, n_factura, empresa, servicio, fecha, importe, iva) "
             "VALUES (2, 'F-1', 'Agua', 'Agua', '02-03-2026', '110.00', 10)");
        exec("INSERT INTO gastos (id, n_factura, empresa, servicio, fecha, importe, iva) "
             "VALUES (3, 'F-3', 'Seguro', 'Seguro', '15-03-2026', '40.00', 0)");
        exec("INSERT INTO gastos (id, n_factura, empresa, servicio, fecha, importe, iva) "
             "VALUES (4, 'F-4', 'Otro', 'Otro', '01-04-2026', '99.00', 21)");  // == end, excluded

        const QVector<ExpenseDetail> expenses =
            expensesBetweenDates(m_db, QDate(2026, 3, 1), QDate(2026, 4, 1));
        QCOMPARE(expenses.size(), 3);
        QCOMPARE(expenses[0].nFactura, QStringLiteral("F-1"));         // ordered by date
        QCOMPARE(expenses[1].nFactura, QStringLiteral("F-3"));
        QCOMPARE(expenses[2].empresa, QStringLiteral("Iberdrola"));
        QCOMPARE(expenses[2].iva, 21);
        QVERIFY(qAbs(expenses[2].importe - 121.0) < 0.001);
        QVERIFY(!expenses[2].invalidAmount);
    }

    void test_expensesBetweenDates_nullIvaIsNotSinIva()
    {
        exec("INSERT INTO gastos (id, n_factura, fecha, importe, iva) VALUES (1, 'F-1', '10-03-2026', '50.00', NULL)");
        exec("INSERT INTO gastos (id, n_factura, fecha, importe, iva) VALUES (2, 'F-2', '11-03-2026', '40.00', 0)");
        const QVector<ExpenseDetail> e = expensesBetweenDates(m_db, QDate(2026, 3, 1), QDate(2026, 4, 1));
        QCOMPARE(e.size(), 2);
        QCOMPARE(e[0].iva, -1);
        QCOMPARE(e[1].iva, 0);
    }

    // The annual one-scan listing must bucket exactly like four per-quarter calls,
    // including a ticket whose garments were paid in two different quarters.
    // An AEAT cancellation records fecha_anulacion on the cancelled payment event
    // only, whether or not its quarter was closed.
    void test_markInvoiceSeqCancelled_stampsTheCancelledEvent()
    {
        insertIngreso("40", "10-02-2026", "10.00", "SI", "ENVIADA", /*editLock=*/1, /*seq=*/1);
        insertIngreso("40", "20-04-2026", "10.00", "SI", "ENVIADA", /*editLock=*/0, /*seq=*/2);
        insertIngreso("41", "20-04-2026", "10.00", "SI", "ENVIADA", /*editLock=*/0, /*seq=*/1);

        QVERIFY(markInvoiceSeqCancelled(m_db, "40", 1, QDate(2026, 5, 15)));
        QVERIFY(markInvoiceSeqCancelled(m_db, "41", 1, QDate(2026, 5, 15)));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo='40' AND verifactu_invoice_seq=1"), QStringLiteral("ANULADA"));
        QCOMPARE(scalar("SELECT fecha_anulacion FROM ingresos WHERE n_recibo='40' AND verifactu_invoice_seq=1"), QStringLiteral("15-05-2026"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo='40' AND verifactu_invoice_seq=2"), QStringLiteral("ENVIADA"));
        QCOMPARE(scalar("SELECT COALESCE(fecha_anulacion, '') FROM ingresos WHERE n_recibo='40' AND verifactu_invoice_seq=2"), QString());
        QCOMPARE(scalar("SELECT fecha_anulacion FROM ingresos WHERE n_recibo='41'"), QStringLiteral("15-05-2026"));  // open quarter too

        // AEAT's cancellation record is stored on the event's paid rows, and a later
        // call without one never erases it.
        insertIngreso("42", "20-04-2026", "10.00", "SI", "ENVIADA", 0, /*seq=*/0);
        QVERIFY(markInvoiceSeqCancelled(m_db, "42", 0, QDate(2026, 5, 15), "<anulacion/>"));
        QVERIFY(markInvoiceSeqCancelled(m_db, "42", 0, QDate(2026, 5, 20)));
        QCOMPARE(scalar("SELECT verifactu_cancel_xml FROM ingresos WHERE n_recibo='42'"), QStringLiteral("<anulacion/>"));
        QCOMPARE(scalar("SELECT COALESCE(verifactu_cancel_xml, '') FROM ingresos WHERE n_recibo='41'"), QString());
    }

    // Cancelling / rectifying an invoice touches only the paid rows it covered: the
    // ticket's unpaid garments (same seq 0) stay chargeable and undated, so a later
    // payment is invoiced on its own and can never reach back into a closed quarter.
    void test_cancelAndRectify_leaveUnpaidGarmentsChargeable()
    {
        insertIngreso("80", "10-02-2026", "10.00", "SI", "ENVIADA", /*editLock=*/1, /*seq=*/0);
        insertRow("80", "h80u", "15.00", "NO", "SIN COBRAR");          // unpaid remainder, seq 0
        QVERIFY(markInvoiceSeqCancelled(m_db, "80", 0, QDate(2026, 5, 15)));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo='80' AND pagado='SI'"), QStringLiteral("ANULADA"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h80u'"), QStringLiteral("SIN COBRAR"));
        QCOMPARE(scalar("SELECT COALESCE(fecha_anulacion, '') FROM ingresos WHERE hash='h80u'"), QString());

        insertIngreso("81", "10-02-2026", "10.00", "SI", "ENVIADA");
        insertRow("81", "h81u", "15.00", "NO", "SIN COBRAR");
        QVERIFY(markTicketRectified(m_db, "81", QDate(2026, 6, 1)));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo='81' AND pagado='SI'"), QStringLiteral("RECTIFICADA"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h81u'"), QStringLiteral("SIN COBRAR"));
        QCOMPARE(scalar("SELECT COALESCE(fecha_anulacion, '') FROM ingresos WHERE hash='h81u'"), QString());
    }

    // A mark that matches no paid row is reported as a failure, not a silent success.
    void test_markFunctions_failWhenNoPaidRowMatches()
    {
        insertRow("85", "h85u", "10.00", "NO", "SIN COBRAR");
        QVERIFY(!markInvoiceSeqCancelled(m_db, "85", 0, QDate(2026, 5, 1)));
        QVERIFY(!markTicketRectified(m_db, "85", QDate(2026, 5, 1)));
        QVERIFY(!markInvoiceSeqCancelled(m_db, "NOPE", 0, QDate(2026, 5, 1)));
    }

    // Pre-10.12 cancellations also marked the unpaid remainder ANULADA; the migration
    // hands those garments back as SIN COBRAR. Local voids and RECTIFICADA rows (which
    // an older substitution may have invoiced) are left alone.
    void test_migrateDatabase_repairsUnpaidRemainderOfCancelledTicket()
    {
        insertIngreso("86", "10-02-2026", "10.00", "SI", "ANULADA");
        insertRow("86", "h86u", "15.00", "NO", "ANULADA");                // wrongly marked remainder
        insertRow("87", "h87u", "15.00", "NO", "RECTIFICADA");             // may be in a submitted substitution: kept
        exec("INSERT INTO ingresos (n_recibo, hash, pagado, estado, verifactu_estado) "
             "VALUES ('88', 'h88v', 'NO', 'Anulado', 'ANULADA')");           // genuine local void
        migrateDatabase(m_db);
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h86u'"), QStringLiteral("SIN COBRAR"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h87u'"), QStringLiteral("RECTIFICADA"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo='86' AND pagado='SI'"), QStringLiteral("ANULADA"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='h88v'"), QStringLiteral("ANULADA"));
    }

    void test_ticketLastPaymentDate()
    {
        insertIngreso("90", "10-02-2026", "10.00", "SI", "ENVIADA", 0, 1);
        insertIngreso("90", "05-04-2026", "10.00", "SI", "ENVIADA", 0, 2);
        insertRow("90", "h90u");                                      // unpaid: ignored
        QCOMPARE(ticketLastPaymentDate(m_db, "90"), QDate(2026, 4, 5));
        insertRow("91", "h91u");
        QVERIFY(!ticketLastPaymentDate(m_db, "91").isValid());          // nothing paid
    }

    void test_markTicketRectified_stampsRectificationDate()
    {
        insertIngreso("42", "10-02-2026", "10.00", "SI", "ENVIADA", /*editLock=*/1);
        insertIngreso("43", "10-04-2026", "10.00", "SI", "ENVIADA", /*editLock=*/0);
        QVERIFY(markTicketRectified(m_db, "42", QDate(2026, 6, 1)));
        QVERIFY(markTicketRectified(m_db, "43", QDate(2026, 6, 1)));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo='42'"), QStringLiteral("RECTIFICADA"));
        QCOMPARE(scalar("SELECT fecha_anulacion FROM ingresos WHERE n_recibo='42'"), QStringLiteral("01-06-2026"));
        QCOMPARE(scalar("SELECT fecha_anulacion FROM ingresos WHERE n_recibo='43'"), QStringLiteral("01-06-2026"));
    }

    // A paid ticket cancelled in a later quarter stays income in its payment quarter
    // (closed or not) and is a regularisation in the cancellation quarter; one
    // cancelled within its own quarter nets out there. A local void (unpaid) is
    // never income nor a regularisation.
    void test_regularizations_countWhereTheyHappen()
    {
        insertIngreso("50", "10-02-2026", "121.00", "SI", "ENVIADA", /*editLock=*/1, /*seq=*/0);  // Q1 closed
        insertIngreso("51", "12-02-2026", "50.00",  "SI", "ENVIADA", /*editLock=*/0, /*seq=*/0);  // Q1 open
        insertIngreso("52", "10-05-2026", "30.00",  "SI", "ENVIADA", /*editLock=*/0, /*seq=*/0);  // Q2, cancelled in Q2
        insertRow("53", "hashV");                                                                   // unpaid, voided
        QVERIFY(markInvoiceSeqCancelled(m_db, "50", 0, QDate(2026, 5, 15)));
        QVERIFY(markInvoiceSeqCancelled(m_db, "51", 0, QDate(2026, 5, 15)));
        QVERIFY(markInvoiceSeqCancelled(m_db, "52", 0, QDate(2026, 5, 20)));
        QVERIFY(voidGarmentRow(m_db, "53", "hashV"));

        const QDate q1s(2026, 1, 1), q2s(2026, 4, 1), q3s(2026, 7, 1);
        const QVector<IncomeTicketDetail> q1 = incomeTicketsBetweenDates(m_db, q1s, q2s);
        QCOMPARE(q1.size(), 2);                                         // payment quarter keeps 50 and 51
        QVERIFY(regularizationsBetweenDates(m_db, q1s, q2s).isEmpty());

        const QVector<IncomeTicketDetail> q2Income = incomeTicketsBetweenDates(m_db, q2s, q3s);
        QCOMPARE(q2Income.size(), 1);                                   // 52 is income in Q2...
        const QVector<RegularizationDetail> q2 = regularizationsBetweenDates(m_db, q2s, q3s);
        QCOMPARE(q2.size(), 3);                                         // ...and subtracted there; 53 never appears
        QCOMPARE(q2[0].nRecibo, QStringLiteral("50"));
        QCOMPARE(q2[0].fechaPago, QStringLiteral("10-02-2026"));
        QCOMPARE(q2[0].fechaAnulacion, QStringLiteral("15-05-2026"));
        QCOMPARE(q2[0].verifactuEstado, QStringLiteral("ANULADA"));
        QVERIFY(qAbs(q2[0].importe - 121.0) < 0.001);
        QCOMPARE(q2[2].nRecibo, QStringLiteral("52"));

        const QuarterlyDetails d = annualDetailsByQuarter(m_db, 2026);
        QCOMPARE(d.income[0].size(), 2);
        QCOMPARE(d.regularizations[0].size(), 0);
        QCOMPARE(d.regularizations[1].size(), 3);                       // bucketed by fecha_anulacion
    }

    // Garments voided before 10.12 carried the void date in fecha_pago and
    // fecha_recogida: the migration moves it to fecha_anulacion, once.
    void test_migrateDatabase_movesVoidDateToFechaAnulacion()
    {
        exec("INSERT INTO ingresos (n_recibo, hash, pagado, estado, verifactu_estado, fecha_pago, fecha_recogida) "
             "VALUES ('70', 'h70', 'NO', 'Anulado', 'ANULADA', '07-08-2026', '07-08-2026')");
        exec("INSERT INTO ingresos (n_recibo, hash, pagado, estado, verifactu_estado, fecha_pago, fecha_recogida) "
             "VALUES ('71', 'h71', 'SI', 'Recogido', 'ANULADA', '01-03-2026', '02-03-2026')");  // AEAT-cancelled: untouched
        exec("INSERT INTO ingresos (n_recibo, hash, pagado, estado, verifactu_estado, fecha_pago, fecha_recogida) "
             "VALUES ('72', 'h72', 'NO', 'Anulado', 'RECTIFICADA', '03-08-2026', '03-08-2026')");  // void later relabelled
        migrateDatabase(m_db);
        QCOMPARE(scalar("SELECT fecha_anulacion FROM ingresos WHERE n_recibo='72'"), QStringLiteral("03-08-2026"));
        QCOMPARE(scalar("SELECT fecha_pago FROM ingresos WHERE n_recibo='72'"), QString());
        QCOMPARE(scalar("SELECT fecha_anulacion FROM ingresos WHERE n_recibo='70'"), QStringLiteral("07-08-2026"));
        QCOMPARE(scalar("SELECT fecha_pago FROM ingresos WHERE n_recibo='70'"), QString());
        QCOMPARE(scalar("SELECT fecha_recogida FROM ingresos WHERE n_recibo='70'"), QString());
        QCOMPARE(scalar("SELECT fecha_pago FROM ingresos WHERE n_recibo='71'"), QStringLiteral("01-03-2026"));
        QCOMPARE(scalar("SELECT COALESCE(fecha_anulacion, '') FROM ingresos WHERE n_recibo='71'"), QString());

        exec("UPDATE ingresos SET fecha_pago = '09-09-2026' WHERE n_recibo = '70'");
        migrateDatabase(m_db);                                           // idempotent: date already set
        QCOMPARE(scalar("SELECT fecha_anulacion FROM ingresos WHERE n_recibo='70'"), QStringLiteral("07-08-2026"));
    }

    void test_annualDetailsByQuarter_matchesPerQuarterListings()
    {
        insertIngreso("20", "15-02-2026", "10.00", "SI", "ENVIADA");
        insertIngreso("21", "20-03-2026", "12.00", "SI", "");
        insertIngreso("21", "05-04-2026", "8.00",  "SI", "ENVIADA");     // same ticket, Q2 payment
        insertIngreso("22", "30-09-2026", "30.00", "SI", "ANULADA");     // excluded
        insertIngreso("23", "31-12-2026", "40.00", "SI", "ENVIADA");
        insertIngreso("24", "01-01-2027", "99.00", "SI", "ENVIADA");     // next year
        exec("INSERT INTO gastos (id, n_factura, fecha, importe, iva) VALUES (1, 'F-1', '03-01-2026', '121.00', 21)");
        exec("INSERT INTO gastos (id, n_factura, fecha, importe, iva) VALUES (2, 'F-2', '30-06-2026', '110.00', 10)");
        exec("INSERT INTO gastos (id, n_factura, fecha, importe, iva) VALUES (3, 'F-3', '01-10-2026', '20.00', 0)");

        const QuarterlyDetails d = annualDetailsByQuarter(m_db, 2026);
        for (int quarter = 1; quarter <= 4; quarter++) {
            const QDate start(2026, 3 * quarter - 2, 1);
            const QDate end = start.addMonths(3);
            const QVector<IncomeTicketDetail> inc = incomeTicketsBetweenDates(m_db, start, end);
            const QVector<ExpenseDetail> exp = expensesBetweenDates(m_db, start, end);
            QCOMPARE(d.income[quarter - 1].size(), inc.size());
            for (int k = 0; k < inc.size(); k++) {
                QCOMPARE(d.income[quarter - 1][k].nRecibo, inc[k].nRecibo);
                QCOMPARE(d.income[quarter - 1][k].importe, inc[k].importe);
                QCOMPARE(d.income[quarter - 1][k].garments, inc[k].garments);
            }
            QCOMPARE(d.expenses[quarter - 1].size(), exp.size());
            for (int k = 0; k < exp.size(); k++)
                QCOMPARE(d.expenses[quarter - 1][k].nFactura, exp[k].nFactura);
        }
        QCOMPARE(d.income[0].size(), 2);                                 // 20 and the Q1 half of 21
        QCOMPARE(d.income[1].size(), 1);                                 // the Q2 half of 21
        QCOMPARE(d.income[2].size(), 0);                                 // 22 is ANULADA
        QCOMPARE(d.income[3][0].nRecibo, QStringLiteral("23"));
        QCOMPARE(d.expenses[3][0].nFactura, QStringLiteral("F-3"));
    }


    void test_readLockForMonthAndYear()
    {
        // Note: unlike readLockForQuarter, this function returns 0 (not 2) for an
        // empty month - a valid-but-empty SELECT falls to `q.first() ? ... : 0`,
        // so "no data" and "open" are conflated here. Its only caller checks ==1,
        // so this is harmless; the contract is asserted as-is.
        QCOMPARE(readLockForMonthAndYear(m_db, "ingresos", 6, 2026), 0); // no data -> 0
        insertIngreso("T1", "15-06-2026", "10.00", "SI", "ENVIADA", /*editLock=*/0);
        QCOMPARE(readLockForMonthAndYear(m_db, "ingresos", 6, 2026), 0); // open
        exec("UPDATE ingresos SET edit_lock = 1 WHERE n_recibo = 'T1'");
        QCOMPARE(readLockForMonthAndYear(m_db, "ingresos", 6, 2026), 1); // locked

        // A locked month must read as locked even when an unlocked row sorts first.
        // Rows come back in rowid (insertion) order, so insert the unlocked row -
        // e.g. a garment voided into the month with edit_lock 0, see voidGarmentRow
        // stamping fecha_pago with the cancellation date - BEFORE the locked one:
        // a "first row wins" read would return 0 and let the locked month be edited.
        // COALESCE(MAX(edit_lock),0) is order-independent and reports the lock.
        insertIngreso("V1", "02-07-2026", "0.00", "NO", "ANULADA", /*editLock=*/0);
        insertIngreso("V2", "10-07-2026", "10.00", "SI", "ENVIADA", /*editLock=*/1);
        QCOMPARE(readLockForMonthAndYear(m_db, "ingresos", 7, 2026), 1); // locked despite order

        // The mirror corner case: the locked row sorts FIRST, an unlocked sibling
        // after it. Also must read locked - MAX is order-independent both ways, so
        // a single locked row is enough to report the month as locked (the safe
        // direction: a false-lock only blocks an edit, a false-open would let a
        // closed accounting period be modified). Not reachable through the app
        // (updateLockForMonth locks a whole month uniformly), but pinned so the
        // conservative contract can't silently regress.
        insertIngreso("W1", "05-08-2026", "10.00", "SI", "ENVIADA", /*editLock=*/1);
        insertIngreso("W2", "12-08-2026", "0.00", "NO", "ANULADA", /*editLock=*/0);
        QCOMPARE(readLockForMonthAndYear(m_db, "ingresos", 8, 2026), 1); // locked despite order
    }

    // The 9.1 regression guard: a quarter with income only in its first month
    // (nothing in the last month) must still report its real lock state, not 2.
    void test_readLockForQuarter_firstMonthOnly()
    {
        insertIngreso("T1", "15-01-2026", "10.00", "SI", "ENVIADA", /*editLock=*/1);
        QCOMPARE(readLockForQuarter(m_db, "ingresos", 1, 2026), 1); // Q1 locked
        QCOMPARE(readLockForQuarter(m_db, "ingresos", 2, 2026), 2); // Q2 no data
    }

    void test_readLockForQuarter_open()
    {
        insertIngreso("T1", "20-02-2026", "10.00", "SI", "ENVIADA", /*editLock=*/0);
        QCOMPARE(readLockForQuarter(m_db, "ingresos", 1, 2026), 0); // has data, open
    }

    // nextVerifactuInvoiceSeq = MAX(seq over paid rows) + 1, 0 when none paid.
    void test_nextVerifactuInvoiceSeq()
    {
        QCOMPARE(nextVerifactuInvoiceSeq(m_db, "T1"), 0); // no paid rows
        insertIngreso("T1", "10-03-2026", "10.00", "SI", "ENVIADA", 0, /*seq=*/0);
        QCOMPARE(nextVerifactuInvoiceSeq(m_db, "T1"), 1);
        insertIngreso("T1", "11-03-2026", "10.00", "SI", "ENVIADA", 0, /*seq=*/1);
        QCOMPARE(nextVerifactuInvoiceSeq(m_db, "T1"), 2);
        insertIngreso("T1", "", "10.00", "NO", "", 0, /*seq=*/9); // unpaid: ignored
        QCOMPARE(nextVerifactuInvoiceSeq(m_db, "T1"), 2);
    }

    // Pure price math (no DB): comma-decimal normalisation + size factor.
    void test_garmentImporte()
    {
        // quantity * unitPrice, no size
        QVERIFY(qAbs(garmentImporte("2", "", 5.0) - 10.0) < 0.001);
        // comma decimal in quantity (the 9.0 bug: must not parse as 0)
        QVERIFY(qAbs(garmentImporte("2,6", "", 5.0) - 13.0) < 0.001);
        // non-zero size factor (m2 garment), comma decimal in size
        QVERIFY(qAbs(garmentImporte("3", "2,5", 4.0) - 30.0) < 0.001);
        // size "0" -> no factor applied
        QVERIFY(qAbs(garmentImporte("3", "0", 4.0) - 12.0) < 0.001);
        // negative result clamps to 0
        QVERIFY(qAbs(garmentImporte("-1", "", 5.0)) < 0.001);
        // empty quantity -> 0
        QVERIFY(qAbs(garmentImporte("", "", 5.0)) < 0.001);
        // m2 garments land on half a cent: rounded up, as AEAT received them (ticket
        // 30723: 2.99 m2 x 9.50 = 28.405 -> 28.41), also from a float list price
        QCOMPARE(moneyText(garmentImporte("1", "2.99", 9.5)), QStringLiteral("28.41"));
        QCOMPARE(moneyText(garmentImporte("1", "2,37", 9.5)), QStringLiteral("22.52"));
        QCOMPARE(moneyText(garmentImporte("1", "1.25", double(3.3f))), QStringLiteral("4.13"));
    }

    // Unmeasured = priced by size (name with "m2") and no usable size yet.
    void test_garmentUnmeasured()
    {
        QVERIFY(garmentUnmeasured("Jarapa (m2)", ""));
        QVERIFY(garmentUnmeasured("Alfombra (m2)", "0"));
        QVERIFY(garmentUnmeasured("Kilim (m2)", " 0,0 "));
        QVERIFY(!garmentUnmeasured("Alfombra (m2)", "2,5"));
        QVERIFY(!garmentUnmeasured("Camisa", ""));
    }

    // Money is stored in cents, half away from zero, whatever binary error the
    // double carries (28.405 is 28.40499... as a double).
    void test_moneyText_roundsHalfAwayFromZero()
    {
        QCOMPARE(moneyText(28.405), QStringLiteral("28.41"));
        QCOMPARE(moneyText(22.515), QStringLiteral("22.52"));
        QCOMPARE(moneyText(16.625), QStringLiteral("16.63"));
        QCOMPARE(moneyText(36.0525), QStringLiteral("36.05"));
        QCOMPARE(moneyText(40.7265), QStringLiteral("40.73"));
        QCOMPARE(moneyText(-1.005), QStringLiteral("-1.01"));
        QCOMPARE(moneyText(10.0), QStringLiteral("10.00"));
        QCOMPARE(moneyText(QStringLiteral("10,5")), QStringLiteral("10.50"));
        QCOMPARE(moneyText(QStringLiteral(" 12.464 ")), QStringLiteral("12.46"));
        QCOMPARE(roundToCents(28.405), 28.41);
    }

    // Every amount writer stores two decimals, and none of them touches a row whose
    // amount AEAT holds (or a legacy paid row): that invoice changes only through
    // Anular / Rectificar factura. Ticket 30837 was re-priced after payment.
    void test_amountWriters_roundAndRefuseInvoicedRows()
    {
        insertRow("T1", "open", "10.00", "NO", "SIN COBRAR");
        QVERIFY(updateTicketSizeAndPrice(m_db, "T1", "open", "2.99", "28.405"));
        QCOMPARE(scalar("SELECT size || '|' || importe FROM ingresos WHERE hash='open'"), QStringLiteral("2.99|28.41"));
        QVERIFY(updateGarmentQtyAndImporte(m_db, "T1", "open", "2", "22,515"));
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash='open'"), QStringLiteral("22.52"));
        QVERIFY(updateGarmentServiceAndImporte(m_db, "T1", "open", "Plan.", "16.625"));
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash='open'"), QStringLiteral("16.63"));

        insertRow("T2", "sent", "30.00", "SI", "ENVIADA");
        insertRow("T2", "pend", "30.00", "SI", "PENDIENTE");
        insertRow("T2", "legacy", "30.00", "SI", "");
        for (const char *h : { "sent", "pend", "legacy" }) {
            QVERIFY(!updateTicketSizeAndPrice(m_db, "T2", h, "1", "36.00"));
            QVERIFY(!updateGarmentQtyAndImporte(m_db, "T2", h, "3", "36.00"));
            QVERIFY(!updateGarmentServiceAndImporte(m_db, "T2", h, "Plan.", "36.00"));
        }
        QCOMPARE(scalar("SELECT GROUP_CONCAT(importe || ':' || cantidad || ':' || servicio || ':' || size, ' ') "
                        "FROM ingresos WHERE n_recibo = 'T2'"),
                 QStringLiteral("30.00:1:Lavar:0 30.00:1:Lavar:0 30.00:1:Lavar:0"));
        // A negative amount is a correction (Rectificar factura), never a price.
        QVERIFY(!updateTicketSizeAndPrice(m_db, "T1", "open", "", "-5,00"));
        QVERIFY(!updateGarmentQtyAndImporte(m_db, "T1", "open", "1", "-1"));
        QVERIFY(!updateGarmentServiceAndImporte(m_db, "T1", "open", "Limp.", "-0.50"));
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash='open'"), QStringLiteral("16.63"));

        // A voided garment is frozen too, and so is a row locked by Contabilidad.
        insertRow("T3", "void", "5.00", "NO", "ANULADA");
        QVERIFY(!updateTicketSizeAndPrice(m_db, "T3", "void", "1", "6.00"));
        insertRow("T3", "locked", "5.00", "NO", "SIN COBRAR");
        exec("UPDATE ingresos SET edit_lock = 1 WHERE hash = 'locked'");
        QVERIFY(!updateGarmentQtyAndImporte(m_db, "T3", "locked", "2", "10.00"));
    }

    // The single source of truth for the AEAT InvoiceID format (used at submit,
    // persist, cancel and reprint). seq 0 -> bare n_recibo; seq>0 -> "<n>-<seq>".
    void test_verifactuInvoiceId()
    {
        QCOMPARE(verifactuInvoiceId("3245", 0), QStringLiteral("3245"));
        QCOMPARE(verifactuInvoiceId("3245", 1), QStringLiteral("3245-1"));
        QCOMPARE(verifactuInvoiceId("3245", 12), QStringLiteral("3245-12"));
    }

    // The printed InvoiceID must be the first NON-EMPTY literal across the ticket
    // rows, not row 0: a multi-event ticket can have an unpaid row 0 (empty id)
    // before the paid row that carries the real "<n>-<seq>" AEAT recorded.
    void test_verifactuDisplayInvoiceId()
    {
        // Row 0 empty, a later row carries the literal -> pick the literal, not the fallback.
        QCOMPARE(verifactuDisplayInvoiceId({"", "3245-1"}, "3245"), QStringLiteral("3245-1"));
        // First non-empty wins even when several are populated.
        QCOMPARE(verifactuDisplayInvoiceId({"3245-1", "3245-2"}, "3245"), QStringLiteral("3245-1"));
        // All empty (legacy 8.0-8.4 rows) -> bare n_recibo fallback.
        QCOMPARE(verifactuDisplayInvoiceId({"", ""}, "3245"), QStringLiteral("3245"));
        // No rows -> fallback.
        QCOMPARE(verifactuDisplayInvoiceId({}, "3245"), QStringLiteral("3245"));
    }

    // Accent/special-char stripper used for client-name matching (extracted from
    // MainWindow::removeSpecialChar). Case is preserved; literal '?' is dropped.
    void test_removeSpecialChars()
    {
        QCOMPARE(removeSpecialChars(QStringLiteral("José")),  QStringLiteral("Jose"));
        QCOMPARE(removeSpecialChars(QStringLiteral("Begoña")), QStringLiteral("Begona"));
        QCOMPARE(removeSpecialChars(QStringLiteral("Ángel")),  QStringLiteral("Angel"));
        QCOMPARE(removeSpecialChars(QStringLiteral("sin-acentos")), QStringLiteral("sin-acentos"));
        QCOMPARE(removeSpecialChars(QStringLiteral("a?b")), QStringLiteral("ab"));
    }

    // Startup recovery feed: one entry per (n_recibo, seq) still PENDIENTE on or
    // after the floor, each with its own SUM(importe). seq>0 partial-pay events
    // surface separately from the seq=0 save-time event (the seq filter is gone);
    // ENVIADA rows and rows before the floor are excluded.
    void test_pendingVerifactuEvents()
    {
        // T100 save-time event (seq 0): two PENDIENTE rows -> summed to 80.
        insertIngreso("T100", "10-03-2026", "50.00", "SI", "PENDIENTE", 0, /*seq=*/0);
        insertIngreso("T100", "10-03-2026", "30.00", "SI", "PENDIENTE", 0, /*seq=*/0);
        // T100 partial-pay event (seq 1): its own PENDIENTE row -> own total 20.
        insertIngreso("T100", "12-03-2026", "20.00", "SI", "PENDIENTE", 0, /*seq=*/1);
        // T100 seq 2 already confirmed by AEAT: excluded.
        insertIngreso("T100", "13-03-2026", "15.00", "SI", "ENVIADA",   0, /*seq=*/2);
        // T50 legacy empty estado: included.
        insertIngreso("T50",  "05-02-2026", "40.00", "SI", "",          0, /*seq=*/0);
        // T10 before the floor: excluded.
        insertIngreso("T10",  "20-12-2025", "99.00", "SI", "PENDIENTE", 0, /*seq=*/0);

        const QVector<PendingVerifactuEvent> ev = pendingVerifactuEvents(m_db, "2026-01-01");
        QCOMPARE(ev.size(), 3);
        // Ordered n_recibo DESC, then seq: "T50" > "T100" lexicographically.
        QCOMPARE(ev[0].nRecibo, QStringLiteral("T50"));
        QCOMPARE(ev[0].seq, 0);
        QVERIFY(qAbs(ev[0].importe - 40.0) < 0.01);
        QCOMPARE(ev[1].nRecibo, QStringLiteral("T100"));
        QCOMPARE(ev[1].seq, 0);
        QVERIFY2(qAbs(ev[1].importe - 80.0) < 0.01, qPrintable(QString::number(ev[1].importe)));
        QCOMPARE(ev[2].nRecibo, QStringLiteral("T100"));
        QCOMPARE(ev[2].seq, 1);
        QVERIFY(qAbs(ev[2].importe - 20.0) < 0.01);
        QCOMPARE(ev[2].fechaPago, QStringLiteral("12-03-2026"));
    }

    // Issue #43: MainWindow::saveTicket stamps every garment PENDIENTE, paid or
    // not, but only submits the paid ones - so an unpaid ticket sitting in the
    // shop is NOT an unreconciled AEAT submission and must never reach the
    // startup recovery dialog. Mirrors the corner case both ways: same ticket
    // number, one unpaid save-time event and one paid partial-pay event.
    void test_pendingVerifactuEvents_excludesUnpaid()
    {
        // Unpaid save-time rows: PENDIENTE, but no invoice was ever sent.
        insertIngreso("T200", "10-03-2026", "50.00", "NO", "PENDIENTE", 0, /*seq=*/0);
        insertIngreso("T200", "10-03-2026", "30.00", "NO", "PENDIENTE", 0, /*seq=*/0);
        // Paid partial-pay event on the same ticket: a genuine pending submission.
        insertIngreso("T200", "12-03-2026", "20.00", "SI", "PENDIENTE", 0, /*seq=*/1);
        // pagado='SI' but never stamped with a payment date: no AEAT date to
        // retry under, so it is not recoverable either.
        exec("INSERT INTO ingresos "
             "(n_recibo, cliente, fecha_recepcion, fecha_pago, importe, pagado, "
             " estado, edit_lock, verifactu_estado, verifactu_invoice_seq) "
             "VALUES ('T201', '', '10-03-2026', '', '40.00', 'SI', '', 0, 'PENDIENTE', 0)");

        const QVector<PendingVerifactuEvent> ev = pendingVerifactuEvents(m_db, "2026-01-01");
        QCOMPARE(ev.size(), 1);
        QCOMPARE(ev[0].nRecibo, QStringLiteral("T200"));
        QCOMPARE(ev[0].seq, 1);
        QVERIFY(qAbs(ev[0].importe - 20.0) < 0.01);
    }

    // The 10.9 Unpaid/NotSubmitted split ships a one-time backfill: rows that the
    // old saveTicket stamped PENDIENTE while unpaid are re-labelled SIN COBRAR.
    // Two boundaries are load-bearing and pinned here: a genuinely-pending paid row
    // must survive untouched (re-labelling it would hide a real unreconciled AEAT
    // submission from the recovery dialog), and a legacy blank row must stay blank
    // (several print/cancel queries detect split-off rows via `verifactu_estado
    // != ''`, so making it non-empty would change what gets printed).
    void test_migrateDatabase_backfillsUnpaidAsSinCobrar()
    {
        const QString estadoSql =
            "SELECT verifactu_estado FROM ingresos WHERE n_recibo = :n";

        insertIngreso("M1", "10-03-2026", "50.00", "NO", "PENDIENTE"); // unpaid -> re-labelled
        insertIngreso("M2", "10-03-2026", "50.00", "SI", "PENDIENTE"); // paid, really pending
        insertIngreso("M3", "10-03-2026", "50.00", "SI", "ENVIADA");   // confirmed
        // Legacy split-off row (blank estado) and a paid row that never got a
        // payment date (so it was never actually submitted).
        exec("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, importe, "
             "pagado, estado, edit_lock, verifactu_estado, verifactu_invoice_seq) "
             "VALUES ('M4', '', '10-03-2026', '', '50.00', 'NO', '', 0, '', 0)");
        exec("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, importe, "
             "pagado, estado, edit_lock, verifactu_estado, verifactu_invoice_seq) "
             "VALUES ('M5', '', '10-03-2026', '', '50.00', 'SI', '', 0, 'PENDIENTE', 0)");

        migrateDatabase(m_db);

        QCOMPARE(scalar(estadoSql, {{":n", "M1"}}), QStringLiteral("SIN COBRAR"));
        QCOMPARE(scalar(estadoSql, {{":n", "M2"}}), QStringLiteral("PENDIENTE"));
        QCOMPARE(scalar(estadoSql, {{":n", "M3"}}), QStringLiteral("ENVIADA"));
        QCOMPARE(scalar(estadoSql, {{":n", "M4"}}), QString());
        QCOMPARE(scalar(estadoSql, {{":n", "M5"}}), QStringLiteral("SIN COBRAR"));

        // Idempotent: re-running on an already-migrated DB is a no-op.
        migrateDatabase(m_db);
        QCOMPARE(scalar(estadoSql, {{":n", "M1"}}), QStringLiteral("SIN COBRAR"));
        QCOMPARE(scalar(estadoSql, {{":n", "M2"}}), QStringLiteral("PENDIENTE"));
        QCOMPARE(scalar(estadoSql, {{":n", "M4"}}), QString());
    }

    // An AEAT reply belongs only to the rows that were actually paid. This is not
    // hypothetical: nextVerifactuInvoiceSeq counts PAID rows, so a ticket's FIRST
    // partial payment gets seq 0 - which the still-unpaid siblings also carry.
    // Scoping the write-back by seq alone stamped those siblings ENVIADA + CSV for
    // an invoice that never covered them, which then made them non-voidable in
    // "Anular prendas" and fed a CSV into the print path. Pinned both ways: the
    // paid rows must be patched, the unpaid sibling must be left completely alone.
    void test_updateTicketVerifactuFields_leavesUnpaidSiblingsAlone()
    {
        insertIngreso("P1", "10-03-2026", "50.00", "SI", "PENDIENTE", 0, /*seq=*/0);
        insertIngreso("P1", "10-03-2026", "30.00", "SI", "PENDIENTE", 0, /*seq=*/0);
        insertIngreso("P1", "",           "20.00", "NO", "SIN COBRAR", 0, /*seq=*/0);

        VerifactuResult ok;
        ok.status        = VerifactuResult::SUCCESS;
        ok.csv           = "CSV-ABC123";
        ok.validationUrl = "https://aeat.example/validate";
        updateTicketVerifactuFields(m_db, "P1", ok, /*seq=*/0);

        QCOMPARE(scalar("SELECT COUNT(*) FROM ingresos WHERE n_recibo = 'P1' "
                        "AND verifactu_estado = 'ENVIADA'"), QStringLiteral("2"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo = 'P1' "
                        "AND pagado = 'NO'"), QStringLiteral("SIN COBRAR"));
        QCOMPARE(scalar("SELECT COALESCE(verifactu_csv, '') FROM ingresos "
                        "WHERE n_recibo = 'P1' AND pagado = 'NO'"), QString());
        QCOMPARE(scalar("SELECT COALESCE(verifactu_invoice_id, '') FROM ingresos "
                        "WHERE n_recibo = 'P1' AND pagado = 'NO'"), QString());
    }

    // The same scoping must hold for a failed submission: an AEAT error belongs to
    // the paid rows, and must not push an unpaid sibling into ERROR (which would
    // also surface a bogus "Reintentar envio" button on it in RecogPrendas).
    void test_updateTicketVerifactuFields_errorAlsoSkipsUnpaid()
    {
        insertIngreso("P2", "10-03-2026", "50.00", "SI", "PENDIENTE", 0, /*seq=*/0);
        insertIngreso("P2", "",           "20.00", "NO", "SIN COBRAR", 0, /*seq=*/0);

        VerifactuResult bad;
        bad.status           = VerifactuResult::ERROR;
        bad.errorDescription = "AEAT rejected";
        updateTicketVerifactuFields(m_db, "P2", bad, /*seq=*/0);

        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo = 'P2' "
                        "AND pagado = 'SI'"), QStringLiteral("ERROR"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo = 'P2' "
                        "AND pagado = 'NO'"), QStringLiteral("SIN COBRAR"));
    }

    // A dropped connection is not a rejection. The row must land PENDIENTE (so the
    // startup recovery dialog owns it) and KEEP its InvoiceID, because AEAT may
    // already hold the invoice under that identity and a retry has to reuse it.
    void test_updateTicketVerifactuFields_transportFailureStaysPending()
    {
        insertIngreso("P3", "10-03-2026", "50.00", "SI", "PENDIENTE", 0, /*seq=*/0);

        VerifactuResult dropped;
        dropped.status           = VerifactuResult::NETWORK_ERROR;
        dropped.errorDescription = "Tiempo de espera agotado";
        updateTicketVerifactuFields(m_db, "P3", dropped, /*seq=*/0);

        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo = 'P3'"),
                 QStringLiteral("PENDIENTE"));
        QCOMPARE(scalar("SELECT verifactu_invoice_id FROM ingresos WHERE n_recibo = 'P3'"),
                 QStringLiteral("P3"));
        QCOMPARE(scalar("SELECT verifactu_error FROM ingresos WHERE n_recibo = 'P3'"),
                 QStringLiteral("Tiempo de espera agotado"));
        // Still no CSV - nothing was confirmed.
        QCOMPARE(scalar("SELECT COALESCE(verifactu_csv, '') FROM ingresos "
                        "WHERE n_recibo = 'P3'"), QString());
    }

    // A definitive AEAT rejection, by contrast, is final: Error, and the InvoiceID
    // is cleared because nothing is registered under it.
    void test_updateTicketVerifactuFields_aeatRejectionIsFinal()
    {
        insertIngreso("P4", "10-03-2026", "50.00", "SI", "PENDIENTE", 0, /*seq=*/0);

        VerifactuResult rejected;
        rejected.status           = VerifactuResult::ERROR;
        rejected.errorDescription = "NIF invalido";
        updateTicketVerifactuFields(m_db, "P4", rejected, /*seq=*/0);

        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo = 'P4'"),
                 QStringLiteral("ERROR"));
        QCOMPARE(scalar("SELECT COALESCE(verifactu_invoice_id, '') FROM ingresos "
                        "WHERE n_recibo = 'P4'"), QString());
    }

    // A reply never rewrites an invoice that is already settled: a duplicate rejection
    // arriving for an event whose rows are ENVIADA must not turn them into ERROR and
    // wipe their CSV (only a still-pending row of the same event takes the result).
    void test_updateTicketVerifactuFields_neverRewritesSettledRows()
    {
        insertIngreso("P5", "10-03-2026", "50.00", "SI", "ENVIADA", 0, /*seq=*/0);
        exec("UPDATE ingresos SET verifactu_csv = 'CSV-P5', hash = 'p5a' WHERE n_recibo = 'P5'");
        insertIngreso("P5", "10-03-2026", "8.00", "SI", "PENDIENTE", 0, /*seq=*/0);

        VerifactuResult dup;
        dup.status           = VerifactuResult::ERROR;
        dup.errorDescription = "Registro duplicado";
        QCOMPARE(updateTicketVerifactuFields(m_db, "P5", dup, /*seq=*/0), 1);

        QCOMPARE(scalar("SELECT verifactu_estado || '|' || verifactu_csv FROM ingresos WHERE hash = 'p5a'"),
                 QStringLiteral("ENVIADA|CSV-P5"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo = 'P5' AND importe = '8.00'"),
                 QStringLiteral("ERROR"));

        // Once every row is settled a reply changes nothing, and says so (0), so the
        // caller does not report "enviado" / "Error al enviar" for it.
        exec("UPDATE ingresos SET verifactu_estado = 'ENVIADA' WHERE n_recibo = 'P5'");
        QCOMPARE(updateTicketVerifactuFields(m_db, "P5", dup, /*seq=*/0), 0);
    }

    // A retry re-submits ONE payment event. Before this seam RecogPrendas summed
    // every row of the ticket and sent it under the bare n_recibo on the RECEPTION
    // date - so retrying a partial payment submitted the wrong amount under an
    // InvoiceID belonging to a different event, on a date AEAT never keyed it on.
    void test_verifactuEventFor()
    {
        // seq 0: paid 50, plus an unpaid 30 that is NOT part of the invoice.
        insertIngreso("R1", "10-03-2026", "50.00", "SI", "ENVIADA", 0, /*seq=*/0);
        insertIngreso("R1", "",           "30.00", "NO", "SIN COBRAR", 0, /*seq=*/0);
        // seq 1: a later partial pay of 20 on a different date.
        insertIngreso("R1", "25-06-2026", "20.00", "SI", "PENDIENTE", 0, /*seq=*/1);

        const PendingVerifactuEvent e0 = verifactuEventFor(m_db, "R1", 0);
        QCOMPARE(e0.nRecibo, QStringLiteral("R1"));
        QCOMPARE(e0.seq, 0);
        QVERIFY2(qAbs(e0.importe - 50.0) < 0.01, qPrintable(QString::number(e0.importe)));
        QCOMPARE(e0.fechaPago, QStringLiteral("10-03-2026"));

        const PendingVerifactuEvent e1 = verifactuEventFor(m_db, "R1", 1);
        QVERIFY(qAbs(e1.importe - 20.0) < 0.01);
        QCOMPARE(e1.fechaPago, QStringLiteral("25-06-2026"));

        // No paid rows for that seq -> empty, so the caller refuses to re-submit.
        QVERIFY(verifactuEventFor(m_db, "R1", 7).nRecibo.isEmpty());
        QVERIFY(verifactuEventFor(m_db, "NOPE", 0).nRecibo.isEmpty());

        // A pre-10.9 seq-0 invoice paid garment by garment on different days was
        // registered under the first payment: the earliest DATE, not the smallest
        // dd-MM-yyyy text ("05-03" sorts before "28-01").
        insertIngreso("R2", "28-01-2026", "10.00", "SI", "ENVIADA", 0, /*seq=*/0);
        insertIngreso("R2", "05-03-2026", "15.00", "SI", "ENVIADA", 0, /*seq=*/0);
        QCOMPARE(verifactuEventFor(m_db, "R2", 0).fechaPago, QStringLiteral("28-01-2026"));
    }

    // Adopting AEAT's own CSV is how an "already exists" rejection gets resolved:
    // the invoice IS registered, we just lost the reply. Every row of the event is
    // settled, and the error text cleared.
    void test_reconcileVerifactuFromAeat_adoptsCsv()
    {
        insertIngreso("K1", "10-03-2026", "50.00", "SI", "ERROR", 0, /*seq=*/0);
        insertIngreso("K1", "10-03-2026", "30.00", "SI", "ERROR", 0, /*seq=*/0);
        insertIngreso("K1", "",           "20.00", "NO", "SIN COBRAR", 0, /*seq=*/0);

        QCOMPARE(reconcileVerifactuFromAeat(m_db, "K1", 0, "A-7F3K9Q",
                                            "https://aeat.example/v"), 2);
        QCOMPARE(scalar("SELECT COUNT(*) FROM ingresos WHERE n_recibo = 'K1' "
                        "AND verifactu_estado = 'ENVIADA' AND verifactu_csv = 'A-7F3K9Q'"),
                 QStringLiteral("2"));
        QCOMPARE(scalar("SELECT verifactu_error FROM ingresos WHERE n_recibo = 'K1' "
                        "AND pagado = 'SI' LIMIT 1"), QString());
        // The unpaid sibling was never part of that invoice.
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo = 'K1' "
                        "AND pagado = 'NO'"), QStringLiteral("SIN COBRAR"));
    }

    // The refusals. A settled row must never be re-stamped from a query: ENVIADA
    // already carries its own CSV, and ANULADA / RECTIFICADA were deliberately
    // superseded, so overwriting either would revive a cancelled invoice.
    void test_reconcileVerifactuFromAeat_refusals()
    {
        insertIngreso("K2", "10-03-2026", "50.00", "SI", "ANULADA",     0, /*seq=*/0);
        insertIngreso("K3", "10-03-2026", "50.00", "SI", "RECTIFICADA", 0, /*seq=*/0);
        insertIngreso("K4", "10-03-2026", "50.00", "SI", "ENVIADA",     0, /*seq=*/0);
        insertIngreso("K5", "10-03-2026", "50.00", "SI", "ERROR",       0, /*seq=*/0);

        QCOMPARE(reconcileVerifactuFromAeat(m_db, "K2", 0, "X", ""), 0);
        QCOMPARE(reconcileVerifactuFromAeat(m_db, "K3", 0, "X", ""), 0);
        QCOMPARE(reconcileVerifactuFromAeat(m_db, "K4", 0, "X", ""), 0);
        // An empty CSV is refused outright - there is nothing to adopt.
        QCOMPARE(reconcileVerifactuFromAeat(m_db, "K5", 0, "", ""), 0);
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo = 'K5'"),
                 QStringLiteral("ERROR"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE n_recibo = 'K2'"),
                 QStringLiteral("ANULADA"));
    }

    // PendingSubmitsDialog persisted a literal 'Error' for years, which no SQL
    // filter and (before the reader was made lenient) no C++ read recognised. The
    // migration normalises casing so the stored value is canonical - the SQL
    // filters compare case-sensitively, so fixing only the reader is not enough.
    void test_migrateDatabase_normalisesEstadoCasing()
    {
        insertIngreso("N1", "10-03-2026", "50.00", "SI", "Error",   0, /*seq=*/0);
        insertIngreso("N2", "10-03-2026", "50.00", "SI", "ENVIADA", 0, /*seq=*/0);
        insertIngreso("N3", "10-03-2026", "50.00", "SI", "Anulada", 0, /*seq=*/0);
        // Blank stays blank - the split-off legacy shape must survive untouched.
        exec("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, importe, "
             "pagado, estado, edit_lock, verifactu_estado, verifactu_invoice_seq) "
             "VALUES ('N4', '', '10-03-2026', '', '50.00', 'NO', '', 0, '', 0)");

        migrateDatabase(m_db);

        const QString q = "SELECT verifactu_estado FROM ingresos WHERE n_recibo = :n";
        QCOMPARE(scalar(q, {{":n", "N1"}}), QStringLiteral("ERROR"));
        QCOMPARE(scalar(q, {{":n", "N2"}}), QStringLiteral("ENVIADA"));
        QCOMPARE(scalar(q, {{":n", "N3"}}), QStringLiteral("ANULADA"));
        QCOMPARE(scalar(q, {{":n", "N4"}}), QString());

        // Idempotent.
        migrateDatabase(m_db);
        QCOMPARE(scalar(q, {{":n", "N1"}}), QStringLiteral("ERROR"));
        QCOMPARE(scalar(q, {{":n", "N4"}}), QString());
    }

    // Drives the IMPORTE PAGADO marker on a reprinted recibo, which shows the whole
    // ticket total - so a partially-paid ticket must NOT claim to be settled. The
    // reprint path used to hardcode the marker off, so a fully-paid ticket reprinted
    // from Recogida de Prendas showed no payment at all.
    void test_ticketAllGarmentsPaid()
    {
        insertIngreso("A1", "10-03-2026", "10.00", "SI", "ENVIADA");
        insertIngreso("A1", "10-03-2026", "20.00", "SI", "ENVIADA");
        QVERIFY(ticketAllGarmentsPaid(m_db, "A1"));            // every row paid

        insertIngreso("A2", "10-03-2026", "10.00", "SI", "ENVIADA");
        insertIngreso("A2", "",           "20.00", "NO", "SIN COBRAR");
        QVERIFY(!ticketAllGarmentsPaid(m_db, "A2"));           // partially paid

        insertIngreso("A3", "", "10.00", "NO", "SIN COBRAR");
        QVERIFY(!ticketAllGarmentsPaid(m_db, "A3"));           // none paid

        // A blank pagado must not read as paid, and an unknown ticket is not "all paid".
        exec("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, importe, "
             "pagado, estado, edit_lock, verifactu_estado, verifactu_invoice_seq) "
             "VALUES ('A4', '', '10-03-2026', '', '10.00', '', '', 0, '', 0)");
        QVERIFY(!ticketAllGarmentsPaid(m_db, "A4"));
        QVERIFY(!ticketAllGarmentsPaid(m_db, "NOPE"));
    }

    // A migrated SIN COBRAR row must not resurface in the recovery dialog: the
    // estado filter excludes it on its own, independently of the pagado gate.
    void test_pendingVerifactuEvents_excludesSinCobrar()
    {
        insertIngreso("T300", "10-03-2026", "50.00", "NO", "SIN COBRAR", 0, /*seq=*/0);
        QVERIFY(pendingVerifactuEvents(m_db, "2026-01-01").isEmpty());
    }

    // A retry must re-submit under the original AEAT date (fecha_pago), not the
    // reception date: a partial pay made on a different day than reception would
    // otherwise register a second invoice at AEAT (date is part of the invoice
    // identity). The recovery FLOOR still gates on fecha_recepcion.
    void test_pendingVerifactuEvents_returnsPaymentDate()
    {
        // reception 10-03-2026 (after floor), paid later on 25-06-2026.
        exec("INSERT INTO ingresos "
             "(n_recibo, cliente, fecha_recepcion, fecha_pago, importe, pagado, "
             " estado, edit_lock, verifactu_estado, verifactu_invoice_seq) "
             "VALUES ('T9', '', '10-03-2026', '25-06-2026', '60.00', 'SI', '', 0, 'PENDIENTE', 2)");

        const QVector<PendingVerifactuEvent> ev = pendingVerifactuEvents(m_db, "2026-01-01");
        QCOMPARE(ev.size(), 1);
        QCOMPARE(ev[0].fechaPago, QStringLiteral("25-06-2026")); // payment date, not 10-03-2026

        // And the floor gates on reception: a reception before the floor is excluded
        // even though its payment date is after it.
        QVERIFY(pendingVerifactuEvents(m_db, "2026-12-01").isEmpty());
    }

    // A ticket whose two payment events (Q1 and Q2) are both cancelled in Q2 is one
    // regularisation entry in Q2; it keeps the LATEST payment date whatever the row
    // order, so the Q2 ticket count knows a Q2 payment was cancelled.
    void test_regularizations_mergedTicketKeepsLatestPayment()
    {
        insertIngreso("R9", "10-02-2026", "20.00", "SI", "ANULADA", 0, /*seq=*/0);
        insertIngreso("R9", "10-05-2026", "30.00", "SI", "ANULADA", 0, /*seq=*/1);
        exec("UPDATE ingresos SET fecha_anulacion = '20-05-2026' WHERE n_recibo = 'R9'");
        const QVector<RegularizationDetail> regs = regularizationsBetweenDates(m_db, QDate(2026, 4, 1), QDate(2026, 7, 1));
        QCOMPARE(regs.size(), 1);
        QCOMPARE(regs[0].fechaPago, QStringLiteral("10-05-2026"));
        QCOMPARE(regs[0].importe, 50.0);
    }

    // Exportar registros AEAT: one record per payment event AEAT holds, not per
    // garment row; its literal InvoiceID, payment date and total; in the range by
    // payment or cancellation date; unpaid rows and rows AEAT never confirmed are
    // left out.
    // An invoice AEAT holds without a readable payment date belongs to no period:
    // it is counted so the export can warn about it, never silently dropped.
    void test_aeatExportRecords_countsUndatedInvoices()
    {
        exec("INSERT INTO ingresos (n_recibo, fecha_pago, importe, pagado, hash, verifactu_csv, "
             "verifactu_estado) VALUES ('200', '', '5.00', 'SI', 'u1', 'CSV-U', 'ENVIADA'), "
             "('201', '3/5/26', '6.00', 'SI', 'u2', 'CSV-V', 'ENVIADA'), "
             "('202', '05-03-2026', '7.00', 'SI', 'u3', 'CSV-W', 'ENVIADA'), "
             "('203', '', '8.00', 'SI', 'u4', '', 'PENDIENTE')");     // unknown to AEAT: not counted
        QVector<AeatExportRecord> r;
        int undated = -1;
        QVERIFY(aeatExportRecords(m_db, QDate(2026, 3, 1), QDate(2026, 3, 31), r, &undated));
        QCOMPARE(r.size(), 1);
        QCOMPARE(undated, 2);
    }

    // Anular factura reads each invoice of a ticket in one query: the paid rows of
    // each seq with an estado, its earliest payment date by date (not text), the
    // legacy InvoiceID rebuilt; unpaid garments and legacy pre-Verifactu rows out.
    void test_submittedInvoiceEvents()
    {
        exec("INSERT INTO ingresos (n_recibo, fecha_pago, importe, pagado, hash, verifactu_csv, "
             "verifactu_estado, verifactu_invoice_seq, verifactu_invoice_id) VALUES "
             "('300', '05-03-2026', '10.00', 'SI', 'a', 'CSV-0', 'ENVIADA', 0, ''), "
             "('300', '28-01-2026', '4.00',  'SI', 'b', 'CSV-0', 'ENVIADA', 0, ''), "
             "('300', '',           '3.00',  'NO', 'c', '',      'SIN COBRAR', 0, ''), "
             "('300', '10-04-2026', '6.00',  'SI', 'd', 'CSV-1', 'ANULADA', 1, '300-1'), "
             "('300', '01-01-2025', '9.00',  'SI', 'e', '',      '', 2, ''), "
             // a paid row of seq 1 without estado: not listed, but its date counts
             "('300', '01-04-2026', '2.00',  'SI', 'f', '',      '', 1, '')");
        QVector<SubmittedInvoiceEvent> ev;
        QVERIFY(submittedInvoiceEvents(m_db, "300", ev));
        QCOMPARE(ev.size(), 2);
        QCOMPARE(ev[0].seq, 0);
        QCOMPARE(ev[0].invoiceId, QStringLiteral("300"));
        QCOMPARE(ev[0].importe, 14.0);
        QCOMPARE(ev[0].fechaPago, QStringLiteral("28-01-2026"));
        QCOMPARE(ev[0].csv, QStringLiteral("CSV-0"));
        QCOMPARE(ev[1].invoiceId, QStringLiteral("300-1"));
        QCOMPARE(ev[1].estado, QStringLiteral("ANULADA"));
        QCOMPARE(ev[1].importe, 6.0);
        QCOMPARE(ev[1].fechaPago, QStringLiteral("01-04-2026"));     // as verifactuEventFor dates it
        QCOMPARE(ev[1].fechaPago, verifactuEventFor(m_db, "300", 1).fechaPago);
    }

    // Separar prendas on a paid garment: the split-off garments stay in the invoice
    // AEAT registered (seq, estado, CSV, id, payload), and the two rows add up to
    // the original importe to the cent, so the invoice total does not change.
    void test_splitGarmentRow_paidRowStaysInItsInvoice()
    {
        exec("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, "
             "importe, pagado, estado, cantidad, prenda, size, servicio, observaciones, edit_lock, hash, "
             "verifactu_csv, verifactu_estado, verifactu_invoice_seq, verifactu_invoice_id, verifactu_xml) "
             "VALUES ('400', 'Ana', '01-03-2026', '05-03-2026', '', '10.00', 'SI', 'En tienda', '3', "
             "'Mantel', '', 'Limp.', 'nota', 0, 'orig', 'CSV-400', 'ENVIADA', 1, '400-1', '<x/>'), "
             "('401', 'Ana', '01-03-2026', '05-04-2026', '', '10.00', 'SI', 'En tienda', '3', "
             "'Mantel', '', 'Limp.', '', 1, 'locked', 'CSV-401', 'ENVIADA', 0, '401', '')");

        const QString newHash = splitGarmentRow(m_db, "400", "orig", 1);
        QCOMPARE(newHash.size(), 16);
        QCOMPARE(scalar("SELECT cantidad || '|' || importe FROM ingresos WHERE hash = 'orig'"),
                 QStringLiteral("2|6.67"));
        QCOMPARE(scalar("SELECT cantidad || '|' || importe || '|' || pagado || '|' || fecha_pago || '|' || "
                        "prenda || '|' || observaciones || '|' || verifactu_csv || '|' || verifactu_estado || '|' || "
                        "verifactu_invoice_seq || '|' || verifactu_invoice_id || '|' || verifactu_xml "
                        "FROM ingresos WHERE hash = :h", { {":h", newHash} }),
                 QStringLiteral("1|3.33|SI|05-03-2026|Mantel|nota|CSV-400|ENVIADA|1|400-1|<x/>"));

        QVector<AeatExportRecord> r;
        QVERIFY(aeatExportRecords(m_db, QDate(2026, 3, 1), QDate(2026, 3, 31), r));
        QCOMPARE(r.size(), 1);
        QCOMPARE(r[0].importe, 10.0);

        // Nothing is written for a split that would leave no garment behind.
        QVERIFY(splitGarmentRow(m_db, "400", "orig", 2).isEmpty());
        QVERIFY(splitGarmentRow(m_db, "400", "orig", 0).isEmpty());
        QCOMPARE(scalar("SELECT COUNT(*) FROM ingresos WHERE n_recibo = '400'"), QStringLiteral("2"));
        // A row locked by Contabilidad is never split.
        QVERIFY(splitGarmentRow(m_db, "401", "locked", 1).isEmpty());
        QCOMPARE(scalar("SELECT COUNT(*) || '|' || MAX(cantidad) FROM ingresos WHERE n_recibo = '401'"),
                 QStringLiteral("1|3"));

        // An older paid row stored with three decimals is never re-rounded: the part
        // split off is in cents, the original keeps the exact remainder.
        exec("INSERT INTO ingresos (n_recibo, fecha_pago, importe, pagado, cantidad, hash, verifactu_estado) "
             "VALUES ('402', '05-04-2026', '22.515', 'SI', '3', 'm2', 'ENVIADA')");
        const QString part = splitGarmentRow(m_db, "402", "m2", 1);
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash = :h", { {":h", part} }), QStringLiteral("7.51"));
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash = 'm2'"), QStringLiteral("15.005"));
    }

    // Garments split off a paid row before 10.14 were left with an empty estado,
    // outside their invoice. The migration links each back to the one ENVIADA
    // invoice on the ticket with the same payment date, garment and service; legacy
    // payments, a cancelled invoice (would move income) and an ambiguous match stay.
    void test_migrateDatabase_relinksSplitOffPaidGarment()
    {
        const QString ins = "INSERT INTO ingresos (n_recibo, fecha_pago, importe, pagado, prenda, servicio, "
                            "hash, verifactu_csv, verifactu_estado, verifactu_invoice_seq, verifactu_invoice_id, "
                            "fecha_anulacion) VALUES ";
        exec(ins + "('500', '02-06-2026', '20.00', 'SI', 'Camisa', 'Limp.', 'a', 'CSV-500', 'ENVIADA', 1, '500-1', ''), "
                   "('500', '02-06-2026', '5.00',  'SI', 'Camisa', 'Limp.', 'split', '', NULL, 0, NULL, ''), "
                   "('500', '25-04-2025', '9.00',  'SI', 'Camisa', 'Limp.', 'legacy', '', NULL, 0, NULL, ''), "
                   "('501', '03-06-2026', '6.00',  'SI', 'Traje', 'Limp.', 'b', 'CSV-501', 'ANULADA', 0, '501', '20-06-2026'), "
                   "('501', '03-06-2026', '3.00',  'SI', 'Traje', 'Limp.', 'cancelledSplit', '', '', 0, '', ''), "
                   "('502', '04-06-2026', '6.00',  'SI', 'Falda', 'Limp.', 'c', 'CSV-502', 'ENVIADA', 0, '502', ''), "
                   "('502', '04-06-2026', '6.00',  'SI', 'Falda', 'Limp.', 'd', 'CSV-502-1', 'ENVIADA', 1, '502-1', ''), "
                   "('502', '04-06-2026', '3.00',  'SI', 'Falda', 'Limp.', 'ambiguous', '', '', 0, '', ''), "
                   // paid at seq 2 while Verifactu was off: its own event, never a split
                   "('500', '02-06-2026', '4.00',  'SI', 'Camisa', 'Limp.', 'ownEvent', '', '', 2, '', '')");

        // Every relinked row is logged, so an old separate payment can be found and checked.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("relinking paid garment \"split\" of ticket \"500\""));
        migrateDatabase(m_db);
        migrateDatabase(m_db);   // idempotent
        QCOMPARE(scalar("SELECT verifactu_estado || '|' || verifactu_csv || '|' || verifactu_invoice_seq || '|' || "
                        "verifactu_invoice_id FROM ingresos WHERE hash = 'split'"),
                 QStringLiteral("ENVIADA|CSV-500|1|500-1"));
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash = 'split'"), QStringLiteral("5.00"));
        QCOMPARE(scalar("SELECT COALESCE(verifactu_estado, '') || '|' || COALESCE(verifactu_csv, '') "
                        "FROM ingresos WHERE hash = 'legacy'"), QStringLiteral("|"));
        QCOMPARE(scalar("SELECT COALESCE(verifactu_estado, '') FROM ingresos WHERE hash = 'cancelledSplit'"),
                 QString());
        QCOMPARE(scalar("SELECT COALESCE(verifactu_estado, '') FROM ingresos WHERE hash = 'ambiguous'"),
                 QString());
        QCOMPARE(scalar("SELECT COALESCE(verifactu_estado, '') || '|' || verifactu_invoice_seq "
                        "FROM ingresos WHERE hash = 'ownEvent'"), QStringLiteral("|2"));
    }

    // Open amounts an older Recogida m2 edit stored with three or four decimals are
    // rounded to cents, the way they will be charged; a paid amount stays as stored.
    void test_migrateDatabase_roundsOpenAmountsToCents()
    {
        insertRow("M1", "open", "28.405", "NO", "SIN COBRAR");
        insertRow("M1", "open4", "36.0525", "NO", "");
        insertRow("M2", "paid", "22.515", "SI", "ENVIADA");
        migrateDatabase(m_db);
        migrateDatabase(m_db);
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash = 'open'"), QStringLiteral("28.41"));
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash = 'open4'"), QStringLiteral("36.05"));
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash = 'paid'"), QStringLiteral("22.515"));
    }

    // The suites build ingresos through the app's own migrations; the column
    // positions are the ones INGRESOS_COL_* (used by every table view) expect.
    void test_ingresosSchema_matchesColumnIndices()
    {
        QVERIFY(m_db.open());
        const QSqlRecord rec = m_db.record("ingresos");
        m_db.close();
        QCOMPARE(rec.count(), INGRESOS_COL_VERIFACTU_CANCEL_XML + 1);
        QCOMPARE(rec.indexOf("hash"), INGRESOS_COL_HASH);
        QCOMPARE(rec.indexOf("verifactu_csv"), INGRESOS_COL_VERIFACTU_CSV);
        QCOMPARE(rec.indexOf("verifactu_invoice_seq"), INGRESOS_COL_VERIFACTU_INVOICE_SEQ);
        QCOMPARE(rec.indexOf("fecha_anulacion"), INGRESOS_COL_FECHA_ANULACION);
        QCOMPARE(rec.indexOf("verifactu_cancel_xml"), INGRESOS_COL_VERIFACTU_CANCEL_XML);
    }

    void test_aeatExportRecords_onePerPaymentEvent()
    {
        const char *ins = "INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, importe, "
                          "pagado, estado, edit_lock, hash, verifactu_csv, verifactu_estado, "
                          "verifactu_invoice_seq, verifactu_invoice_id, verifactu_xml, fecha_anulacion, "
                          "verifactu_rectifies_n_recibo, verifactu_rectification_type) "
                          "VALUES (:n, '', :rec, :pago, :imp, :pag, '', 0, :h, :csv, :est, :seq, :id, :xml, "
                          ":anul, :rect, :rtype)";
        auto row = [&](const char *n, const char *h, const char *rec, const char *pago, const char *imp,
                       const char *pag, const char *csv, const char *est, int seq, const char *id,
                       const char *xml, const char *anul = "", const char *rect = "", const char *rtype = "") {
            exec(ins, { {":n", n}, {":rec", rec}, {":pago", pago}, {":imp", imp}, {":pag", pag},
                        {":h", h}, {":csv", csv}, {":est", est}, {":seq", seq}, {":id", id}, {":xml", xml},
                        {":anul", anul}, {":rect", rect}, {":rtype", rtype} });
        };
        // Ticket 100: received in February, first event (two garments) paid 05-03,
        // second event paid 20-03, one garment still unpaid.
        row("100", "a", "10-02-2026", "05-03-2026", "10.00", "SI", "CSV1", "ENVIADA", 0, "100", "<x>e0</x>");
        row("100", "b", "10-02-2026", "05-03-2026", "4.50",  "SI", "CSV1", "ENVIADA", 0, "100", "<x>e0</x>");
        row("100", "c", "10-02-2026", "20-03-2026", "6.00",  "SI", "CSV2", "ENVIADA", 1, "100-1", "<x>e1</x>");
        row("100", "d", "10-02-2026", "",           "3.00",  "NO", "",     "SIN COBRAR", 0, "", "");
        // Ticket 101: legacy row without stored id.
        row("101", "e", "01-03-2026", "02-03-2026", "8.00",  "SI", "CSV3", "ENVIADA", 0, "", "<x>e2</x>");
        // Ticket 102: paid, AEAT never answered (no CSV, no payload) - nothing to export.
        row("102", "f", "01-03-2026", "03-03-2026", "9.00",  "SI", "",     "PENDIENTE", 0, "", "");
        // Ticket 103: paid in April - outside the range.
        row("103", "g", "01-03-2026", "02-04-2026", "7.00",  "SI", "CSV4", "ENVIADA", 0, "103", "<x>e3</x>");
        // Ticket 104: recovered with "Consultar en AEAT" - CSV but no stored payload.
        row("104", "h", "01-03-2026", "10-03-2026", "5.00",  "SI", "CSV5", "ENVIADA", 0, "104", "");
        // Ticket 105: paid in January, cancelled in March - in March by its cancellation.
        row("105", "i", "05-01-2026", "05-01-2026", "12.00", "SI", "CSV6", "ANULADA", 0, "105", "<x>e4</x>", "15-03-2026");
        // Ticket 106: rectificativa of 100 by substitution.
        row("106", "j", "25-03-2026", "25-03-2026", "12.00", "SI", "CSV7", "ENVIADA", 0, "106", "<x>e5</x>", "", "100", "S");
        // Ticket 107: a pre-10.9 seq-0 invoice paid over two days across the quarter
        // end - one invoice, issued on 30-03 (its first payment), total 25.
        row("107", "k", "20-03-2026", "30-03-2026", "10.00", "SI", "CSV8", "ENVIADA", 0, "107", "<x>e6</x>");
        row("107", "l", "20-03-2026", "02-04-2026", "15.00", "SI", "CSV8", "ENVIADA", 0, "107", "<x>e6</x>");

        QVector<AeatExportRecord> r;
        QVERIFY(aeatExportRecords(m_db, QDate(2026, 3, 1), QDate(2026, 3, 31), r));
        QStringList ids;
        for (const AeatExportRecord &e : r)
            ids << e.invoiceId;
        QCOMPARE(ids, QStringList({ "105", "101", "100", "104", "100-1", "106", "107" }));   // by issue date
        QCOMPARE(r[0].fechaAnulacion, QStringLiteral("15-03-2026"));
        QCOMPARE(r[0].estado, QStringLiteral("ANULADA"));
        QCOMPARE(r[2].seq, 0);
        QCOMPARE(r[2].importe, 14.5);                              // both garments, unpaid one excluded
        QCOMPARE(r[2].csv, QStringLiteral("CSV1"));
        QCOMPARE(r[2].xml, QStringLiteral("<x>e0</x>"));
        QVERIFY(r[3].xml.isEmpty());                               // known by its CSV only
        QCOMPARE(r[3].csv, QStringLiteral("CSV5"));
        QCOMPARE(r[4].fechaPago, QStringLiteral("20-03-2026"));
        QCOMPARE(r[4].importe, 6.0);
        QCOMPARE(r[5].rectifiesNRecibo, QStringLiteral("100"));
        QCOMPARE(r[5].rectificationType, QStringLiteral("S"));
        QCOMPARE(r[6].fechaPago, QStringLiteral("30-03-2026"));
        QCOMPARE(r[6].importe, 25.0);                              // both rows, though one is in April

        // April: the legacy invoice is not repeated with its April row alone.
        QVERIFY(aeatExportRecords(m_db, QDate(2026, 4, 1), QDate(2026, 4, 30), r));
        QCOMPARE(r.size(), 1);
        QCOMPARE(r[0].invoiceId, QStringLiteral("103"));

        // The range is inclusive on both ends; reception dates do not count.
        QVERIFY(aeatExportRecords(m_db, QDate(2026, 3, 5), QDate(2026, 3, 5), r));
        QCOMPARE(r.size(), 1);
        QVERIFY(aeatExportRecords(m_db, QDate(2026, 2, 1), QDate(2026, 2, 28), r));
        QCOMPARE(r.size(), 0);
    }



    void test_readClientPhones()
    {
        exec("INSERT INTO clientes (nombre, tel_fijo, movil, direccion) "
             "VALUES ('Jose', '911111111', '600000000', 'Calle Falsa 123')");
        const QStringList p = readClientPhones(m_db, "Jose");
        QCOMPARE(p.value(0), QStringLiteral("911111111"));
        QCOMPARE(p.value(1), QStringLiteral("600000000"));

        const QStringList none = readClientPhones(m_db, "Nadie");
        QVERIFY(none.value(0).isEmpty());
        QVERIFY(none.value(1).isEmpty());
    }

    // --- RecogPrendas::updateDb DB-write seams ---------------------------------
    // Each helper writes one parameterised UPDATE/INSERT keyed by (n_recibo, hash).
    // The tests assert the write lands and - critically - that the hash key scopes
    // it to a single row, so the other garments of a multi-row ticket are untouched.

    // Insert a minimal garment row with an explicit hash for the (n_recibo, hash)
    // keyed seam tests.
    void insertRow(const QString &nRecibo, const QString &hash,
                   const QString &importe = "10.00", const QString &pagado = "NO",
                   const QString &verifactuEstado = "")
    {
        exec("INSERT INTO ingresos "
             "(n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, "
             " pagado, estado, cantidad, prenda, size, servicio, observaciones, edit_lock, "
             " hash, verifactu_estado) "
             "VALUES (:n, 'Cli', '01-03-2026', '', '', :imp, :pag, 'NO', '1', 'Camisa', "
             " '0', 'Lavar', '', 0, :h, :est)",
             { {":n", nRecibo}, {":imp", importe}, {":pag", pagado},
               {":h", hash}, {":est", verifactuEstado} });
    }

    void test_updateTicketPickup_setAndClear()
    {
        insertRow("T1", "hashA");
        QVERIFY(updateTicketPickup(m_db, "T1", "hashA", "20-03-2026", "SI"));
        QCOMPARE(scalar("SELECT fecha_recogida FROM ingresos WHERE hash='hashA'"),
                 QStringLiteral("20-03-2026"));
        QCOMPARE(scalar("SELECT estado FROM ingresos WHERE hash='hashA'"), QStringLiteral("SI"));

        QVERIFY(updateTicketPickup(m_db, "T1", "hashA", "", "NO"));
        QVERIFY(scalar("SELECT fecha_recogida FROM ingresos WHERE hash='hashA'").isEmpty());
        QCOMPARE(scalar("SELECT estado FROM ingresos WHERE hash='hashA'"), QStringLiteral("NO"));
    }

    void test_updateTicketObservations()
    {
        insertRow("T1", "hashA");
        insertRow("T1", "hashB");
        QVERIFY(updateTicketObservations(m_db, "T1", "hashA", "manchas de cafe"));
        QCOMPARE(scalar("SELECT observaciones FROM ingresos WHERE hash='hashA'"),
                 QStringLiteral("manchas de cafe"));
        QVERIFY(scalar("SELECT observaciones FROM ingresos WHERE hash='hashB'").isEmpty());
    }

    void test_updateTicketSizeAndPrice()
    {
        insertRow("T1", "hashA");
        QVERIFY(updateTicketSizeAndPrice(m_db, "T1", "hashA", "2.5", "18.00"));
        QCOMPARE(scalar("SELECT size FROM ingresos WHERE hash='hashA'"), QStringLiteral("2.5"));
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash='hashA'"), QStringLiteral("18.00"));
    }

    void test_updateGarmentQtyAndImporte()
    {
        insertRow("T1", "hashA");
        QVERIFY(updateGarmentQtyAndImporte(m_db, "T1", "hashA", "3", "30.00"));
        QCOMPARE(scalar("SELECT cantidad FROM ingresos WHERE hash='hashA'"), QStringLiteral("3"));
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash='hashA'"), QStringLiteral("30.00"));
    }

    void test_updateGarmentServiceAndImporte()
    {
        insertRow("T1", "hashA");
        QVERIFY(updateGarmentServiceAndImporte(m_db, "T1", "hashA", "Plan.", "8.50"));
        QCOMPARE(scalar("SELECT servicio FROM ingresos WHERE hash='hashA'"), QStringLiteral("Plan."));
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash='hashA'"), QStringLiteral("8.50"));
    }

    // Every garment field is persisted; verifactu_estado is only what the caller sets.
    void test_insertGarmentRow_fieldsAndVerifactuLeftEmpty()
    {
        IngresoGarmentRow row;
        row.nRecibo        = "T7";
        row.cliente        = "Ana";
        row.fechaRecepcion = "01-03-2026";
        row.fechaPago      = "05-03-2026";
        row.fechaRecogida  = "";
        row.importe        = "12.505";
        row.pagado         = "SI";
        row.estado         = "NO";
        row.cantidad       = "2";
        row.prenda         = "Pantalon";
        row.size           = "1.5";
        row.servicio       = "Tinte";
        row.observaciones  = "urgente";
        row.editLock       = "0";
        row.hash           = "splitHash";

        QVERIFY(insertGarmentRow(m_db, row));
        QCOMPARE(scalar("SELECT n_recibo FROM ingresos WHERE hash='splitHash'"), QStringLiteral("T7"));
        QCOMPARE(scalar("SELECT cliente FROM ingresos WHERE hash='splitHash'"), QStringLiteral("Ana"));
        QCOMPARE(scalar("SELECT fecha_pago FROM ingresos WHERE hash='splitHash'"), QStringLiteral("05-03-2026"));
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash='splitHash'"), QStringLiteral("12.51"));   // stored in cents
        QCOMPARE(scalar("SELECT cantidad FROM ingresos WHERE hash='splitHash'"), QStringLiteral("2"));
        QCOMPARE(scalar("SELECT prenda FROM ingresos WHERE hash='splitHash'"), QStringLiteral("Pantalon"));
        QCOMPARE(scalar("SELECT servicio FROM ingresos WHERE hash='splitHash'"), QStringLiteral("Tinte"));
        QCOMPARE(scalar("SELECT observaciones FROM ingresos WHERE hash='splitHash'"), QStringLiteral("urgente"));
        QVERIFY(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='splitHash'").isEmpty());
    }

    // MainWindow::saveTicket inserts via the same seam but stamps verifactu_estado
    // PENDIENTE (NotSubmitted) so the async AEAT submit can later patch the row.
    void test_insertGarmentRow_saveTicketShapePending()
    {
        IngresoGarmentRow row;
        row.nRecibo         = "T8";
        row.cliente         = "Luis";
        row.fechaRecepcion  = "03-03-2026";
        row.fechaPago       = "03-03-2026"; // paid at save -> booked on reception date
        row.fechaRecogida   = "";           // new ticket, not picked up
        row.importe         = "21.00";
        row.pagado          = "SI";
        row.estado          = "En tienda";
        row.cantidad        = "1";
        row.prenda          = "Abrigo";
        row.size            = "";
        row.servicio        = "Limpieza";
        row.observaciones   = "";
        row.hash            = "saveHash";
        row.verifactuEstado = "PENDIENTE";

        QVERIFY(insertGarmentRow(m_db, row));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='saveHash'"),
                 QStringLiteral("PENDIENTE"));
        QCOMPARE(scalar("SELECT fecha_pago FROM ingresos WHERE hash='saveHash'"),
                 QStringLiteral("03-03-2026"));
        QCOMPARE(scalar("SELECT estado FROM ingresos WHERE hash='saveHash'"), QStringLiteral("En tienda"));
        QVERIFY(scalar("SELECT fecha_recogida FROM ingresos WHERE hash='saveHash'").isEmpty());
    }

    // Issue #41: a garment added to an existing ticket via AddGarment must also be
    // booked PENDIENTE (never blank) so the estado column is consistent and the
    // pending-submit recovery can see it. AddGarment now routes through the same
    // insertGarmentRow seam with verifactuEstado=PENDIENTE.
    void test_insertGarmentRow_addGarmentShapePending()
    {
        IngresoGarmentRow row;
        row.nRecibo         = "30877";      // existing ticket the garment is added to
        row.cliente         = "Salvador";
        row.fechaRecepcion  = "01-07-2026";
        row.fechaPago       = "";           // added unpaid
        row.fechaRecogida   = "";
        row.importe         = "11.00";
        row.pagado          = "NO";
        row.estado          = "En tienda";
        row.cantidad        = "1";
        row.prenda          = "Manta";
        row.hash            = "addGarmentHash";
        row.verifactuEstado = "PENDIENTE";

        QVERIFY(insertGarmentRow(m_db, row));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='addGarmentHash'"),
                 QStringLiteral("PENDIENTE"));
    }

    // AddGarment gate: garments may only be appended to an unpaid ticket (a paid
    // one has already been submitted to AEAT).
    void test_ticketHasPaidGarment()
    {
        insertRow("UNP", "u1");                       // pagado NO
        insertRow("UNP", "u2");                       // pagado NO
        QVERIFY(!ticketHasPaidGarment(m_db, "UNP"));  // fully unpaid -> addable

        insertRow("PAR", "p1");                       // pagado NO
        insertRow("PAR", "p2", "10.00", "SI");        // one paid row
        QVERIFY(ticketHasPaidGarment(m_db, "PAR"));   // any paid row -> blocked

        QVERIFY(!ticketHasPaidGarment(m_db, "NOPE")); // unknown ticket
    }

    // --- VoidGarmentsDialog seams (issue #40) ----------------------------------
    // A garment is voidable in place only when it was never sent to AEAT: unpaid
    // and verifactu_estado PENDIENTE/empty. Paid or already-submitted rows must go
    // through the AEAT anulacion (CancelInvoiceDialog) instead.
    void test_garmentIsLocallyVoidable()
    {
        QVERIFY(garmentIsLocallyVoidable("NO", ""));           // legacy empty = NotSubmitted
        QVERIFY(garmentIsLocallyVoidable("NO", "PENDIENTE"));
        QVERIFY(garmentIsLocallyVoidable("NO", "SIN COBRAR")); // the normal unpaid state
        QVERIFY(!garmentIsLocallyVoidable("SI", "PENDIENTE")); // paid -> not local
        QVERIFY(!garmentIsLocallyVoidable("NO", "ENVIADA"));   // sent to AEAT
        QVERIFY(!garmentIsLocallyVoidable("NO", "ANULADA"));
        QVERIFY(!garmentIsLocallyVoidable("NO", "ERROR"));
    }

    void test_garmentExcludedFromTotals()
    {
        QVERIFY(garmentExcludedFromTotals("ANULADA"));         // voided in place (ticket 31045) or cancelled at AEAT
        QVERIFY(garmentExcludedFromTotals("RECTIFICADA"));     // superseded by a rectificativa
        QVERIFY(!garmentExcludedFromTotals(""));               // legacy pre-Verifactu row
        QVERIFY(!garmentExcludedFromTotals("SIN COBRAR"));     // unpaid ticket still shows what is owed
        QVERIFY(!garmentExcludedFromTotals("PENDIENTE"));
        QVERIFY(!garmentExcludedFromTotals("ENVIADA"));
        QVERIFY(!garmentExcludedFromTotals("ERROR"));
    }

    void test_voidGarmentRow_setsAnuladoAndScopesByHash()
    {
        insertRow("T9", "hashA");            // pagado NO, verifactu_estado empty
        insertRow("T9", "hashB");            // sibling, must stay untouched

        QVERIFY(voidGarmentRow(m_db, "T9", "hashA"));
        QCOMPARE(scalar("SELECT estado FROM ingresos WHERE hash='hashA'"), QStringLiteral("Anulado"));
        QCOMPARE(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='hashA'"), QStringLiteral("ANULADA"));
        // pagado is deliberately left as-is (the row is closed, not collected).
        QCOMPARE(scalar("SELECT pagado FROM ingresos WHERE hash='hashA'"), QStringLiteral("NO"));
        // The void date goes to fecha_anulacion; never paid nor collected, so both
        // fecha_pago and fecha_recogida are empty.
        const QString today = QDate::currentDate().toString("dd-MM-yyyy");
        QCOMPARE(scalar("SELECT fecha_anulacion FROM ingresos WHERE hash='hashA'"), today);
        QCOMPARE(scalar("SELECT fecha_pago FROM ingresos WHERE hash='hashA'"), QString());
        QCOMPARE(scalar("SELECT fecha_recogida FROM ingresos WHERE hash='hashA'"), QString());
        // The sibling garment of the same ticket is keyed out by hash.
        QCOMPARE(scalar("SELECT estado FROM ingresos WHERE hash='hashB'"), QStringLiteral("NO"));
        QVERIFY(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='hashB'").isEmpty());

        // The write itself refuses what the dialog would not offer: a paid, a sent,
        // an already voided and a locked garment stay as they were.
        insertRow("T9", "paid", "10.00", "SI", "ENVIADA");
        insertRow("T9", "sent", "10.00", "NO", "ERROR");
        insertRow("T9", "locked");
        exec("UPDATE ingresos SET edit_lock = 1 WHERE hash = 'locked'");
        for (const char *h : { "paid", "sent", "hashA", "locked" })
            QVERIFY2(!voidGarmentRow(m_db, "T9", h), h);
        QCOMPARE(scalar("SELECT COUNT(*) FROM ingresos WHERE n_recibo='T9' AND estado = 'Anulado'"),
                 QStringLiteral("1"));
        QCOMPARE(scalar("SELECT fecha_anulacion FROM ingresos WHERE hash='hashA'"), today);
    }

    // "Recoger todo" marks the whole ticket Recogido but must leave a voided
    // (Anulado) garment untouched - a cancelled row is never revived.
    void test_markTicketPickedUp_excludesAnulado()
    {
        insertRow("TP", "hA");                 // normal row (estado 'NO')
        insertRow("TP", "hB");                 // will be voided below
        QVERIFY(voidGarmentRow(m_db, "TP", "hB"));
        // Legacy row with a NULL estado must still be picked up (the != 'Anulado'
        // filter is NULL-guarded; SQLite treats NULL != x as NULL, i.e. excluded).
        exec("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, importe, pagado, "
             "estado, cantidad, prenda, hash) "
             "VALUES ('TP', 'Cli', '01-04-2026', '5.00', 'NO', NULL, '1', 'Camisa', 'hC')");

        QVERIFY(markTicketPickedUp(m_db, "TP", "10-04-2026"));
        QCOMPARE(scalar("SELECT estado FROM ingresos WHERE hash='hA'"), QStringLiteral("Recogido"));
        QCOMPARE(scalar("SELECT fecha_recogida FROM ingresos WHERE hash='hA'"), QStringLiteral("10-04-2026"));
        // The voided garment stays Anulado and is never "picked up": the "Recoger
        // todo" date is not written to it, and its void date stays in fecha_anulacion.
        QCOMPARE(scalar("SELECT estado FROM ingresos WHERE hash='hB'"), QStringLiteral("Anulado"));
        QCOMPARE(scalar("SELECT fecha_recogida FROM ingresos WHERE hash='hB'"), QString());
        QCOMPARE(scalar("SELECT fecha_anulacion FROM ingresos WHERE hash='hB'"),
                 QDate::currentDate().toString("dd-MM-yyyy"));
        // The NULL-estado legacy row is picked up like any normal row.
        QCOMPARE(scalar("SELECT estado FROM ingresos WHERE hash='hC'"), QStringLiteral("Recogido"));
        QCOMPARE(scalar("SELECT fecha_recogida FROM ingresos WHERE hash='hC'"), QStringLiteral("10-04-2026"));
    }
};

QTEST_GUILESS_MAIN(TestSqlLite)
#include "test_sql_lite.moc"
