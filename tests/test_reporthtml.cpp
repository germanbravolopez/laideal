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
        // Spanish decimal comma, exactly two decimals, trailing euro sign.
        QCOMPARE(ReportHtml::formatEuro(1234.56), QStringLiteral("1234,56 €"));
        QCOMPARE(ReportHtml::formatEuro(0.0),     QStringLiteral("0,00 €"));
        QCOMPARE(ReportHtml::formatEuro(-5.5),    QStringLiteral("-5,50 €"));
        // Structure for any value: ", <2 decimals> €" suffix.
        const QString big = ReportHtml::formatEuro(1000000.0);
        QVERIFY2(big.endsWith(",00 €"), qPrintable(big));
        // (Thousands grouping for large values is locale-quirky - see the
        // "formatEuro thousands grouping" tracker note - so it is not asserted.)
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
