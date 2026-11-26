// Tests for UpdaterDialog: the error of a failed download is not left on screen when the
// download is retried. A real Updater talks to a loopback fake; nothing reaches the internet.
// (The executable is not named *update*: see the note in tests/CMakeLists.txt.)

#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

#include "updater.h"
#include "updaterdialog.h"
#include "uikit.h"

namespace {

// Serves the release JSON; the installer answers 404 until `hangInstaller` is set, then never.
class FakeGitHub : public QObject
{
public:
    QTcpServer server;
    bool hangInstaller = false;
    QByteArray installer = "MZ payload";

    FakeGitHub()
    {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const QByteArray req = socket->readAll();
                    if (req.isEmpty() || socket->property("answered").toBool())
                        return;
                    socket->setProperty("answered", true);
                    const QByteArray path = req.split(' ').value(1);
                    if (path == "/releases/latest") {
                        const QByteArray body = QJsonDocument(releaseJson()).toJson();
                        socket->write("HTTP/1.1 200 X\r\nContent-Length: " + QByteArray::number(body.size())
                                      + "\r\nConnection: close\r\n\r\n" + body);
                    } else if (hangInstaller) {
                        return;
                    } else {
                        socket->write("HTTP/1.1 404 X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    }
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }

private:
    QJsonObject releaseJson() const
    {
        const QString exe = QStringLiteral("laideal_setup_99.0.exe");
        const QJsonObject asset{
            { "name", exe }, { "size", installer.size() },
            { "digest", "sha256:" + QString::fromLatin1(QCryptographicHash::hash(installer, QCryptographicHash::Sha256).toHex()) },
            { "browser_download_url", base() + "/" + exe } };
        return { { "tag_name", "99.0" }, { "assets", QJsonArray{ asset } } };
    }
};

}   // namespace

class TestUpdaterDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void test_retryHidesThePreviousError()
    {
        FakeGitHub gh;
        Updater updater;
        updater.setAllowLoopbackForTests(true);
        updater.setApiUrl(QUrl(gh.base() + "/releases/latest"));
        QSignalSpy available(&updater, &Updater::updateAvailable);
        updater.checkForUpdates(false);
        QVERIFY(available.wait(5000));

        UpdaterDialog dlg(&updater, "99.0", "notes", available.at(0).at(2).toUrl());
        auto *update = dlg.findChild<QPushButton *>("btnUpdate");
        UiKit::ResultPanel *result = nullptr;   // findChild would match any QLabel: the panel has no Q_OBJECT
        for (auto *label : dlg.findChildren<QLabel *>())
            if (auto *panel = dynamic_cast<UiKit::ResultPanel *>(label))
                result = panel;
        QVERIFY(update);
        QVERIFY(result);

        QSignalSpy failed(&updater, &Updater::downloadFailed);
        update->click();
        QVERIFY(failed.wait(5000));
        QVERIFY(result->isVisibleTo(&dlg));

        gh.hangInstaller = true;
        update->click();
        QVERIFY(updater.isDownloading());
        QVERIFY(!result->isVisibleTo(&dlg));
        updater.cancelDownload();
    }
};

QTEST_MAIN(TestUpdaterDialog)
#include "test_versiondialog.moc"
