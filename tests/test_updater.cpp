// Tests for Updater's pure helpers: the version compare that drives the "is a newer
// release available?" decision, and the installer checks (asset, URL, SHA-256, size)
// that decide whether a downloaded installer may run.

#include <QtTest>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
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
        // The first two-digit minor the project actually shipped. Lexicographically
        // "10.10" < "10.9", which would have silently stopped every auto-update.
        QVERIFY(Updater::compareVersions("10.10", "10.9") > 0);
        QVERIFY(Updater::compareVersions("10.9", "10.10") < 0);
        QVERIFY(Updater::compareVersions("v8.2", "8.1") > 0); // leading 'v' normalised
        QCOMPARE(Updater::compareVersions("8.0", "v8.0"), 0);
    }

    // A tag that is not exactly X.Y is invalid and never newer than anything.
    void test_compareVersions_malformedNeverNewer()
    {
        QVERIFY(Updater::isValidVersion("12.0"));
        QVERIFY(Updater::isValidVersion("v12.0"));
        QVERIFY(!Updater::isValidVersion("12.0.1"));
        QVERIFY(!Updater::isValidVersion("12.0-beta"));
        QVERIFY(!Updater::isValidVersion("nightly"));
        QVERIFY(!Updater::isValidVersion(""));
        QVERIFY(Updater::compareVersions("nightly", "12.0") <= 0);
        QVERIFY(Updater::compareVersions("12.0.1", "12.0") <= 0);
        QVERIFY(Updater::compareVersions("12.0-beta", "9.0") <= 0);
        QVERIFY(Updater::compareVersions("12.0", "nightly") <= 0);
        QVERIFY(Updater::compareVersions("12.0", "11.10") > 0);
        QVERIFY(Updater::compareVersions("11.10", "11.9") > 0);
    }

    // The installer asset, its size, GitHub's digest and the published .sha256 asset.
    void test_findInstallerAsset()
    {
        const QString base = QStringLiteral("https://github.com/o/r/releases/download/12.0/");
        const QJsonArray assets = {
            QJsonObject{ { "name", "12.0.zip" }, { "browser_download_url", base + "12.0.zip" } },
            QJsonObject{ { "name", "laideal_setup_12.0.exe.sha256" },
                         { "browser_download_url", base + "laideal_setup_12.0.exe.sha256" } },
            QJsonObject{ { "name", "laideal_setup_12.0.exe" }, { "size", 1234 },
                         { "digest", "sha256:ABCDEF0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789" },
                         { "browser_download_url", base + "laideal_setup_12.0.exe" } },
        };
        const Updater::InstallerAsset a = Updater::findInstallerAsset(assets);
        QCOMPARE(a.url.fileName(), QStringLiteral("laideal_setup_12.0.exe"));
        QCOMPARE(a.size, qint64(1234));
        QCOMPARE(a.sha256, QStringLiteral("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
        QCOMPARE(a.checksumUrl.fileName(), QStringLiteral("laideal_setup_12.0.exe.sha256"));
        QVERIFY(!Updater::findInstallerAsset(QJsonArray()).url.isValid());
    }

    void test_trustedDownloadUrl()
    {
        QVERIFY(Updater::isTrustedDownloadUrl(QUrl("https://github.com/o/r/releases/download/12.0/x.exe")));
        QVERIFY(Updater::isTrustedDownloadUrl(QUrl("https://objects.githubusercontent.com/abc")));
        QVERIFY(!Updater::isTrustedDownloadUrl(QUrl("http://github.com/o/r/x.exe")));
        QVERIFY(!Updater::isTrustedDownloadUrl(QUrl("https://github.com.evil.example/x.exe")));
        QVERIFY(!Updater::isTrustedDownloadUrl(QUrl("https://evilgithubusercontent.com/x.exe")));
        QVERIFY(!Updater::isTrustedDownloadUrl(QUrl()));
    }

    void test_parseChecksumFile()
    {
        const QString hex = QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        QCOMPARE(Updater::parseChecksumFile(hex.toUpper().toLatin1() + "  laideal_setup_12.0.exe\n"), hex);
        QCOMPARE(Updater::parseChecksumFile(hex.toLatin1()), hex);
        QVERIFY(Updater::parseChecksumFile("not a hash").isEmpty());
        QVERIFY(Updater::parseChecksumFile(hex.left(63).toLatin1()).isEmpty());
    }

    // Only bytes of the published size and SHA-256 may run.
    void test_installerMismatch()
    {
        const QByteArray bytes("installer bytes");
        const QString sha = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        QVERIFY(Updater::installerMismatch(bytes, bytes.size(), sha).isEmpty());
        QVERIFY(Updater::installerMismatch(bytes, 0, sha).isEmpty());                    // size not published
        QVERIFY(Updater::installerMismatch(bytes.left(5), bytes.size(), sha).contains("incompleto"));
        QVERIFY(Updater::installerMismatch("tampered bytes!", 15, sha).contains("SHA-256"));
        QVERIFY(!Updater::installerMismatch(bytes, bytes.size(), QString()).isEmpty());   // nothing to compare with
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
