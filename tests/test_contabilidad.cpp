// Tests for Contabilidad::periodRangeFor() - the pure period-range date math
// extracted from the dialog. The range is half-open: endExclusive is the first
// day AFTER the period.

#include <QtTest>
#include <QDate>

#include "contabilidad.h"

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
};

QTEST_GUILESS_MAIN(TestContabilidad)
#include "test_contabilidad.moc"
