#ifndef BACKUP_MANAGER_H
#define BACKUP_MANAGER_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

// Verifactu Req. 4 (RD 1007/2023 Art. 8.2.c): durable archive of the live
// SQLite DB during the 4-year tax prescription window. See
// docs/modules/backup.md for the retention contract.
class BackupManager : public QObject
{
    Q_OBJECT
public:
    explicit BackupManager(QObject *parent = nullptr);

    struct Result {
        bool    success = false;
        QString backupPath;     // absolute path of the created file (empty on failure)
        QString errorMessage;   // localized Spanish message suitable for QMessageBox
        qint64  bytesWritten = 0;
        QDateTime finishedAt;   // when the copy was verified (success only)
    };

    // Snapshot the live DB via SQLite "VACUUM INTO" into "<name>.part", verify it
    // with PRAGMA integrity_check, then rename it to its final name - a cut-off copy
    // never looks like a backup. Synchronous; one run at a time in the process (a
    // second call fails at once). Applies the retention policy on success. Never
    // touches AppSettings, so it can run on a worker thread: the caller records the
    // success with recordSuccess() on the GUI thread.
    Result performBackup();

    // Saves the time of a successful backup to AppSettings. GUI thread only.
    static void recordSuccess(const Result &result);

    // True when the recorded last-backup timestamp is older than
    // minIntervalSeconds (default 24h) or no backup has ever run.
    bool needsBackup(qint64 minIntervalSeconds = 24 * 3600) const;

    // Apply the retention policy: keep every backup from the last 30 days,
    // then one per calendar month for up to 4 years. Returns the number of
    // files deleted (negative count = scan error).
    int pruneOldBackups();

    // Pure retention decision (no file I/O): given backup file NAMES (schema
    // laideal_yyyy-MM-dd_HHmmss.db) and the reference time, return the subset to
    // delete - keep every copy from the last 30 days, then one per calendar
    // month for up to 4 years, drop the rest. Names that do not match the schema
    // are ignored (kept). Exposed for unit testing; pruneOldBackups() wraps it
    // with the directory listing + removal.
    static QStringList backupsToPrune(const QStringList &fileNames, const QDateTime &now);

private:
    // Resolved absolute path of the backups/ subdirectory next to dbPath()
    // (creates it if missing).
    QString backupDirectory() const;
    static QString timestampedFileName();
    static void removeStalePartFiles(const QString &dir);
};

#endif // BACKUP_MANAGER_H
