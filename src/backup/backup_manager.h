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
        QString secondCopyPath;   // the copy in the second folder, when one is set and it worked
        QString secondCopyError;  // why the second copy failed (the main backup still succeeded)
    };

    // Snapshot the live DB via SQLite "VACUUM INTO" into "<name>.part", verify it
    // with PRAGMA integrity_check, then rename it to its final name - a cut-off copy
    // never looks like a backup. Synchronous; one run at a time in the process (a
    // second call fails at once). Applies the retention policy on success. Never
    // touches AppSettings, so it can run on a worker thread: the caller records the
    // success with recordSuccess() on the GUI thread.
    Result performBackup();

    // The second folder each verified backup is also copied to (pruned with the same
    // retention); empty = none. Set on the GUI thread before performBackup().
    void setSecondFolder(const QString &folder) { m_secondFolder = folder; }

    // Saves the time of a successful backup to AppSettings. GUI thread only.
    static void recordSuccess(const Result &result);

    // Stores a failed run (time + message) so the warning survives a restart. GUI thread only.
    static void recordFailure(const Result &result);

    // Pure: Spanish warning text, empty when all is well. Non-empty when backups are
    // disabled, or when the last successful backup (ISO time, empty = never) is more
    // than 3 days before `now`; lastError is appended when present.
    static QString backupWarning(bool enabled, const QString &lastSuccessIso,
                                 const QString &lastError, const QDateTime &now);
    // backupWarning() fed from AppSettings and the current time. GUI thread only.
    static QString currentBackupWarning();

    // True when the recorded last-backup timestamp is older than
    // minIntervalSeconds (default 24h) or no backup has ever run.
    bool needsBackup(qint64 minIntervalSeconds = 24 * 3600) const;

    // Apply the retention policy (see backupsToPrune). Returns the number of
    // files deleted.
    int pruneOldBackups();

    // Pure retention decision (no file I/O): given backup file NAMES (schema
    // laideal_yyyy-MM-dd_HHmmss.db) and the reference time, return the subset to
    // delete - always keep the newest 3 copies, every copy from the last 30 days,
    // then the LAST copy of each calendar month up to 4 years, drop the rest. A
    // suspect clock (now before the newest copy, or over 45 days after it, or a
    // 45-day jump between the two newest) prunes nothing. Names that do not match
    // the schema are ignored (kept). Exposed for unit testing; pruneOldBackups() wraps it
    // with the directory listing + removal.
    static QStringList backupsToPrune(const QStringList &fileNames, const QDateTime &now);

private:
    // Absolute path of the backups/ subdirectory next to dbPath(); creates nothing.
    QString backupDirectory() const;
    // The same path, with the directory created if missing.
    QString ensureBackupDirectory() const;
    static QString timestampedFileName();
    static void removeStalePartFiles(const QString &dir);
    void copyToSecondFolder(Result &res) const;
    QString m_secondFolder;
};

#endif // BACKUP_MANAGER_H
