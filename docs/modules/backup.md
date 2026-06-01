# backup — automated SQLite snapshots (Verifactu Req. 4)

Lives at `src/backup/`. Closes the **Conservación de registros** requirement (Art. 8.2.c RD 1007/2023, Ley 58/2003 LGT prescription period — 4 años).

## Files
- `backup_manager.h` / `backup_manager.cpp` — `BackupManager` class (Qt-friendly, parented to MainWindow).
- `CMakeLists.txt` — static lib `backup`; depends on `Qt::Core`, `Qt::Sql`, `appsettings`, `sql_lite`.

## Public API

```cpp
class BackupManager : public QObject {
public:
    struct Result {
        bool    success = false;
        QString backupPath;     // absolute path written (empty on failure)
        QString errorMessage;   // Spanish, suitable for QMessageBox
        qint64  bytesWritten = 0;
    };
    Result  performBackup();
    bool    needsBackup(qint64 minIntervalSeconds = 24 * 3600) const;
    int     pruneOldBackups();
    QString backupDirectory() const;
};
```

## How it works

1. **Snapshot**: a dedicated `QSQLITE` connection named `laideal_backup_snapshot` opens the live DB and runs `VACUUM INTO '<target>'`. SQLite emits a transactionally-consistent point-in-time copy without taking an exclusive lock — the operator's current session is not interrupted.
2. **Verify**: the target file is re-opened on a second dedicated connection (`laideal_backup_verify`) with `QSQLITE_OPEN_READONLY`. `PRAGMA integrity_check` must return `ok`; any other verdict deletes the file and reports failure.
3. **Persist**: on success, `AppSettings::backupLastTime` is set to the current ISO timestamp and the settings JSON is saved atomically.
4. **Prune**: `pruneOldBackups()` walks the backup directory matching the timestamped filename pattern, keeps every file from the last 30 days, then one file per calendar month for the next 4 years, then deletes the rest.

## Filename convention

`<dbDir>/backups/laideal_yyyy-MM-dd_HHmmss.db` — same `.db` extension as the live DB so the operator manages a uniform set of files; the timestamp prefix sorts lexicographically by time, keeping the pruner trivial. The `backups/` subdir is created on demand next to `dbPath()`, isolating snapshots from the live file.

## Retention contract

| Age | Behaviour |
|-----|-----------|
| 0 — 30 days | every snapshot kept |
| 31 days — 4 years | one snapshot per (year, month) — first match wins, rest pruned |
| > 4 years | deleted |

The 4-year window matches the LGT tax-prescription period; the daily-for-30-days window keeps recent restorability cheap.

## Settings keys (`~/.laideal_settings.json`)

| Key | Type | Default | Notes |
|-----|------|---------|-------|
| `backup.enabled` | bool | `true` | gates `needsBackup()`; manual trigger is independent |
| `backup.last_time` | string (ISO-8601) | empty | populated after each successful backup |

The backup directory is **not** configurable separately — `BackupManager::backupDirectory()` returns `<QFileInfo(dbPath()).absolutePath()>/backups`, so backups always sit in a `backups/` subdir next to the live DB. One location for the operator to manage; one path to back up offsite.

## Wiring (MainWindow)

- Constructor: `m_backupManager = new BackupManager(this);` then `if (needsBackup()) QTimer::singleShot(3000, ...)` to defer the snapshot past the first paint.
- Auto path: silent on success (8 s status-bar message), `QMessageBox::warning` on failure — a failed backup is the only signal the operator gets.
- Manual path: `actionHacer_copia_de_seguridad` in the Herramientas menu calls `performBackup()` under `WaitCursor` and shows path + size in a `QMessageBox`.

## Why in-app instead of Windows Task Scheduler

The shop opens the app every working day. An in-app daily backup on startup covers the prescription window equivalently to a nightly scheduled task — and lets the user trigger an extra backup before leaving for offsite copy. If the shop ever runs the app irregularly (closed for vacation longer than a week), a Task Scheduler entry can be added separately via `release.ps1` without touching the module — `BackupManager::performBackup()` is callable from any context.

## What is *not* in scope

- **Offsite replication** (cloud / network share). Backups live next to `dbPath()`, so to mirror them off-machine the operator either points the DB itself at a synced folder (`dbPath` in settings) or copies the directory periodically. The module does not push files itself.
- **Encryption**. Backups are plain SQLite files. If the source DB ever gains row-level encryption, the snapshot inherits it — `VACUUM INTO` preserves the byte format.
- **Restore UI**. To restore a backup the operator copies the `.sqlite` file over `dbPath()` while the app is closed; no in-app restore flow exists yet.
