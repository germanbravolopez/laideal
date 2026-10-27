// Tests for Updater's pure helpers: the version compare that drives the "is a newer
// release available?" decision, and the installer checks (asset, URL, SHA-256, size)
// that decide whether a downloaded installer may run.

#include <QtTest>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "updater.h"

// Answers every request with one canned HTTP response; never reaches the internet.
class CannedServer : public QObject
{
public:
    QTcpServer server;
    int status = 200;
    QByteArray body;
    int hits = 0;

    CannedServer()
    {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    if (socket->readAll().isEmpty() || socket->property("answered").toBool())
                        return;
                    socket->setProperty("answered", true);
                    ++hits;
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                                  "Connection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/releases/latest").arg(server.serverPort())); }
};

// Answers per path (404 when unknown); a path can claim more bytes than it sends (cut-off
// transfer) or never answer. Never reaches the internet.
class PathServer : public QObject
{
public:
    struct Answer { QByteArray body; qint64 claimedLength = -1; bool hang = false; QString redirectTo; };
    QTcpServer server;
    QMap<QString, Answer> answers;
    QMap<QString, int> hits;

    explicit PathServer(const QHostAddress &address = QHostAddress::LocalHost)
    {
        server.listen(address);
        connect(&server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    const QByteArray req = socket->readAll();
                    if (req.isEmpty() || socket->property("answered").toBool())
                        return;
                    socket->setProperty("answered", true);
                    const QString path = QString::fromLatin1(req.split(' ').value(1));
                    ++hits[path];
                    if (answers.value(path).hang)
                        return;
                    if (!answers.value(path).redirectTo.isEmpty()) {
                        socket->write("HTTP/1.1 302 X\r\nLocation: " + answers.value(path).redirectTo.toLatin1()
                                      + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    } else if (!answers.contains(path)) {
                        socket->write("HTTP/1.1 404 X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    } else {
                        const Answer a = answers.value(path);
                        const qint64 len = a.claimedLength >= 0 ? a.claimedLength : a.body.size();
                        socket->write("HTTP/1.1 200 X\r\nContent-Length: " + QByteArray::number(len)
                                      + "\r\nConnection: close\r\n\r\n" + a.body);
                    }
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QString base() const
    {
        return QStringLiteral("http://%1:%2").arg(server.serverAddress().toString()).arg(server.serverPort());
    }
    QUrl url(const QString &path) const { return QUrl(base() + path); }
};

static QString shaOf(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}

static QString updatesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/updates");
}

// A release JSON with a laideal_setup_<tag>.exe asset (and its .sha256 asset).
static QByteArray releaseJson(const QString &tag, const QString &body = QString(),
                              const QString &exeUrl = QString(), const QString &digest = QString())
{
    const QString base = QStringLiteral("https://github.com/o/r/releases/download/%1/").arg(tag);
    QJsonObject exe{ { "name", "laideal_setup_" + tag + ".exe" }, { "size", 100 },
                     { "browser_download_url", exeUrl.isEmpty() ? base + "laideal_setup_" + tag + ".exe" : exeUrl } };
    if (!digest.isEmpty())
        exe.insert("digest", digest);
    const QJsonObject checksum{ { "name", "laideal_setup_" + tag + ".exe.sha256" },
                                { "browser_download_url", base + "laideal_setup_" + tag + ".exe.sha256" } };
    return QJsonDocument(QJsonObject{ { "tag_name", tag }, { "body", body },
                                      { "assets", QJsonArray{ exe, checksum } } }).toJson();
}

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

    void test_compareVersions_boundary11to12()
    {
        QVERIFY(Updater::compareVersions("12.0", "11.10") > 0);
        QVERIFY(Updater::compareVersions("11.10", "11.9") > 0);
        QCOMPARE(Updater::compareVersions("12.00", "12.0"), 0);
    }

    void test_findInstallerAsset_prefersTheExactName()
    {
        const QString base = QStringLiteral("https://github.com/o/r/releases/download/12.0/");
        const QJsonArray assets = {
            QJsonObject{ { "name", "laideal_setup_11.9.exe" }, { "browser_download_url", base + "laideal_setup_11.9.exe" } },
            QJsonObject{ { "name", "laideal_setup_12.0.exe" }, { "browser_download_url", base + "laideal_setup_12.0.exe" } },
        };
        QCOMPARE(Updater::findInstallerAsset(assets, "12.0").url.fileName(), QStringLiteral("laideal_setup_12.0.exe"));
        QCOMPARE(Updater::findInstallerAsset(assets).url.fileName(), QStringLiteral("laideal_setup_11.9.exe"));
        // No exact match: the first installer-looking asset still has to pass the SHA-256 check.
        QCOMPARE(Updater::findInstallerAsset(assets, "13.0").url.fileName(), QStringLiteral("laideal_setup_11.9.exe"));
    }

    // The local file name comes from the release, so anything odd gets a fixed name.
    void test_installerFileName()
    {
        QCOMPARE(Updater::installerFileName(QUrl("https://github.com/o/r/laideal_setup_12.0.exe")),
                 QStringLiteral("laideal_setup_12.0.exe"));
        QCOMPARE(Updater::installerFileName(QUrl("https://github.com/o/r/x.exe")), QStringLiteral("laideal_setup.exe"));
        QCOMPARE(Updater::installerFileName(QUrl("https://github.com/o/r/laideal_setup_12.0.exe%3Astream")),
                 QStringLiteral("laideal_setup.exe"));
        QCOMPARE(Updater::installerFileName(QUrl()), QStringLiteral("laideal_setup.exe"));
    }

    void test_parseRelease_newerWithInstaller()
    {
        const Updater::Release r = Updater::parseRelease(
            releaseJson("12.0", "notes", QString(), "sha256:" + QString(64, 'a')), "11.10");
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.newer);
        QCOMPARE(r.tag, QStringLiteral("12.0"));
        QCOMPARE(r.notes, QStringLiteral("notes"));
        QCOMPARE(r.installer.url.fileName(), QStringLiteral("laideal_setup_12.0.exe"));
    }

    void test_parseRelease_sameOrOlderTag_noUpdate()
    {
        const Updater::Release same = Updater::parseRelease(releaseJson("12.0"), "12.0");
        QVERIFY(same.error.isEmpty());
        QVERIFY(!same.newer);
        const Updater::Release older = Updater::parseRelease(releaseJson("11.9"), "12.0");
        QVERIFY(older.error.isEmpty());
        QVERIFY(!older.newer);
    }

    void test_parseRelease_missingTagOrBadJson()
    {
        QVERIFY(Updater::parseRelease("not json", "1.0").error.contains("JSON"));
        QVERIFY(Updater::parseRelease("[1,2]", "1.0").error.contains("JSON"));
        QVERIFY(Updater::parseRelease("{\"body\":\"x\"}", "1.0").error.contains("tag_name"));
        QVERIFY(Updater::parseRelease("{\"tag_name\":\"nightly\"}", "1.0").error.contains("nightly"));
    }

    void test_parseRelease_newerWithoutUsableInstaller()
    {
        const QByteArray zipOnly = R"({"tag_name":"12.0","assets":[{"name":"12.0.zip","browser_download_url":"https://github.com/x/12.0.zip"}]})";
        QVERIFY(Updater::parseRelease(zipOnly, "11.0").error.contains("12.0"));
        const QString digest = "sha256:" + QString(64, 'b');
        QVERIFY(!Updater::parseRelease(releaseJson("12.0", "", "http://github.com/x/laideal_setup_12.0.exe", digest), "11.0").error.isEmpty());
        QVERIFY(!Updater::parseRelease(releaseJson("12.0", "", "https://evil.example/laideal_setup_12.0.exe", digest), "11.0").error.isEmpty());
        // Without a digest the published .sha256 asset stands in for it.
        QVERIFY(Updater::parseRelease(releaseJson("12.0"), "11.0").error.isEmpty());
    }

    // The bilingual body shows only its Spanish section.
    void test_releaseNotesForUi()
    {
        const QString body = "## Español\n\n- Uno\n- Dos\n\n## English\n\n- One";
        QCOMPARE(Updater::releaseNotesForUi(body), QStringLiteral("- Uno\n- Dos"));
        QCOMPARE(Updater::releaseNotesForUi("## Español\n\n- Solo"), QStringLiteral("- Solo"));
        QCOMPARE(Updater::releaseNotesForUi("- sin secciones"), QStringLiteral("- sin secciones"));
        QCOMPARE(Updater::releaseNotesForUi(QString()), QString());
    }

    void test_describeNetworkError()
    {
        QVERIFY(Updater::describeNetworkError(QNetworkReply::ContentAccessDenied, 403, "raw").contains("limitado"));
        QVERIFY(Updater::describeNetworkError(QNetworkReply::ContentNotFoundError, 404, "raw").contains("404"));
        QVERIFY(Updater::describeNetworkError(QNetworkReply::TimeoutError, 0, "raw").contains("a tiempo"));
        QVERIFY(Updater::describeNetworkError(QNetworkReply::HostNotFoundError, 0, "raw").contains("conexión"));
        QVERIFY(Updater::describeNetworkError(QNetworkReply::ProtocolFailure, 0, "raw text").contains("raw text"));
    }

    void test_checkForUpdates_updateAvailable()
    {
        CannedServer server;
        server.body = releaseJson("99.0", "## Español\n\n- Hola\n\n## English\n\n- Hi", QString(), "sha256:" + QString(64, 'c'));
        Updater updater;
        updater.setApiUrl(server.url());
        QSignalSpy available(&updater, &Updater::updateAvailable);
        QSignalSpy failed(&updater, &Updater::checkFailed);
        updater.checkForUpdates(false);
        QTRY_COMPARE(available.count(), 1);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(available.at(0).at(0).toString(), QStringLiteral("99.0"));
        QCOMPARE(available.at(0).at(1).toString(), QStringLiteral("- Hola"));
        QCOMPARE(available.at(0).at(2).toUrl().fileName(), QStringLiteral("laideal_setup_99.0.exe"));
    }

    void test_checkForUpdates_http404_failsOnce()
    {
        CannedServer server;
        server.status = 404;
        server.body = "{}";
        Updater updater;
        updater.setApiUrl(server.url());
        QSignalSpy available(&updater, &Updater::updateAvailable);
        QSignalSpy failed(&updater, &Updater::checkFailed);
        updater.checkForUpdates(false);
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(available.count(), 0);
        QVERIFY(failed.at(0).at(0).toString().contains("404"));
    }

    void test_checkForUpdates_serverDown_failsOnce()
    {
        QUrl url;
        {
            CannedServer server;
            url = server.url();
        }   // closed again: nothing listens on that port
        Updater updater;
        updater.setApiUrl(url);
        QSignalSpy failed(&updater, &Updater::checkFailed);
        updater.checkForUpdates(true);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed.at(0).at(0).toString().contains("GitHub"));
    }

    // A manual click during the startup check shares its request and gets its answer shown.
    void test_silentFlagPerRequest()
    {
        CannedServer server;
        server.body = releaseJson("0.1");
        Updater updater;
        updater.setApiUrl(server.url());
        QList<bool> silent;
        connect(&updater, &Updater::noUpdateAvailable, this, [&]() { silent << updater.isSilentCheck(); });
        updater.checkForUpdates(true);
        updater.checkForUpdates(false);
        QTRY_COMPARE(silent.size(), 1);
        QCOMPARE(silent.at(0), false);
        QCOMPARE(server.hits, 1);

        updater.checkForUpdates(true);   // a later startup-style check is silent again
        QTRY_COMPARE(silent.size(), 2);
        QCOMPARE(silent.at(1), true);
    }

    // ---- downloadInstaller against a local fake (the updates folder is the Qt test location) ----

    // A newer release whose installer and checksum live on the fake; the updater holds it
    // after the check, which downloadInstaller requires.
private:
    struct Rig {
        PathServer server;
        Updater    updater;
        QUrl       installerUrl;
    };
    bool primeUpdate(Rig &rig, const QByteArray &installer, bool withDigest)
    {
        const QString exe = QStringLiteral("laideal_setup_99.0.exe");
        QJsonObject a{ { "name", exe }, { "size", installer.size() }, { "browser_download_url", rig.server.base() + "/" + exe } };
        if (withDigest)
            a.insert("digest", "sha256:" + shaOf(installer));
        const QJsonObject c{ { "name", exe + ".sha256" }, { "browser_download_url", rig.server.base() + "/" + exe + ".sha256" } };
        rig.server.answers["/releases/latest"].body = QJsonDocument(QJsonObject{
            { "tag_name", "99.0" }, { "assets", QJsonArray{ a, c } } }).toJson();
        rig.updater.setAllowLoopbackForTests(true);
        rig.updater.setApiUrl(rig.server.url("/releases/latest"));
        QSignalSpy spy(&rig.updater, &Updater::updateAvailable);
        rig.updater.checkForUpdates(false);
        if (!spy.wait(5000))
            return false;
        rig.installerUrl = spy.at(0).at(2).toUrl();
        return true;
    }

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);   // never the shop's folder
        QDir(updatesDir()).removeRecursively();
    }

    void test_downloadInstaller_writesFileAndCleansStale()
    {
        const QByteArray installer = "MZ fake installer payload";
        QDir().mkpath(updatesDir());
        QFile stale(updatesDir() + "/laideal_setup_1.0.exe");
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.write("old");
        stale.close();

        Rig rig;
        QVERIFY(primeUpdate(rig, installer, true));
        rig.server.answers["/laideal_setup_99.0.exe"].body = installer;
        QSignalSpy done(&rig.updater, &Updater::downloadFinished);
        QSignalSpy prog(&rig.updater, &Updater::downloadProgress);
        QSignalSpy failed(&rig.updater, &Updater::downloadFailed);
        rig.updater.downloadInstaller(rig.installerUrl);
        QVERIFY(done.wait(5000));
        QCOMPARE(failed.count(), 0);
        const QString path = done.at(0).at(0).toString();
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), installer);
        f.close();
        QVERIFY(!QFile::exists(updatesDir() + "/laideal_setup_1.0.exe"));
        QVERIFY(!QFile::exists(path + ".part"));
        QVERIFY(prog.count() >= 1);
        QVERIFY(!rig.updater.isDownloading());
    }

    // No digest from GitHub: the hash comes from the published .sha256 asset.
    void test_downloadInstaller_checksumAsset()
    {
        const QByteArray installer = "MZ payload two";
        Rig rig;
        QVERIFY(primeUpdate(rig, installer, false));
        rig.server.answers["/laideal_setup_99.0.exe"].body = installer;
        rig.server.answers["/laideal_setup_99.0.exe.sha256"].body = shaOf(installer).toLatin1() + "  laideal_setup_99.0.exe\n";
        QSignalSpy done(&rig.updater, &Updater::downloadFinished);
        QSignalSpy failed(&rig.updater, &Updater::downloadFailed);
        rig.updater.downloadInstaller(rig.installerUrl);
        QVERIFY(done.wait(5000));
        QCOMPARE(failed.count(), 0);
        QCOMPARE(rig.server.hits["/laideal_setup_99.0.exe.sha256"], 1);
    }

    void test_downloadInstaller_hashMismatch_noFileLeft()
    {
        Rig rig;
        QVERIFY(primeUpdate(rig, "the published bytes", true));
        rig.server.answers["/laideal_setup_99.0.exe"].body = "tampered bytes!!!!!";   // same length, other content
        QSignalSpy done(&rig.updater, &Updater::downloadFinished);
        QSignalSpy failed(&rig.updater, &Updater::downloadFailed);
        rig.updater.downloadInstaller(rig.installerUrl);
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.at(0).at(0).toString().contains("SHA-256"));
        QCOMPARE(done.count(), 0);
        QVERIFY(QDir(updatesDir()).entryList({ "*.exe", "*.part" }, QDir::Files).isEmpty());
    }

    void test_downloadInstaller_truncated_noFinalFile()
    {
        const QByteArray installer = "a full installer body";
        Rig rig;
        QVERIFY(primeUpdate(rig, installer, true));
        auto &a = rig.server.answers["/laideal_setup_99.0.exe"];
        a.body = installer.left(5);
        a.claimedLength = installer.size();   // server closes early
        QSignalSpy done(&rig.updater, &Updater::downloadFinished);
        QSignalSpy failed(&rig.updater, &Updater::downloadFailed);
        rig.updater.downloadInstaller(rig.installerUrl);
        QVERIFY(failed.wait(5000));
        QCOMPARE(done.count(), 0);
        QVERIFY(QDir(updatesDir()).entryList({ "*.exe", "*.part" }, QDir::Files).isEmpty());
    }

    void test_downloadInstaller_http404_failed()
    {
        Rig rig;
        QVERIFY(primeUpdate(rig, "bytes", true));   // the installer path is not served
        QSignalSpy failed(&rig.updater, &Updater::downloadFailed);
        rig.updater.downloadInstaller(rig.installerUrl);
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.count(), 1);
        QVERIFY(!rig.updater.isDownloading());
    }

    void test_downloadInstaller_cancel_silent()
    {
        Rig rig;
        QVERIFY(primeUpdate(rig, "bytes", true));
        rig.server.answers["/laideal_setup_99.0.exe"].hang = true;
        QSignalSpy done(&rig.updater, &Updater::downloadFinished);
        QSignalSpy failed(&rig.updater, &Updater::downloadFailed);
        rig.updater.downloadInstaller(rig.installerUrl);
        QVERIFY(rig.updater.isDownloading());
        rig.updater.cancelDownload();
        QVERIFY(!rig.updater.isDownloading());
        QTest::qWait(200);
        QCOMPARE(done.count(), 0);
        QCOMPARE(failed.count(), 0);
    }

    // A URL other than the one the last check reported is refused without any request.
    void test_downloadInstaller_unknownUrl_refused()
    {
        Rig rig;
        QVERIFY(primeUpdate(rig, "bytes", true));
        QSignalSpy failed(&rig.updater, &Updater::downloadFailed);
        rig.updater.downloadInstaller(rig.server.url("/other.exe"));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(rig.server.hits.value("/other.exe"), 0);
    }

    // A download redirected to another host is refused even when that host serves the right
    // bytes: 127.0.0.2 is loopback, but not one of the trusted test hosts.
    void test_downloadInstaller_redirectedToForeignHost_refused()
    {
        const QByteArray installer = "MZ redirected payload";
        PathServer foreign(QHostAddress(QStringLiteral("127.0.0.2")));
        if (!foreign.server.isListening())
            QSKIP("127.0.0.2 is not available on this machine");
        foreign.answers["/laideal_setup_99.0.exe"].body = installer;
        Rig rig;
        QVERIFY(primeUpdate(rig, installer, true));
        rig.server.answers["/laideal_setup_99.0.exe"].redirectTo = foreign.base() + "/laideal_setup_99.0.exe";
        QSignalSpy done(&rig.updater, &Updater::downloadFinished);
        QSignalSpy failed(&rig.updater, &Updater::downloadFailed);
        rig.updater.downloadInstaller(rig.installerUrl);
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.at(0).at(0).toString().contains("redirigido"));
        QCOMPARE(foreign.hits.value("/laideal_setup_99.0.exe"), 1);
        QCOMPARE(done.count(), 0);
        QVERIFY(QDir(updatesDir()).entryList({ "*.exe", "*.part" }, QDir::Files).isEmpty());
    }

    // Loopback http is only trusted when a test asks for it.
    void test_trustedDownloadUrl_loopbackOnlyOnRequest()
    {
        const QUrl u("http://127.0.0.1:1/x.exe");
        QVERIFY(!Updater::isTrustedDownloadUrl(u));
        QVERIFY(Updater::isTrustedDownloadUrl(u, true));
        QVERIFY(!Updater::isTrustedDownloadUrl(QUrl("http://evil.example/x.exe"), true));
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
