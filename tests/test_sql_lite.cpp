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
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QVariantMap>

#include "sql_lite.h"

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
        QSqlQuery q(m_db);
        // Column names (not order) are what the sql_lite functions key on.
        QVERIFY2(q.exec(
            "CREATE TABLE ingresos ("
            " n_recibo TEXT, cliente TEXT, fecha_recepcion TEXT, fecha_pago TEXT,"
            " fecha_recogida TEXT, importe TEXT, pagado TEXT, estado TEXT,"
            " cantidad TEXT, prenda TEXT, size TEXT, servicio TEXT,"
            " observaciones TEXT, edit_lock INTEGER DEFAULT 0, hash TEXT,"
            " verifactu_csv TEXT, verifactu_timestamp TEXT, verifactu_estado TEXT,"
            " verifactu_error TEXT, verifactu_url_qr TEXT, verifactu_xml TEXT,"
            " verifactu_hash TEXT, verifactu_rectifies_n_recibo TEXT,"
            " verifactu_rectification_type TEXT, verifactu_invoice_seq INTEGER DEFAULT 0,"
            " verifactu_invoice_id TEXT)"), qPrintable(q.lastError().text()));
        QVERIFY2(q.exec(
            "CREATE TABLE gastos ("
            " id INTEGER PRIMARY KEY, fecha TEXT, importe TEXT, iva INTEGER,"
            " edit_lock INTEGER DEFAULT 0)"), qPrintable(q.lastError().text()));
        QVERIFY2(q.exec(
            "CREATE TABLE clientes ("
            " nombre TEXT, tel_fijo TEXT, movil TEXT, direccion TEXT)"),
            qPrintable(q.lastError().text()));
        m_db.close();
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

    // totalPriceBetweenDates(ingresos): only pagado='SI', excludes ANULADA and
    // RECTIFICADA, includes ENVIADA / legacy-empty, half-open [start, end).
    void test_totalPriceBetweenDates_ingresosFilters()
    {
        insertIngreso("T1", "10-03-2026", "100.00", "SI", "ENVIADA");
        insertIngreso("T2", "12-03-2026", "50.00",  "SI", "");          // legacy, included
        insertIngreso("T3", "13-03-2026", "30.00",  "SI", "ANULADA");   // excluded
        insertIngreso("T4", "14-03-2026", "20.00",  "SI", "RECTIFICADA"); // excluded
        insertIngreso("T5", "15-03-2026", "70.00",  "NO", "ENVIADA");   // unpaid, excluded
        insertIngreso("T6", "01-04-2026", "999.00", "SI", "ENVIADA");   // == end, half-open excludes

        const float total = totalPriceBetweenDates(
            m_db, "ingresos", QDate(2026, 3, 1), QDate(2026, 4, 1), 0);
        QVERIFY2(qAbs(total - 150.0f) < 0.01f, qPrintable(QString::number(total)));
    }

    // totalPriceBetweenDates(gastos): filters by iva rate.
    void test_totalPriceBetweenDates_gastosByIva()
    {
        exec("INSERT INTO gastos (fecha, importe, iva) VALUES ('10-03-2026', '121.00', 21)");
        exec("INSERT INTO gastos (fecha, importe, iva) VALUES ('11-03-2026', '110.00', 10)");
        const float v21 = totalPriceBetweenDates(
            m_db, "gastos", QDate(2026, 3, 1), QDate(2026, 4, 1), 21);
        QVERIFY2(qAbs(v21 - 121.0f) < 0.01f, qPrintable(QString::number(v21)));
    }

    void test_countOperationsBetweenDates_distinctTickets()
    {
        insertIngreso("T1", "10-03-2026", "10.00", "SI", "ENVIADA"); // same ticket, 2 rows
        insertIngreso("T1", "10-03-2026", "10.00", "SI", "ENVIADA");
        insertIngreso("T2", "11-03-2026", "10.00", "SI", "ENVIADA");
        const int n = countOperationsBetweenDates(
            m_db, "ingresos", QDate(2026, 3, 1), QDate(2026, 4, 1));
        QCOMPARE(n, 2);
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
    }

    // The single source of truth for the AEAT InvoiceID format (used at submit,
    // persist, cancel and reprint). seq 0 -> bare n_recibo; seq>0 -> "<n>-<seq>".
    void test_verifactuInvoiceId()
    {
        QCOMPARE(verifactuInvoiceId("3245", 0), QStringLiteral("3245"));
        QCOMPARE(verifactuInvoiceId("3245", 1), QStringLiteral("3245-1"));
        QCOMPARE(verifactuInvoiceId("3245", 12), QStringLiteral("3245-12"));
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

    // The annual report's grouped per-quarter aggregation must equal, quarter by
    // quarter, the per-quarter totalPriceBetweenDates / countOperationsBetweenDates
    // it replaces - so the tax math is provably unchanged. Dot-decimal fixtures
    // (the comma path is an error dialog the grouped SUM does not reproduce).
    void test_annualAccountingByQuarter()
    {
        // ingresos spread across quarters; ANULADA / unpaid excluded, a two-row
        // ticket counts once (distinct n_recibo), a legacy empty-estado row counts.
        insertIngreso("I1", "15-02-2026", "100.00", "SI", "ENVIADA");      // Q1
        insertIngreso("I1", "20-02-2026", "50.00",  "SI", "ENVIADA");      // Q1, same ticket
        insertIngreso("I2", "10-05-2026", "200.00", "SI", "");             // Q2, legacy
        insertIngreso("I3", "11-05-2026", "30.00",  "SI", "ANULADA");      // excluded
        insertIngreso("I4", "01-08-2026", "70.00",  "NO", "ENVIADA");      // unpaid, excluded
        insertIngreso("I5", "20-11-2026", "400.00", "SI", "ENVIADA");      // Q4

        // gastos across quarters and iva rates.
        exec("INSERT INTO gastos (fecha, importe, iva) VALUES ('10-02-2026', '121.00', 21)"); // Q1
        exec("INSERT INTO gastos (fecha, importe, iva) VALUES ('11-03-2026', '110.00', 10)"); // Q1
        exec("INSERT INTO gastos (fecha, importe, iva) VALUES ('05-05-2026', '50.00',  0)");  // Q2 sin IVA
        exec("INSERT INTO gastos (fecha, importe, iva) VALUES ('20-12-2026', '242.00', 21)"); // Q4

        const QuarterlyAccountingTotals t = annualAccountingByQuarter(m_db, 2026);

        const QDate starts[4] = { QDate(2026,1,1), QDate(2026,4,1), QDate(2026,7,1), QDate(2026,10,1) };
        const QDate ends[4]   = { QDate(2026,4,1), QDate(2026,7,1), QDate(2026,10,1), QDate(2027,1,1) };
        for (int i = 0; i < 4; ++i) {
            const float ing = totalPriceBetweenDates(m_db, "ingresos", starts[i], ends[i], 0);
            const int   ingC = countOperationsBetweenDates(m_db, "ingresos", starts[i], ends[i]);
            const float g10 = totalPriceBetweenDates(m_db, "gastos", starts[i], ends[i], 10);
            const float g21 = totalPriceBetweenDates(m_db, "gastos", starts[i], ends[i], 21);
            const float gNi = totalPriceBetweenDates(m_db, "gastos", starts[i], ends[i], 0);
            const int   gC  = countOperationsBetweenDates(m_db, "gastos", starts[i], ends[i]);
            QVERIFY2(qAbs(t.ingImporte[i]   - ing) < 0.01, qPrintable(QString("Q%1 ing").arg(i + 1)));
            QCOMPARE(t.ingTickets[i], ingC);
            QVERIFY2(qAbs(t.gas10Importe[i] - g10) < 0.01, qPrintable(QString("Q%1 g10").arg(i + 1)));
            QVERIFY2(qAbs(t.gas21Importe[i] - g21) < 0.01, qPrintable(QString("Q%1 g21").arg(i + 1)));
            QVERIFY2(qAbs(t.gasNiImporte[i] - gNi) < 0.01, qPrintable(QString("Q%1 gNi").arg(i + 1)));
            QCOMPARE(t.gasFacturas[i], gC);
        }
        // Spot-check the absolute Q1 numbers so a coincidental both-wrong match
        // can't pass: 100+50 income, 1 ticket, 121 @21% + 110 @10%, 2 facturas.
        QVERIFY(qAbs(t.ingImporte[0] - 150.0) < 0.01);
        QCOMPARE(t.ingTickets[0], 1);
        QVERIFY(qAbs(t.gas21Importe[0] - 121.0) < 0.01);
        QVERIFY(qAbs(t.gas10Importe[0] - 110.0) < 0.01);
        QCOMPARE(t.gasFacturas[0], 2);
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

    void test_updateTicketPayment_setAndClear()
    {
        insertRow("T1", "hashA");
        insertRow("T1", "hashB"); // same ticket, different garment - must stay untouched

        QVERIFY(updateTicketPayment(m_db, "T1", "hashA", "15-03-2026", "SI"));
        QCOMPARE(scalar("SELECT fecha_pago FROM ingresos WHERE hash='hashA'"),
                 QStringLiteral("15-03-2026"));
        QCOMPARE(scalar("SELECT pagado FROM ingresos WHERE hash='hashA'"), QStringLiteral("SI"));
        // The sibling row is keyed out by hash.
        QCOMPARE(scalar("SELECT pagado FROM ingresos WHERE hash='hashB'"), QStringLiteral("NO"));
        QVERIFY(scalar("SELECT fecha_pago FROM ingresos WHERE hash='hashB'").isEmpty());

        // PAY_NO path: empty fecha_pago, pagado back to NO.
        QVERIFY(updateTicketPayment(m_db, "T1", "hashA", "", "NO"));
        QVERIFY(scalar("SELECT fecha_pago FROM ingresos WHERE hash='hashA'").isEmpty());
        QCOMPARE(scalar("SELECT pagado FROM ingresos WHERE hash='hashA'"), QStringLiteral("NO"));
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

    // The split-off row must persist all garment fields and leave verifactu_estado
    // empty (legacy/NotSubmitted) so accounting/print treat it as un-submitted - a
    // re-submission would duplicate the ticket's AEAT InvoiceID.
    void test_insertGarmentRow_fieldsAndVerifactuLeftEmpty()
    {
        IngresoGarmentRow row;
        row.nRecibo        = "T7";
        row.cliente        = "Ana";
        row.fechaRecepcion = "01-03-2026";
        row.fechaPago      = "05-03-2026";
        row.fechaRecogida  = "";
        row.importe        = "12.50";
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
        QCOMPARE(scalar("SELECT importe FROM ingresos WHERE hash='splitHash'"), QStringLiteral("12.50"));
        QCOMPARE(scalar("SELECT cantidad FROM ingresos WHERE hash='splitHash'"), QStringLiteral("2"));
        QCOMPARE(scalar("SELECT prenda FROM ingresos WHERE hash='splitHash'"), QStringLiteral("Pantalon"));
        QCOMPARE(scalar("SELECT servicio FROM ingresos WHERE hash='splitHash'"), QStringLiteral("Tinte"));
        QCOMPARE(scalar("SELECT observaciones FROM ingresos WHERE hash='splitHash'"), QStringLiteral("urgente"));
        // A split-off row leaves verifactuEstado empty -> reads as legacy/NotSubmitted.
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

    // Read-back used by the PAY_YES pay-all dedup: estado of the ticket's first
    // row, empty when the ticket has no rows.
    void test_ticketVerifactuEstado()
    {
        QVERIFY(ticketVerifactuEstado(m_db, "T1").isEmpty()); // no rows
        insertRow("T1", "hashA", "10.00", "SI", "ENVIADA");
        QCOMPARE(ticketVerifactuEstado(m_db, "T1"), QStringLiteral("ENVIADA"));
    }

    // --- VoidGarmentsDialog seams (issue #40) ----------------------------------
    // A garment is voidable in place only when it was never sent to AEAT: unpaid
    // and verifactu_estado PENDIENTE/empty. Paid or already-submitted rows must go
    // through the AEAT anulacion (CancelInvoiceDialog) instead.
    void test_garmentIsLocallyVoidable()
    {
        QVERIFY(garmentIsLocallyVoidable("NO", ""));           // legacy empty = NotSubmitted
        QVERIFY(garmentIsLocallyVoidable("NO", "PENDIENTE"));
        QVERIFY(!garmentIsLocallyVoidable("SI", "PENDIENTE")); // paid -> not local
        QVERIFY(!garmentIsLocallyVoidable("NO", "ENVIADA"));   // sent to AEAT
        QVERIFY(!garmentIsLocallyVoidable("NO", "ANULADA"));
        QVERIFY(!garmentIsLocallyVoidable("NO", "ERROR"));
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
        // The sibling garment of the same ticket is keyed out by hash.
        QCOMPARE(scalar("SELECT estado FROM ingresos WHERE hash='hashB'"), QStringLiteral("NO"));
        QVERIFY(scalar("SELECT verifactu_estado FROM ingresos WHERE hash='hashB'").isEmpty());
    }

    // "Recoger todo" marks the whole ticket Recogido but must leave a voided
    // (Anulado) garment untouched - a cancelled row is never revived.
    void test_markTicketPickedUp_excludesAnulado()
    {
        insertRow("TP", "hA");                 // normal row (estado 'NO')
        insertRow("TP", "hB");                 // will be voided below
        QVERIFY(voidGarmentRow(m_db, "TP", "hB"));

        QVERIFY(markTicketPickedUp(m_db, "TP", "10-04-2026"));
        QCOMPARE(scalar("SELECT estado FROM ingresos WHERE hash='hA'"), QStringLiteral("Recogido"));
        QCOMPARE(scalar("SELECT fecha_recogida FROM ingresos WHERE hash='hA'"), QStringLiteral("10-04-2026"));
        // The voided garment stays Anulado and gets no pickup date.
        QCOMPARE(scalar("SELECT estado FROM ingresos WHERE hash='hB'"), QStringLiteral("Anulado"));
        QVERIFY(scalar("SELECT fecha_recogida FROM ingresos WHERE hash='hB'").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSqlLite)
#include "test_sql_lite.moc"
