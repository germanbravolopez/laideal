// Tests for BackupManager: real copies of a throwaway DB (.part then rename, the
// same-second and concurrent-run guards, stale .part cleanup), and
// backupsToPrune() - the pure retention decision. Policy: keep every copy
// from the last 30 days, then the last copy of each calendar month for up to
// 4 years, drop the rest; the newest 3 are always kept; a suspect clock prunes
// nothing; non-matching names are ignored.

#include <QtTest>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "appsettings.h"
#include "backup_manager.h"
#include "sql_lite.h"

class TestBackupManager : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString backupDir() const { return m_dir.filePath("backups"); }
    QStringList backupFiles() const { return QDir(backupDir()).entryList(QDir::Files, QDir::Name); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QFile settings(m_dir.filePath("settings.json"));
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("{}");
        settings.close();
        AppSettings::instance()->loadFrom(settings.fileName());
        setDbPath(m_dir.filePath("live.db"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "seed");
            db.setDatabaseName(dbPath());
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE ingresos (n_recibo TEXT, importe TEXT)"));
            QVERIFY(q.exec("INSERT INTO ingresos VALUES ('1', '5.00')"));
            db.close();
        }
        QSqlDatabase::removeDatabase("seed");
    }

    void init()
    {
        QDir(backupDir()).removeRecursively();
        AppSettings::instance()->setBackupLastTime(QString());
    }

    // The copy is written as .part and renamed once verified; recording the time is
    // left to the caller (the GUI thread), never done by the backup itself.
    void test_backupIsRenamedOnceVerified()
    {
        BackupManager manager;
        const BackupManager::Result res = manager.performBackup();
        QVERIFY2(res.success, qPrintable(res.errorMessage));
        QVERIFY(res.backupPath.endsWith(".db"));
        QVERIFY(QFile::exists(res.backupPath));
        QVERIFY(res.finishedAt.isValid());
        QCOMPARE(backupFiles(), QStringList{ QFileInfo(res.backupPath).fileName() });
        QVERIFY(AppSettings::instance()->backupLastTime().isEmpty());
        BackupManager::recordSuccess(res);
        QCOMPARE(AppSettings::instance()->backupLastTime(), res.finishedAt.toString(Qt::ISODate));
        QVERIFY(!manager.needsBackup());
    }

    // A second folder gets the same verified copy (never a .part); a folder that cannot be
    // used leaves the main backup successful and says why.
    void test_secondFolderGetsTheCopy()
    {
        BackupManager manager;
        manager.setSecondFolder(m_dir.filePath("usb"));
        const BackupManager::Result res = manager.performBackup();
        QVERIFY2(res.success, qPrintable(res.errorMessage));
        QVERIFY(res.secondCopyError.isEmpty());
        QVERIFY(QFile::exists(res.secondCopyPath));
        QCOMPARE(QFileInfo(res.secondCopyPath).size(), res.bytesWritten);
        QCOMPARE(QDir(m_dir.filePath("usb")).entryList({ "*.part" }, QDir::Files).size(), 0);

        QFile blocker(m_dir.filePath("not-a-folder"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        QTest::qWait(1100);                                             // a new second, a new name
        manager.setSecondFolder(m_dir.filePath("not-a-folder"));
        const BackupManager::Result blocked = manager.performBackup();
        QVERIFY(blocked.success);
        QVERIFY(!blocked.secondCopyError.isEmpty());
    }

    // An unfinished copy left by a crash is removed by the next run and never counts.
    void test_stalePartFileRemoved()
    {
        QDir().mkpath(backupDir());
        QFile stale(QDir(backupDir()).filePath("laideal_2026-01-01_120000.db.part"));
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.write("cut off");
        stale.close();
        BackupManager manager;
        QVERIFY(manager.performBackup().success);
        QVERIFY(!stale.exists());
        QCOMPARE(backupFiles().filter(".part").size(), 0);
    }

    // A copy of the same second already exists: the run fails and keeps that copy as it was.
    void test_sameSecondKeepsTheExistingCopy()
    {
        QDir().mkpath(backupDir());
        const QDateTime now = QDateTime::currentDateTime();
        for (int s = 0; s < 5; ++s) {
            QFile f(QDir(backupDir()).filePath(now.addSecs(s).toString("'laideal_'yyyy-MM-dd_HHmmss'.db'")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("earlier copy");
        }
        BackupManager manager;
        const BackupManager::Result res = manager.performBackup();
        QVERIFY(!res.success);
        QVERIFY2(res.errorMessage.contains("hace un momento"), qPrintable(res.errorMessage));
        const QStringList files = backupFiles();
        QCOMPARE(files.size(), 5);
        for (const QString &name : files) {
            QFile f(QDir(backupDir()).filePath(name));
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), QByteArray("earlier copy"));
        }
    }

    // A last-backup time in the future (the clock was wrong then) does not stop backups.
    void test_futureLastTimeNeedsBackup()
    {
        AppSettings::instance()->setBackupEnabled(true);
        AppSettings::instance()->setBackupLastTime(QDateTime::currentDateTime().addDays(30).toString(Qt::ISODate));
        QVERIFY(BackupManager().needsBackup());
        AppSettings::instance()->setBackupLastTime(QDateTime::currentDateTime().addSecs(-60).toString(Qt::ISODate));
        QVERIFY(!BackupManager().needsBackup());
    }

    // One backup at a time: a call while another runs fails at once, without touching files.
    void test_secondConcurrentRunRefused()
    {
        BackupManager manager;
        // Hold the run open by locking the live DB for writing: VACUUM INTO waits on it.
        QSqlDatabase lock = QSqlDatabase::addDatabase("QSQLITE", "lock");
        lock.setDatabaseName(dbPath());
        QVERIFY(lock.open());
        {
            QSqlQuery q(lock);
            QVERIFY(q.exec("BEGIN EXCLUSIVE"));
        }
        QThread *first = QThread::create([&manager] { manager.performBackup(); });
        first->start();
        QTest::qWait(300);
        const BackupManager::Result second = BackupManager().performBackup();
        {
            QSqlQuery q(lock);
            QVERIFY(q.exec("COMMIT"));
        }
        QVERIFY(first->wait(30000));
        delete first;
        lock.close();
        QSqlDatabase::removeDatabase("lock");
        QVERIFY(!second.success);
        QVERIFY2(second.errorMessage.contains("en curso"), qPrintable(second.errorMessage));
    }

    // Recent copies so the newest-3 rule does not shadow the file under test.
    static QStringList filler(const QDate &last)
    {
        QStringList l;
        for (int i = 0; i < 3; ++i)
            l << last.addDays(-i).toString("'laideal_'yyyy-MM-dd'_100000.db'");
        return l;
    }

    void test_retentionPolicy()
    {
        const QDateTime now(QDate(2026, 6, 2), QTime(12, 0, 0));
        const QStringList names = filler(QDate(2026, 6, 1)) + QStringList{
            "laideal_2026-05-20_120000.db",  // 13 days -> recent, keep
            "laideal_2026-03-01_120000.db",  // month 2026-03, earlier -> DELETE
            "laideal_2026-03-15_120000.db",  // month 2026-03, last    -> keep
            "laideal_2026-02-10_120000.db",  // month 2026-02, only    -> keep
            "laideal_2020-01-01_120000.db",  // > 4 years -> DELETE
            "notabackup.db"                  // not our schema -> ignored (keep)
        };

        QCOMPARE(BackupManager::backupsToPrune(names, now),
                 (QStringList{ "laideal_2020-01-01_120000.db", "laideal_2026-03-01_120000.db" }));
    }

    // Age exactly 30 days is no longer "recent"; 29 days is.
    void test_boundary30Days()
    {
        const QDateTime now(QDate(2026, 6, 30), QTime(12, 0, 0));
        QStringList names = filler(QDate(2026, 6, 29));
        names << "laideal_2026-05-31_120000.db"   // exactly 30 days: monthly window, last of May
              << "laideal_2026-05-30_120000.db";  // 31 days: monthly, earlier -> delete
        QCOMPARE(BackupManager::backupsToPrune(names, now), QStringList{ "laideal_2026-05-30_120000.db" });

        const QDateTime now2(QDate(2026, 7, 1), QTime(12, 0, 0));
        QStringList names2 = filler(QDate(2026, 6, 30));
        names2 << "laideal_2026-06-02_120000.db"  // 29 days: recent, keep
               << "laideal_2026-06-01_120000.db"; // 30 days: sole monthly candidate, keep
        QCOMPARE(BackupManager::backupsToPrune(names2, now2).size(), 0);
    }

    // 4 years are calendar years (a leap day does not make the copy 1 day too old).
    void test_boundary4Years()
    {
        const QDateTime now(QDate(2026, 6, 2), QTime(12, 0, 0));
        const QStringList recent = filler(QDate(2026, 6, 1));
        QCOMPARE(BackupManager::backupsToPrune(recent + QStringList{ "laideal_2022-06-02_120000.db" }, now).size(), 0);
        QCOMPARE(BackupManager::backupsToPrune(recent + QStringList{ "laideal_2022-06-02_115900.db" }, now),
                 QStringList{ "laideal_2022-06-02_115900.db" });
    }

    // A clock before the newest copy, far after it, or a jump between the two
    // newest copies deletes nothing.
    void test_clockJumpPrunesNothing()
    {
        const QStringList old = { "laideal_2020-01-01_120000.db", "laideal_2020-01-02_120000.db",
                                  "laideal_2024-03-01_120000.db", "laideal_2024-03-15_120000.db" };
        QStringList names = filler(QDate(2026, 6, 1)) + old;
        QVERIFY(!BackupManager::backupsToPrune(names, QDateTime(QDate(2026, 6, 2), QTime(0, 0))).isEmpty());
        QVERIFY(BackupManager::backupsToPrune(names, QDateTime(QDate(2025, 1, 1), QTime(0, 0))).isEmpty());
        QVERIFY(BackupManager::backupsToPrune(names, QDateTime(QDate(2031, 1, 1), QTime(0, 0))).isEmpty());
        // The newest copy was made when the clock had already jumped forward.
        names << "laideal_2030-01-01_120000.db";
        QVERIFY(BackupManager::backupsToPrune(names, QDateTime(QDate(2030, 1, 1), QTime(13, 0))).isEmpty());
    }

    // The newest 3 copies survive even inside the monthly window.
    void test_newestThreeKept()
    {
        const QDateTime now(QDate(2026, 6, 10), QTime(12, 0, 0));
        QStringList names;
        for (int d = 1; d <= 5; ++d)
            names << QString("laideal_2026-05-0%1_120000.db").arg(d);
        // 05-03..05-05 protected, 05-02 is the month's survivor, 05-01 goes.
        QCOMPARE(BackupManager::backupsToPrune(names, now), QStringList{ "laideal_2026-05-01_120000.db" });
    }

    void test_backupWarning()
    {
        const QDateTime now(QDate(2026, 6, 10), QTime(12, 0, 0));
        const QString iso2d = now.addDays(-2).toString(Qt::ISODate);
        const QString iso4d = now.addDays(-4).toString(Qt::ISODate);
        QVERIFY(BackupManager::backupWarning(true, iso2d, QString(), now).isEmpty());
        QVERIFY(!BackupManager::backupWarning(true, iso4d, QString(), now).isEmpty());
        QVERIFY(!BackupManager::backupWarning(true, QString(), QString(), now).isEmpty());
        QVERIFY(BackupManager::backupWarning(true, iso4d, "disco lleno", now).contains("disco lleno"));
        QVERIFY(!BackupManager::backupWarning(false, iso2d, QString(), now).isEmpty());
    }

    void test_recordFailureThenSuccessClearsError()
    {
        BackupManager::Result bad;
        bad.errorMessage = "sin espacio";
        BackupManager::recordFailure(bad);
        QVERIFY(AppSettings::instance()->backupLastError().contains("sin espacio"));
        BackupManager::Result ok;
        ok.success = true;
        ok.finishedAt = QDateTime::currentDateTime();
        BackupManager::recordSuccess(ok);
        QVERIFY(AppSettings::instance()->backupLastError().isEmpty());
    }

    void test_emptyAndRecentOnly()
    {
        const QDateTime now(QDate(2026, 6, 2), QTime(12, 0, 0));
        QCOMPARE(BackupManager::backupsToPrune({}, now).size(), 0);

        const QStringList recent = {
            "laideal_2026-06-01_120000.db",
            "laideal_2026-05-15_120000.db"  // 18 days, still within the 30-day window
        };
        QCOMPARE(BackupManager::backupsToPrune(recent, now).size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestBackupManager)
#include "test_backup_manager.moc"
