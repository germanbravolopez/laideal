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
