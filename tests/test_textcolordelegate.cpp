// Unit tests for TextColorDelegate::classify - the pure colour decision behind
// the pagado / estado cell colouring. Covers the issue #40 rule: a voided
// garment row (estado == "Anulado") renders green everywhere, so its "NO"
// pagado no longer reads as an outstanding debt.

#include <QtTest>

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
};

QTEST_APPLESS_MAIN(TestTextColorDelegate)
#include "test_textcolordelegate.moc"
