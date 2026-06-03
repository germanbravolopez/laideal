// Tests for ReportHtml pure helpers (the PDF-report scaffolding shared by
// Contabilidad and Listados). documentOpen() is intentionally not tested here
// because it pulls the business identity from the AppSettings singleton; the
// rest is pure. The euro sign is written as € to keep this source ASCII.

#include <QtTest>

#include "reporthtml.h"

class TestReportHtml : public QObject
{
    Q_OBJECT

private slots:
    void test_formatEuro()
    {
        // '.' thousands grouping, ',' decimal, exactly two decimals, euro sign.
        QCOMPARE(ReportHtml::formatEuro(1234.56),    QStringLiteral("1.234,56 €"));
        QCOMPARE(ReportHtml::formatEuro(999.99),     QStringLiteral("999,99 €"));   // no group below 1000
        QCOMPARE(ReportHtml::formatEuro(1000000.0),  QStringLiteral("1.000.000,00 €"));
        QCOMPARE(ReportHtml::formatEuro(0.0),        QStringLiteral("0,00 €"));
        QCOMPARE(ReportHtml::formatEuro(-1234.5),    QStringLiteral("-1.234,50 €"));
    }

    void test_tableOpen()
    {
        // grid=true draws a 1px border via the table attribute.
        QVERIFY(ReportHtml::tableOpen(true).contains("border='1'"));
        const QString borderless = ReportHtml::tableOpen(false);
        QVERIFY(borderless.contains("cellspacing"));
        QVERIFY(!borderless.contains("border='1'"));
    }

    void test_documentClose()
    {
        const QString c = ReportHtml::documentClose();
        QVERIFY(c.trimmed().endsWith("</body></html>"));
        QVERIFY(c.contains("La Ideal")); // footer attribution
    }
};

QTEST_GUILESS_MAIN(TestReportHtml)
#include "test_reporthtml.moc"
