// Unit tests for TextColorDelegate::classify - the pure colour decision behind
// the pagado / estado cell colouring. Covers the issue #40 rule: a voided
// garment row (estado == "Anulado") renders green everywhere, so its "NO"
// pagado no longer reads as an outstanding debt.

#include <QtTest>

#include <QHeaderView>
#include <QStandardItemModel>
#include <QTableView>

#include "ingresos_schema.h"
#include "ingresoscolumns.h"
#include "textcolordelegate.h"

using TC = TextColorDelegate::TextColor;

class TestTextColorDelegate : public QObject
{
    Q_OBJECT

private slots:
    void test_paidAndPickedRenderGreen()
    {
        QCOMPARE(TextColorDelegate::classify("SI", "En tienda"), TC::Green);
        QCOMPARE(TextColorDelegate::classify("Recogido", "Recogido"), TC::Green);
    }

    void test_unpaidAndInStoreRenderRed()
    {
        QCOMPARE(TextColorDelegate::classify("NO", "En tienda"), TC::Red);
        QCOMPARE(TextColorDelegate::classify("En tienda", "En tienda"), TC::Red);
    }

    void test_anuladoRowRendersGreenIncludingUnpaidPagado()
    {
        // The pagado cell of a voided row: text "NO" but the row estado is Anulado.
        QCOMPARE(TextColorDelegate::classify("NO", "Anulado"), TC::Green);
        // The estado cell itself showing "Anulado".
        QCOMPARE(TextColorDelegate::classify("Anulado", "Anulado"), TC::Green);
    }

    void test_otherTextIsDefault()
    {
        QCOMPARE(TextColorDelegate::classify("15.00", "En tienda"), TC::Default);
    }

    // A garment cancelled at AEAT (paid SI) or voided in place (NO) renders green
    // whenever verifactu_estado is ANULADA, whatever the estado cell holds.
    void test_verifactuAnuladaRendersGreen()
    {
        QCOMPARE(TextColorDelegate::classify("NO", "En tienda", "ANULADA"), TC::Green);
        QCOMPARE(TextColorDelegate::classify("En tienda", "En tienda", "ANULADA"), TC::Green);
        QCOMPARE(TextColorDelegate::classify("NO", "En tienda", "SIN COBRAR"), TC::Red);
        QCOMPARE(TextColorDelegate::classify("NO", "En tienda"), TC::Red);   // no verifactu estado given
    }

    // fecha_anulacion (appended last in the table) is shown right after the other
    // three dates; the logical columns are untouched and the call is idempotent.
    void test_placeIngresosDateColumns()
    {
        QStandardItemModel model(0, INGRESOS_COL_FECHA_ANULACION + 1);
        QTableView view;
        view.setModel(&model);
        QHeaderView *h = view.horizontalHeader();
        placeIngresosDateColumns(h);
        QCOMPARE(h->visualIndex(INGRESOS_COL_FECHA_RECEPCION), 2);
        QCOMPARE(h->visualIndex(INGRESOS_COL_FECHA_PAGO), 3);
        QCOMPARE(h->visualIndex(INGRESOS_COL_FECHA_RECOGIDA), 4);
        QCOMPARE(h->visualIndex(INGRESOS_COL_FECHA_ANULACION), 5);
        QCOMPARE(h->visualIndex(INGRESOS_COL_IMPORTE), 6);
        placeIngresosDateColumns(h);
        QCOMPARE(h->visualIndex(INGRESOS_COL_FECHA_ANULACION), 5);
        QCOMPARE(h->logicalIndex(5), INGRESOS_COL_FECHA_ANULACION);
    }
};

QTEST_MAIN(TestTextColorDelegate)
#include "test_textcolordelegate.moc"
