// Tests for BackupManager: real copies of a throwaway DB (.part then rename, the
// same-second and concurrent-run guards, stale .part cleanup), and
// backupsToPrune() - the pure retention decision. Policy: keep every copy
// from the last 30 days, then one (the earliest) per calendar month for up to
// 4 years, drop the rest; non-matching names are ignored.

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

    void test_retentionPolicy()
    {
        const QDateTime now(QDate(2026, 6, 2), QTime(12, 0, 0));
        const QStringList names = {
            "laideal_2026-06-01_120000.db",  // 1 day   -> recent, keep
            "laideal_2026-05-20_120000.db",  // 13 days -> recent, keep
            "laideal_2026-03-01_120000.db",  // ~93 days, month 2026-03, first  -> keep
            "laideal_2026-03-15_120000.db",  // ~79 days, month 2026-03, second -> DELETE
            "laideal_2026-02-10_120000.db",  // ~112 days, month 2026-02, first -> keep
            "laideal_2020-01-01_120000.db",  // > 4 years -> DELETE
            "notabackup.db"                  // not our schema -> ignored (keep)
        };

        const QStringList del = BackupManager::backupsToPrune(names, now);

        QCOMPARE(del.size(), 2);
        QVERIFY(del.contains("laideal_2026-03-15_120000.db")); // monthly dup
        QVERIFY(del.contains("laideal_2020-01-01_120000.db")); // too old
        QVERIFY(!del.contains("laideal_2026-03-01_120000.db")); // monthly survivor
        QVERIFY(!del.contains("laideal_2026-06-01_120000.db")); // recent
        QVERIFY(!del.contains("notabackup.db"));                // ignored
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
