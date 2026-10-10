// Tests for Contabilidad::periodRangeFor() - the pure period-range date math
// extracted from the dialog. The range is half-open: endExclusive is the first
// day AFTER the period.

#include <QtTest>
#include <QDate>
#include <QRegularExpression>

#include "contabilidad.h"
#include "reporthtml.h"

class TestContabilidad : public QObject
{
    Q_OBJECT

private slots:
    // Gastos summary, in the order the shop reads it: IVA 21 %, IVA 10 %, their
    // subtotal with IVA, sin IVA, total - each row adding up across.
    void test_gastosTable_columnsAndSubtotal()
    {
        Contabilidad::PeriodFigures f;
        f.gas21Importe = 121.0; f.gas21Base = 100.0; f.gas21Iva = 21.0;
        f.gas10Importe = 110.0; f.gas10Base = 100.0; f.gas10Iva = 10.0;
        f.gasNiImporte = 50.0;
        QString html = Contabilidad::createHtmlTableGastos(f);
        html.remove(QRegularExpression("<td style='text-align:right;'>|</td>"));
        const QStringList headers = { "IVA 21%", "IVA 10%", "Subtotal con IVA", "Sin IVA", "Total" };
        int at = 0;
        for (const QString &h : headers) {
            const int next = html.indexOf(h, at);
            QVERIFY2(next > at, qPrintable(h));
            at = next;
        }
        const auto row = [](double a, double b, double c, double d, double e) {
            return ReportHtml::formatEuro(a) + ReportHtml::formatEuro(b) + ReportHtml::formatEuro(c)
                   + ReportHtml::formatEuro(d) + ReportHtml::formatEuro(e);
        };
        QVERIFY(html.contains("Importe" + row(121.0, 110.0, 231.0, 50.0, 281.0)));
        QVERIFY(html.contains("Base imponible" + row(100.0, 100.0, 200.0, 50.0, 250.0)));
        QVERIFY(html.contains("IVA soportado" + ReportHtml::formatEuro(21.0) + ReportHtml::formatEuro(10.0)
                              + ReportHtml::formatEuro(31.0) + "-" + ReportHtml::formatEuro(31.0)));
    }

    // The summary and the detailed report are separate files, so both are kept.
    void test_reportRelativePath()
    {
        QCOMPARE(Contabilidad::reportRelativePath(Contabilidad::Trimestral, 1, 2026, false),
                 QStringLiteral("/contabilidad_trimestral_2026_1.pdf"));
        QCOMPARE(Contabilidad::reportRelativePath(Contabilidad::Trimestral, 1, 2026, true),
                 QStringLiteral("/contabilidad_trimestral_2026_1_detalle.pdf"));
        QCOMPARE(Contabilidad::reportRelativePath(Contabilidad::Mensual, 11, 2026, true),
                 QStringLiteral("/Mensual/reporte_mensual_2026_11_detalle.pdf"));
        QCOMPARE(Contabilidad::reportRelativePath(Contabilidad::Anual, 0, 2026, false),
                 QStringLiteral("/Anual/reporte_anual_2026.pdf"));
    }

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
            Contabilidad::figuresFromDetails({t1, t2}, {}, {g21, g10, g0, g4, gNul}, 21.0,
                                             QDate(2026, 4, 1), QDate(2026, 7, 1));
        QVERIFY(qAbs(f.ingImporte - 145.2) < 1e-9);
        QVERIFY(qAbs(f.ingBase - 120.0) < 1e-9);
        QCOMPARE(f.ingTickets, 2);
        QVERIFY(qAbs(f.gas21Importe - 121.0) < 1e-9);
        QVERIFY(qAbs(f.gas10Base - 100.0) < 1e-9);
        QVERIFY(qAbs(f.gasNiImporte - 40.0) < 1e-9);
        QVERIFY(qAbs(f.gastosImporteTotal() - 271.0) < 1e-9);       // 4 % and NULL rows not summed
        QCOMPARE(f.gasFacturas, 5);                                  // but every row is counted
    }

    // A regularisation (cancellation of an earlier closed quarter's ticket) nets the
    // period's income without touching its ticket count.
    void test_figuresFromDetailsNetsRegularizations()
    {
        IncomeTicketDetail t; t.nRecibo = "50"; t.importe = 242.0;
        RegularizationDetail r; r.nRecibo = "12"; r.importe = 121.0; r.fechaPago = "10-02-2026";
        const Contabilidad::PeriodFigures f = Contabilidad::figuresFromDetails({t}, {r}, {}, 21.0,
                                                                               QDate(2026, 4, 1), QDate(2026, 7, 1));
        QVERIFY(qAbs(f.ingImporte - 121.0) < 1e-9);                  // 242 - 121
        QVERIFY(qAbs(f.ingRegularizacion - 121.0) < 1e-9);
        QVERIFY(qAbs(f.ingBase - 100.0) < 1e-9);
        QCOMPARE(f.ingTickets, 1);                                     // the cancelled ticket is not counted here

        Contabilidad::PeriodFigures year;
        year.accumulate(f);
        year.accumulate(f);
        QVERIFY(qAbs(year.ingRegularizacion - 242.0) < 1e-9);
    }

    void test_detailRegularizaciones()
    {
        QVERIFY(Contabilidad::createHtmlDetailRegularizaciones({}, 21.0).isEmpty());   // no table when none

        RegularizationDetail a; a.nRecibo = "12"; a.cliente = "Ana"; a.fechaPago = "10-02-2026";
        a.fechaAnulacion = "15-05-2026"; a.verifactuEstado = "ANULADA"; a.importe = 121.0;
        RegularizationDetail b; b.nRecibo = "13"; b.verifactuEstado = "RECTIFICADA"; b.importe = 24.2;
        const QString html = Contabilidad::createHtmlDetailRegularizaciones({a, b}, 21.0);
        QVERIFY(html.contains("Anulaciones y rectificaciones del periodo"));
        QVERIFY(html.contains("<td>15-05-2026</td>"));
        QVERIFY(html.contains("<td>Anulada</td>") && html.contains("<td>Rectificada</td>"));
        QVERIFY(html.contains("Total (2 tickets)"));
        QVERIFY(html.contains(ReportHtml::formatEuro(-145.2)));         // amounts shown negative
    }

    // Period Q2 2026. Paid and cancelled in Q2 nets to zero and is not counted; a
    // partial same-period cancellation still counts; a Q1 payment cancelled in Q2
    // does not cancel out a new Q2 sale of the same ticket (found by the e2e bench).
    void test_netTicketCount()
    {
        const QDate q2(2026, 4, 1), q3(2026, 7, 1);
        IncomeTicketDetail full;    full.nRecibo = "1";    full.importe = 50.0;
        IncomeTicketDetail partial; partial.nRecibo = "2"; partial.importe = 30.0;
        IncomeTicketDetail kept;    kept.nRecibo = "3";    kept.importe = 20.0;
        RegularizationDetail rFull;    rFull.nRecibo = "1";    rFull.importe = 50.0;    rFull.fechaPago = "10-05-2026";
        RegularizationDetail rPartial; rPartial.nRecibo = "2"; rPartial.importe = 10.0; rPartial.fechaPago = "12-05-2026";
        RegularizationDetail rOther;   rOther.nRecibo = "9";   rOther.importe = 99.0;   rOther.fechaPago = "10-02-2026";
        QCOMPARE(Contabilidad::netTicketCount({full, partial, kept}, {rFull, rPartial, rOther}, q2, q3), 2);
        QCOMPARE(Contabilidad::netTicketCount({full, partial, kept}, {}, q2, q3), 3);
        IncomeTicketDetail credit; credit.nRecibo = "4"; credit.importe = -20.0;   // by-differences credit note
        QCOMPARE(Contabilidad::netTicketCount({kept, credit}, {}, q2, q3), 1);
        IncomeTicketDetail commaOnly; commaOnly.nRecibo = "5"; commaOnly.invalidAmounts = 1;  // real sale, amount flagged
        QCOMPARE(Contabilidad::netTicketCount({commaOnly}, {}, q2, q3), 1);

        // Ticket 6: 30 charged in Q2, its 50 Q1 payment cancelled in Q2 -> still a Q2 ticket.
        IncomeTicketDetail newSale; newSale.nRecibo = "6"; newSale.importe = 30.0;
        RegularizationDetail oldPayment; oldPayment.nRecibo = "6"; oldPayment.importe = 50.0; oldPayment.fechaPago = "10-02-2026";
        QCOMPARE(Contabilidad::netTicketCount({newSale}, {oldPayment}, q2, q3), 1);

        const Contabilidad::PeriodFigures f =
            Contabilidad::figuresFromDetails({full, partial, kept}, {rFull, rPartial, rOther}, {}, 21.0, q2, q3);
        QCOMPARE(f.ingTickets, 2);
    }

    void test_yearTicketCountNetsCancellationsWithinTheYear()
    {
        QuarterlyDetails d;
        IncomeTicketDetail a; a.nRecibo = "T1"; a.importe = 10.0;
        IncomeTicketDetail b; b.nRecibo = "T2"; b.importe = 10.0;
        RegularizationDetail r; r.nRecibo = "T2"; r.importe = 10.0; r.fechaPago = "15-02-2026";
        d.income[0] = {a, b};
        d.regularizations[2] = {r};                                    // T2 paid Q1, cancelled Q3
        QCOMPARE(Contabilidad::yearTicketCount(d, 2026), 1);
    }

    void test_yearTicketCountIsDistinct()
    {
        QuarterlyDetails d;
        IncomeTicketDetail a; a.nRecibo = "T1"; a.importe = 10.0;
        IncomeTicketDetail b; b.nRecibo = "T2"; b.importe = 10.0;
        d.income[0] = {a};
        d.income[1] = {a, b};                                        // T1 paid across Q1 and Q2
        QCOMPARE(Contabilidad::yearTicketCount(d, 2026), 2);
    }

    void test_invalidAmountsFlagged()
    {
        IncomeTicketDetail t; t.nRecibo = "7"; t.importe = 10.0; t.garments = 2; t.invalidAmounts = 1;
        ExpenseDetail bad; bad.nFactura = "F-9"; bad.iva = 21; bad.invalidAmount = true;
        ExpenseDetail ok;  ok.nFactura = "F-1";  ok.iva = 21;  ok.importe = 121.0;
        QCOMPARE(Contabilidad::invalidAmountCount({t}, {}, {bad, ok}), 2);

        const QString ing = Contabilidad::createHtmlDetailIngresos({t}, 21.0);
        QVERIFY(ing.contains("<td>7 *</td>"));
        QVERIFY(ing.contains("guardado con ',' decimal"));
        QVERIFY(ing.contains("redondeados al c&eacute;ntimo"));

        const QString gas = Contabilidad::createHtmlDetailGastos({bad, ok});
        QVERIFY(gas.contains("importe no v&aacute;lido **"));
        QVERIFY(gas.contains("Total (1 facturas)"));                 // the invalid row stays out
        QVERIFY(gas.contains("1 factura(s) con el importe guardado con ',' decimal"));
        QVERIFY(!gas.contains("tipo de IVA no reconocido"));         // not double-flagged as an odd rate

        ExpenseDetail noRate; noRate.nFactura = "F-7"; noRate.iva = -1; noRate.invalidAmount = true;
        const QString gasNull = Contabilidad::createHtmlDetailGastos({noRate});
        QVERIFY(gasNull.contains("<td style='text-align:right;'>?</td>"));   // NULL rate never shown as -1
        QVERIFY(!gasNull.contains(">-1<"));
    }

    void test_detailEmptyPeriod()
    {
        QVERIFY(Contabilidad::createHtmlDetailIngresos({}, 21.0).contains("Sin tickets cobrados en el periodo."));
        QVERIFY(Contabilidad::createHtmlDetailGastos({}).contains("Sin facturas de gastos en el periodo."));
    }
};

QTEST_GUILESS_MAIN(TestContabilidad)
#include "test_contabilidad.moc"
