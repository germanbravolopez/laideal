// Tests for GenListado's pure helpers, extracted from the ui-reading slots:
// the gastos-PDF filename suffix and the per-row year/accounting-type filter.

#include <QtTest>
#include <QStandardItemModel>

#include "genlistado.h"
#include "listado.h"

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

    // The total row spans the table: 7 columns, or 8 with "Cerrado por contabilidad".
    void test_supplierTotalCells_spanMatchesColumns()
    {
        const QString withClosed = GenListado::supplierTotalCells(123456, true);
        QVERIFY(withClosed.contains("<td colspan='6'"));
        QVERIFY(withClosed.contains("<td colspan='2'"));
        QVERIFY(withClosed.contains("1.234,56"));
        const QString withoutClosed = GenListado::supplierTotalCells(123456, false);
        QVERIFY(withoutClosed.contains("<td colspan='1'"));
        QVERIFY(!withoutClosed.contains("colspan='2'"));
    }

    // A padded date is the same year as a clean one.
    void test_rowYear_paddedDate()
    {
        QCOMPARE(GenListado::rowYear("05-03-2026"), QStringLiteral("2026"));
        QCOMPARE(GenListado::rowYear(" 05-03-2026 "), QStringLiteral("2026"));
        QCOMPARE(GenListado::rowYear(QString()), QString());
    }

    // The listing orders its rows without reordering the model the user sees.
    void test_rowsSortedBy_supplierThenDate_leavesModelAlone()
    {
        QStandardItemModel model(0, 2);
        const QList<QStringList> rows = {
            { "Zeta", "01-02-2026" }, { "alfa", "20-03-2026" }, { "Zeta", "15-01-2026" }, { "alfa", "02-03-2026" },
        };
        for (const QStringList &r : rows)
            model.appendRow({ new QStandardItem(r[0]), new QStandardItem(r[1]) });
        const QVector<int> order = GenListado::rowsSortedBy(&model, 0, 1);
        QCOMPARE(order, (QVector<int>{ 3, 1, 2, 0 }));   // alfa 02-03, alfa 20-03, Zeta 15-01, Zeta 01-02
        QCOMPARE(model.index(0, 0).data().toString(), QStringLiteral("Zeta"));
        // Equal texts without a date column keep the model's order.
        QStandardItemModel same(0, 1);
        for (int i = 0; i < 3; ++i)
            same.appendRow(new QStandardItem("x"));
        QCOMPARE(GenListado::rowsSortedBy(&same, 0, -1), (QVector<int>{ 0, 1, 2 }));
    }

    void test_isOpenableQrUrl_httpsOnly()
    {
        QVERIFY(Listado::isOpenableQrUrl("https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR?nif=B1"));
        QVERIFY(!Listado::isOpenableQrUrl("http://prewww2.aeat.es/x"));
        QVERIFY(!Listado::isOpenableQrUrl("file:///C:/Windows/System32/calc.exe"));
        QVERIFY(!Listado::isOpenableQrUrl("javascript:alert(1)"));
        QVERIFY(!Listado::isOpenableQrUrl("https://"));
        QVERIFY(!Listado::isOpenableQrUrl(QString()));
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
