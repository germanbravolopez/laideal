// Tests for AppLogger through its installAt() seam: rotation while the app runs, the
// previous ".old" surviving a rotation that cannot rename, and an unopenable file.

#include <QtTest>
#include <QTemporaryDir>

#include "applogger.h"

class TestAppLogger : public QObject
{
    Q_OBJECT

private:
    static QByteArray readAll(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    static QStringList &seen()
    {
        static QStringList messages;
        return messages;
    }
    static void recordingHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
    {
        seen() << msg;
    }

private slots:
    void cleanup() { AppLogger::uninstall(); }

    // The handler that was installed before the logger still gets every message (a
    // debug run shows them in the console), and uninstall puts it back.
    void test_previousHandlerIsChainedAndRestored()
    {
        seen().clear();
        const QtMessageHandler original = qInstallMessageHandler(recordingHandler);
        QTemporaryDir dir;
        const QString path = dir.filePath("app.log");
        QVERIFY(AppLogger::installAt(path));
        qWarning() << "chained message";
        QVERIFY(seen().contains("chained message"));
        QVERIFY(readAll(path).contains("chained message"));
        AppLogger::uninstall();
        seen().clear();
        qWarning() << "after uninstall";
        QVERIFY(seen().contains("after uninstall"));
        qInstallMessageHandler(original);
    }

    // The file is rotated once it passes the limit, with no restart.
    void test_rotatesInsideTheSession()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("app.log");
        QVERIFY(AppLogger::installAt(path, 2000));
        for (int i = 0; i < 60; ++i)
            qDebug() << "line number" << i << "padding padding padding";
        AppLogger::uninstall();
        QVERIFY(QFile::exists(path + ".old"));
        QVERIFY(QFileInfo(path).size() < 2500);
        QVERIFY(readAll(path + ".old").contains("line number"));
        QVERIFY(!QFile::exists(path + ".rotating"));
    }

    // A log that is already over the limit at start is rotated before appending.
    void test_oversizedLogRotatedAtStart()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("app.log");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(5000, 'x'));
        f.close();
        QVERIFY(AppLogger::installAt(path, 2000));
        AppLogger::uninstall();
        QCOMPARE(QFileInfo(path + ".old").size(), qint64(5000));
        QVERIFY(readAll(path).contains("Session started"));
    }

    // When the current file cannot be moved aside the previous .old is untouched.
    void test_failedRenameKeepsPreviousOld()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("app.log");
        QFile old(path + ".old");
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("previous generation");
        old.close();
        // A non-empty directory on the ".rotating" name cannot be removed or renamed over.
        QVERIFY(QDir().mkpath(path + ".rotating/blocker"));
        QVERIFY(AppLogger::installAt(path, 500));
        for (int i = 0; i < 30; ++i)
            qDebug() << "filler line" << i << "padding padding padding";
        AppLogger::uninstall();
        QCOMPARE(readAll(path + ".old"), QByteArray("previous generation"));
        QVERIFY(readAll(path).contains("filler line 29"));   // logging carried on
    }

    // A path that cannot be opened is reported by the return value and does not crash.
    void test_unopenableFileDoesNotCrash()
    {
        QTemporaryDir dir;
        QVERIFY(!AppLogger::installAt(dir.filePath("missing_dir/app.log")));
        qDebug() << "goes to the default handler";
    }
};

QTEST_GUILESS_MAIN(TestAppLogger)
#include "test_applogger.moc"
