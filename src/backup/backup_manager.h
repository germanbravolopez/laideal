#ifndef BACKUP_MANAGER_H
#define BACKUP_MANAGER_H

#include <QObject>
#include <QString>

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
    };

    // Snapshot the live DB via SQLite "VACUUM INTO". Synchronous. Verifies
    // the copy with PRAGMA integrity_check; deletes it and reports failure
    // if the check does not return "ok". Applies the retention policy on
    // success and persists the timestamp to AppSettings.
    Result performBackup();

    // True when the recorded last-backup timestamp is older than
    // minIntervalSeconds (default 24h) or no backup has ever run.
    bool needsBackup(qint64 minIntervalSeconds = 24 * 3600) const;

    // Apply the retention policy: keep every backup from the last 30 days,
    // then one per calendar month for up to 4 years. Returns the number of
    // files deleted (negative count = scan error).
    int pruneOldBackups();

    // Resolved absolute path of the backups/ subdirectory next to dbPath()
    // (creates it if missing).
    QString backupDirectory() const;

private:
    static QString timestampedFileName();
};

#endif // BACKUP_MANAGER_H
