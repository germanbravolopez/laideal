// Tests for Contabilidad::periodRangeFor() - the pure period-range date math
// extracted from the dialog. The range is half-open: endExclusive is the first
// day AFTER the period.

#include <QtTest>
#include <QDate>

#include "contabilidad.h"
#include "reporthtml.h"

class TestContabilidad : public QObject
{
    Q_OBJECT

private slots:
    void test_trimestralQuarters()
    {
        QDate s, e;
        Contabilidad::periodRangeFor(Contabilidad::Trimestral, 1, 2026, s, e);
        QCOMPARE(s, QDate(2026, 1, 1));
        QCOMPARE(e, QDate(2026, 4, 1));

        Contabilidad::periodRangeFor(Contabilidad::Trimestral, 2, 2026, s, e);
        QCOMPARE(s, QDate(2026, 4, 1));
        QCOMPARE(e, QDate(2026, 7, 1));

        // Q4 crosses the year boundary; half-open -> last included day is 31 Dec.
        Contabilidad::periodRangeFor(Contabilidad::Trimestral, 4, 2026, s, e);
        QCOMPARE(s, QDate(2026, 10, 1));
        QCOMPARE(e, QDate(2027, 1, 1));
        QCOMPARE(e.addDays(-1), QDate(2026, 12, 31));
    }

    void test_mensualMonths()
    {
        QDate s, e;
        Contabilidad::periodRangeFor(Contabilidad::Mensual, 2, 2026, s, e);
        QCOMPARE(s, QDate(2026, 2, 1));
        QCOMPARE(e, QDate(2026, 3, 1));

        // December rolls the exclusive end into the next year.
        Contabilidad::periodRangeFor(Contabilidad::Mensual, 12, 2026, s, e);
        QCOMPARE(s, QDate(2026, 12, 1));
        QCOMPARE(e, QDate(2027, 1, 1));
    }

    void test_anualUsesQuarterUnit()
    {
        QDate s, e;
        Contabilidad::periodRangeFor(Contabilidad::Anual, 3, 2026, s, e);
        QCOMPARE(s, QDate(2026, 7, 1));
        QCOMPARE(e, QDate(2026, 10, 1));
    }

    void test_lockOptionOnlyForTrimestral()
    {
        QVERIFY(Contabilidad::lockOptionAvailable(Contabilidad::Trimestral, false));
        QVERIFY(!Contabilidad::lockOptionAvailable(Contabilidad::Trimestral, true));   // reverting
        QVERIFY(!Contabilidad::lockOptionAvailable(Contabilidad::Mensual, false));
        QVERIFY(!Contabilidad::lockOptionAvailable(Contabilidad::Anual, false));
    }

    void test_combinedLockState()
    {
        QCOMPARE(Contabilidad::combinedLockState(2, 2), 2);   // no rows in either table
        QCOMPARE(Contabilidad::combinedLockState(2, 0), 0);   // gastos-only quarter, open
        QCOMPARE(Contabilidad::combinedLockState(2, 1), 1);   // gastos-only quarter, locked
        QCOMPARE(Contabilidad::combinedLockState(0, 2), 0);   // ingresos-only, open
        QCOMPARE(Contabilidad::combinedLockState(1, 0), 1);   // either locked -> locked
        QCOMPARE(Contabilidad::combinedLockState(0, 0), 0);
    }

    void test_detailIngresosRowsAndTotal()
    {
        IncomeTicketDetail a; a.nRecibo = "9";  a.cliente = "Ana <S.L.>"; a.fechaPago = "05-03-2026"; a.importe = 121.0; a.garments = 1;
        IncomeTicketDetail b; b.nRecibo = "12"; b.cliente = "García";     b.fechaPago = "10-03-2026"; b.importe = 24.2;  b.garments = 3;
        const QString html = Contabilidad::createHtmlDetailIngresos({a, b}, 21.0);
        QVERIFY(html.contains("Detalle de ingresos"));
        QVERIFY(html.contains("<td>9</td>") && html.contains("<td>12</td>"));
        QVERIFY(html.contains("Ana &lt;S.L.&gt;"));                    // client name is HTML-escaped
        QVERIFY(html.contains("Total (2 tickets)"));
        // Total = 145,20 IVA incl.; base 120,00; IVA 25,20 at 21 %.
        QVERIFY(html.contains(ReportHtml::formatEuro(145.2)));
        QVERIFY(html.contains(ReportHtml::formatEuro(120.0)));
        QVERIFY(html.contains(ReportHtml::formatEuro(25.2)));
    }

    void test_detailGastosBaseByRate()
    {
        ExpenseDetail e10; e10.fecha = "02-03-2026"; e10.nFactura = "F-1"; e10.iva = 10; e10.importe = 110.0;
        ExpenseDetail e0;  e0.fecha  = "15-03-2026"; e0.nFactura  = "F-3"; e0.iva  = 0;  e0.importe  = 40.0;
        const QString html = Contabilidad::createHtmlDetailGastos({e10, e0});
        QVERIFY(html.contains("Total (2 facturas)"));
        QVERIFY(html.contains(ReportHtml::formatEuro(100.0)));          // 110 / 1.10
        QVERIFY(html.contains(ReportHtml::formatEuro(140.0)));          // base total: 100 + 40 (sin IVA)
        QVERIFY(html.contains(ReportHtml::formatEuro(150.0)));          // importe total
    }

    void test_detailGastosFlagsUnsummarisedRates()
    {
        ExpenseDetail ok;   ok.nFactura  = "F-1"; ok.iva  = 21; ok.importe  = 121.0;
        ExpenseDetail odd;  odd.nFactura = "F-2"; odd.iva = 4;  odd.importe = 104.0;
        ExpenseDetail none; none.nFactura = "F-3"; none.iva = -1; none.importe = 50.0;
        const QString html = Contabilidad::createHtmlDetailGastos({ok, odd, none});
        QVERIFY(html.contains("4 *"));
        QVERIFY(html.contains("? *"));                                  // NULL iva
        QVERIFY(html.contains("Total (1 facturas)"));                   // only the 21 % row counts
        QVERIFY(html.contains(ReportHtml::formatEuro(121.0)));
        QVERIFY(!html.contains(ReportHtml::formatEuro(275.0)));         // the flagged rows stay out of the total
        QVERIFY(html.contains("2 factura(s) con un tipo de IVA no reconocido"));
        QVERIFY(Contabilidad::expenseIvaIsSummarised(0));
        QVERIFY(Contabilidad::expenseIvaIsSummarised(10));
        QVERIFY(!Contabilidad::expenseIvaIsSummarised(4));
        QVERIFY(!Contabilidad::expenseIvaIsSummarised(-1));
    }

    void test_figuresFromDetails()
    {
        IncomeTicketDetail t1; t1.nRecibo = "1"; t1.importe = 121.0;
        IncomeTicketDetail t2; t2.nRecibo = "2"; t2.importe = 24.2; t2.invalidAmounts = 1;  // comma row already excluded
        ExpenseDetail g21;  g21.iva = 21;  g21.importe = 121.0;
        ExpenseDetail g10;  g10.iva = 10;  g10.importe = 110.0;
        ExpenseDetail g0;   g0.iva = 0;    g0.importe = 40.0;
        ExpenseDetail g4;   g4.iva = 4;    g4.importe = 104.0;    // unrecognised rate: counted only
        ExpenseDetail gNul; gNul.iva = -1; gNul.importe = 50.0;   // NULL rate: counted only
        const Contabilidad::PeriodFigures f =
            Contabilidad::figuresFromDetails({t1, t2}, {g21, g10, g0, g4, gNul}, 21.0);
        QVERIFY(qAbs(f.ingImporte - 145.2) < 1e-9);
        QVERIFY(qAbs(f.ingBase - 120.0) < 1e-9);
        QCOMPARE(f.ingTickets, 2);
        QVERIFY(qAbs(f.gas21Importe - 121.0) < 1e-9);
        QVERIFY(qAbs(f.gas10Base - 100.0) < 1e-9);
        QVERIFY(qAbs(f.gasNiImporte - 40.0) < 1e-9);
        QVERIFY(qAbs(f.gastosImporteTotal() - 271.0) < 1e-9);       // 4 % and NULL rows not summed
        QCOMPARE(f.gasFacturas, 5);                                  // but every row is counted
    }

    void test_yearTicketCountIsDistinct()
    {
        QuarterlyDetails d;
        IncomeTicketDetail a; a.nRecibo = "T1";
        IncomeTicketDetail b; b.nRecibo = "T2";
        d.income[0] = {a};
        d.income[1] = {a, b};                                        // T1 paid across Q1 and Q2
        QCOMPARE(Contabilidad::yearTicketCount(d), 2);
    }

    void test_invalidAmountsFlagged()
    {
        IncomeTicketDetail t; t.nRecibo = "7"; t.importe = 10.0; t.garments = 2; t.invalidAmounts = 1;
        ExpenseDetail bad; bad.nFactura = "F-9"; bad.iva = 21; bad.invalidAmount = true;
        ExpenseDetail ok;  ok.nFactura = "F-1";  ok.iva = 21;  ok.importe = 121.0;
        QCOMPARE(Contabilidad::invalidAmountCount({t}, {bad, ok}), 2);

        const QString ing = Contabilidad::createHtmlDetailIngresos({t}, 21.0);
        QVERIFY(ing.contains("<td>7 *</td>"));
        QVERIFY(ing.contains("guardado con ',' decimal"));
        QVERIFY(ing.contains("redondeados al c&eacute;ntimo"));

        const QString gas = Contabilidad::createHtmlDetailGastos({bad, ok});
        QVERIFY(gas.contains("importe no v&aacute;lido **"));
        QVERIFY(gas.contains("Total (1 facturas)"));                 // the invalid row stays out
        QVERIFY(gas.contains("1 factura(s) con el importe guardado con ',' decimal"));
        QVERIFY(!gas.contains("tipo de IVA no reconocido"));         // not double-flagged as an odd rate
    }

    void test_detailEmptyPeriod()
    {
        QVERIFY(Contabilidad::createHtmlDetailIngresos({}, 21.0).contains("Sin tickets cobrados en el periodo."));
        QVERIFY(Contabilidad::createHtmlDetailGastos({}).contains("Sin facturas de gastos en el periodo."));
    }
};

QTEST_GUILESS_MAIN(TestContabilidad)
#include "test_contabilidad.moc"
