// Tests for Facturas' pure IVA split (gross / IVA-incluido amount -> base + cuota),
// extracted from on_le_importe_textEdited so it can be checked across rates.

#include <QtTest>

#include "facturas.h"

class TestFacturas : public QObject
{
    Q_OBJECT

private:
    static bool near(double a, double b) { return qAbs(a - b) < 0.005; }

private slots:
    void test_ivaSplit21()
    {
        QVERIFY(near(Facturas::taxBaseFromGross(121.0, 21.0), 100.0));
        QVERIFY(near(Facturas::taxAmountFromGross(121.0, 21.0), 21.0));
    }

    void test_ivaSplit10()
    {
        QVERIFY(near(Facturas::taxBaseFromGross(110.0, 10.0), 100.0));
        QVERIFY(near(Facturas::taxAmountFromGross(110.0, 10.0), 10.0));
    }

    void test_ivaZero()
    {
        QVERIFY(near(Facturas::taxBaseFromGross(50.0, 0.0), 50.0));
        QVERIFY(near(Facturas::taxAmountFromGross(50.0, 0.0), 0.0));
    }

    void test_basePlusCuotaReconstructsGross()
    {
        const double gross = 100.0, rate = 21.0; // non-round split
        QVERIFY(near(Facturas::taxBaseFromGross(gross, rate)
                     + Facturas::taxAmountFromGross(gross, rate), gross));
    }
};

QTEST_GUILESS_MAIN(TestFacturas)
#include "test_facturas.moc"
