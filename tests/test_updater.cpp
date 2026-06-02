// Tests for Updater's pure version helpers, which drive the "is a newer release
// available?" decision in the auto-updater.

#include <QtTest>
#include <QRegularExpression>

#include "updater.h"

class TestUpdater : public QObject
{
    Q_OBJECT

private slots:
    void test_compareVersions()
    {
        QVERIFY(Updater::compareVersions("9.1", "9.0") > 0);
        QVERIFY(Updater::compareVersions("9.0", "9.1") < 0);
        QCOMPARE(Updater::compareVersions("9.0", "9.0"), 0);
        QVERIFY(Updater::compareVersions("10.0", "9.9") > 0); // major compared numerically, not lexicographically
        QVERIFY(Updater::compareVersions("9.10", "9.2") > 0); // minor compared numerically
        QVERIFY(Updater::compareVersions("v8.2", "8.1") > 0); // leading 'v' normalised
        QCOMPARE(Updater::compareVersions("8.0", "v8.0"), 0);
    }

    void test_currentVersion()
    {
        const QString v = Updater::currentVersion();
        QVERIFY(!v.isEmpty());
        QVERIFY2(QRegularExpression("^\\d+\\.\\d+$").match(v).hasMatch(), qPrintable(v));
        // The compiled-in version is newer than the very first release.
        QVERIFY(Updater::compareVersions(v, "0.0") > 0);
    }
};

QTEST_GUILESS_MAIN(TestUpdater)
#include "test_updater.moc"
