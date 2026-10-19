// Tests for ReportHtml (the PDF-report scaffolding shared by Contabilidad and
// Listados): the pure helpers, documentOpen() on a throwaway settings file (never
// the real one) and writePdf() into a temporary folder.

#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include "appsettings.h"

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

    // The business identity is user text: markup in it must not reach the HTML.
    void test_documentOpen_escapesBusinessText()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppSettings *s = AppSettings::instance();
        s->loadFrom(dir.filePath("settings.json"));
        s->setBusinessName("A&B <x>");
        s->setBusinessAddress("C/ Sol <1> & 2");
        s->setBusinessCity("R&D");
        s->setBusinessPhone("<b>9</b>");
        s->setVerifactuNif("B<1>");
        const QString html = ReportHtml::documentOpen("Titulo <i>html</i>", "Sub");
        QVERIFY(html.contains("A&amp;B &lt;x&gt;"));
        QVERIFY(html.contains("C/ Sol &lt;1&gt; &amp; 2"));
        QVERIFY(html.contains("R&amp;D"));
        QVERIFY(html.contains("Tel: &lt;b&gt;9&lt;/b&gt;"));
        QVERIFY(html.contains("NIF: B&lt;1&gt;"));
        QVERIFY(!html.contains("<x>"));
        QVERIFY(html.contains("<h1>Titulo <i>html</i></h1>"));   // title is HTML by contract
    }

    // A new PDF replaces the old one only once complete; nothing is left beside it.
    void test_writePdf_replacesAndLeavesNoPart()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ReportHtml::setOpenGeneratedReports(false);
        const QString file = dir.filePath("r.pdf");
        QVERIFY(ReportHtml::writePdf(file, "<html><body>uno</body></html>"));
        const qint64 first = QFileInfo(file).size();
        QVERIFY(first > 0);
        QVERIFY(ReportHtml::writePdf(file, "<html><body>dos</body></html>"));
        QVERIFY(QFileInfo(file).size() > 0);
        QVERIFY(!QFile::exists(file + ".part"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);
    }

    // A failed write reports false and keeps the report that was already there.
    void test_writePdf_failureKeepsExistingFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ReportHtml::setOpenGeneratedReports(false);
        const QString file = dir.filePath("keep.pdf");
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("filed");
        f.close();
        // The .part name is taken by a directory, so the print cannot succeed.
        QVERIFY(QDir(dir.path()).mkdir("keep.pdf.part"));
        QVERIFY(!ReportHtml::writePdf(file, "<html><body>x</body></html>"));
        QFile g(file);
        QVERIFY(g.open(QIODevice::ReadOnly));
        QCOMPARE(g.readAll(), QByteArray("filed"));
    }

    void test_documentClose()
    {
        const QString c = ReportHtml::documentClose();
        QVERIFY(c.trimmed().endsWith("</body></html>"));
        QVERIFY(c.contains("La Ideal")); // footer attribution
    }
};

QTEST_MAIN(TestReportHtml)
#include "test_reporthtml.moc"
