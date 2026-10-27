// Tests for AppLogger through its installAt() seam: rotation while the app runs, the
// previous ".old" surviving a rotation that cannot rename, and an unopenable file.

#include <QtTest>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <thread>
#include <vector>

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

    // One line per message: millisecond timestamp, level tag, text (UTF-8 intact).
    void test_lineFormatAndEncoding()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("app.log");
        QVERIFY(AppLogger::installAt(path));
        qDebug() << "d msg";
        qInfo() << "i msg";
        qWarning() << "w msg";
        qCritical() << "c msg";
        qDebug() << "camisa ñ á €";
        AppLogger::uninstall();
        const QString text = QString::fromUtf8(readAll(path));
        static const QRegularExpression line(R"(^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} \[(DEBUG|INFO|WARN|CRIT)\] )");
        for (const char *expected : { "[DEBUG] d msg", "[INFO] i msg", "[WARN] w msg", "[CRIT] c msg" })
            QVERIFY2(text.contains(QString::fromLatin1(expected)), expected);
        QVERIFY(text.contains("camisa ñ á €"));
        for (const QString &raw : text.split('\n', Qt::SkipEmptyParts)) {
            const QString l = raw.trimmed();   // the file is text mode: CRLF on Windows
            if (l.isEmpty() || l.startsWith("=== Session started"))
                continue;
            QVERIFY2(line.match(l).hasMatch(), qPrintable(l));
        }
    }

    // A secret in a message is masked before it reaches the file.
    void test_secretsAreScrubbed()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("app.log");
        QVERIFY(AppLogger::installAt(path));
        qDebug() << "payload {\"ServiceKey\":\"abc123SECRET\",\"nif\":\"B123\"}";
        qDebug() << "certificate password=hunter2 loaded";
        qDebug() << "ticket 30634 total 12.50";
        AppLogger::uninstall();
        const QByteArray log = readAll(path);
        QVERIFY(!log.contains("abc123SECRET"));
        QVERIFY(!log.contains("hunter2"));
        QVERIFY(log.contains("\"nif\":\"B123\""));
        QVERIFY(log.contains("ticket 30634 total 12.50"));
    }

    // The previous .old generation is replaced by the rotated file.
    void test_rotationReplacesExistingOld()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("app.log");
        QFile old(path + ".old");
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("stale generation");
        old.close();
        QVERIFY(AppLogger::installAt(path, 500));
        for (int i = 0; i < 30; ++i)
            qDebug() << "filler line" << i << "padding padding padding";
        AppLogger::uninstall();
        const QByteArray rotated = readAll(path + ".old");
        QVERIFY(!rotated.contains("stale generation"));
        QVERIFY(rotated.contains("filler line"));
    }

    // Several threads logging at once never interleave inside a line.
    void test_concurrentWritesStayIntact()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("app.log");
        QVERIFY(AppLogger::installAt(path, 50 * 1024 * 1024));
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([t]() {
                for (int i = 0; i < 500; ++i)
                    qDebug().noquote() << "thread" << t << "line" << i << "end";
            });
        for (std::thread &th : threads)
            th.join();
        AppLogger::uninstall();
        static const QRegularExpression line(R"(^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} \[DEBUG\] thread \d line \d+ end$)");
        int intact = 0;
        for (const QString &l : QString::fromUtf8(readAll(path)).split('\n', Qt::SkipEmptyParts))
            if (line.match(l.trimmed()).hasMatch())
                ++intact;
        QCOMPARE(intact, 2000);
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
