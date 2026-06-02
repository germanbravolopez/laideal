// Tests for BackupManager::backupsToPrune() - the pure retention decision
// (which backup filenames to delete), extracted from pruneOldBackups() so it
// can be exercised without touching the filesystem. Policy: keep every copy
// from the last 30 days, then one (the earliest) per calendar month for up to
// 4 years, drop the rest; non-matching names are ignored.

#include <QtTest>
#include <QDateTime>

#include "backup_manager.h"

class TestBackupManager : public QObject
{
    Q_OBJECT

private slots:
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
