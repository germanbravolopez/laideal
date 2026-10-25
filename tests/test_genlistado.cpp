// Tests for GenListado's pure helpers, extracted from the ui-reading slots:
// the gastos-PDF filename suffix and the per-row year/accounting-type filter.

#include <QtTest>

#include "genlistado.h"

class TestGenListado : public QObject
{
    Q_OBJECT

private slots:
    void test_filenameSuffix()
    {
        QCOMPARE(GenListado::filenameSuffix("Fechas", "Incluir todos los gastos", true, "2026"),
                 QStringLiteral("agrupado_fechas_incluir_todos_los_gastos_todos_los_años"));
        QCOMPARE(GenListado::filenameSuffix("Proveedores", "Contabilidad cerrada", false, "2025"),
                 QStringLiteral("agrupado_proveedores_contabilidad_cerrada_2025"));
    }

    // Any numeric spelling of a rate divides the IVA out; zero or junk means none.
    void test_splitGasto_rates()
    {
        for (const QString &rate : { "21", "21.0", "21,0", " 21 " }) {
            const auto a = GenListado::splitGasto("121.00", rate);
            QCOMPARE(a.importeCents, qint64(12100));
            QCOMPARE(a.baseCents, qint64(10000));
            QCOMPARE(a.ivaCents, qint64(2100));
        }
        QCOMPARE(GenListado::splitGasto("110,00", "10").baseCents, qint64(10000));
        const auto four = GenListado::splitGasto("104.00", "4");
        QCOMPARE(four.baseCents, qint64(10000));
        QCOMPARE(four.ivaCents, qint64(400));
        for (const QString &rate : { "0", "", "abc", "-5" }) {
            const auto a = GenListado::splitGasto("50.00", rate);
            QCOMPARE(a.baseCents, qint64(5000));
            QCOMPARE(a.ivaCents, qint64(0));
        }
        QCOMPARE(GenListado::splitGasto("abc", "21").importeCents, qint64(0));
    }

    // Cents add up exactly where a float sum drifts.
    void test_splitGasto_longSumIsExact()
    {
        qint64 total = 0;
        for (int i = 0; i < 20000; ++i)
            total += GenListado::splitGasto("1234.57", "0").importeCents;
        QCOMPARE(total, qint64(20000) * 123457);
    }

    void test_shouldPrintGastoRow()
    {
        // All years + all types -> always printed.
        QVERIFY(GenListado::shouldPrintGastoRow(true, "", "2026", false, false));

        // Specific year: only the matching row passes.
        QVERIFY(GenListado::shouldPrintGastoRow(false, "2026", "2026", false, false));
        QVERIFY(!GenListado::shouldPrintGastoRow(false, "2026", "2025", false, false));

        // Only-closed: passes only when the row is closed by accounting.
        QVERIFY(GenListado::shouldPrintGastoRow(false, "2026", "2026", true, true));
        QVERIFY(!GenListado::shouldPrintGastoRow(false, "2026", "2026", true, false));

        // Both gates apply together: all years but only-closed + open row -> false.
        QVERIFY(!GenListado::shouldPrintGastoRow(true, "", "2026", true, false));
    }
};

QTEST_GUILESS_MAIN(TestGenListado)
#include "test_genlistado.moc"
