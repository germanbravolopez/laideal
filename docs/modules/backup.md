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
    static void recordSuccess(const Result &result);   // GUI thread
    bool    needsBackup(qint64 minIntervalSeconds = 24 * 3600) const;
    int     pruneOldBackups();
    QString backupDirectory() const;
};
```

## How it works

0. **Guards**: one backup at a time in the process. The automatic one runs on a worker thread and the manual one on the GUI thread, and a second call fails at once with "Ya hay una copia de seguridad en curso". Stale `laideal_*.db.part` files left by a cut-off run are removed. If a copy with this second's name already exists, the run fails and leaves that copy as it is. A run only ever removes files it created.
1. **Snapshot**: a dedicated `QSQLITE` connection named `laideal_backup_snapshot` (busy timeout 30 s) opens the live DB and runs `VACUUM INTO '<final name>.part'`. SQLite emits a transactionally-consistent point-in-time copy. While it reads, a ticket being saved waits for it (there is no WAL), and the backup waits for a save in progress.
2. **Verify**: the `.part` file is re-opened on a second dedicated connection (`laideal_backup_verify`) with `QSQLITE_OPEN_READONLY`. `PRAGMA integrity_check` must return `ok`, then the file is renamed to its final `.db` name. Any other verdict deletes the `.part` and reports failure, so a cut-off copy never matches the backup pattern.
3. **Persist**: `performBackup()` never touches `AppSettings`, because it may run off the GUI thread. The caller passes the result to `BackupManager::recordSuccess()` on the GUI thread, which saves `backup.last_time` (`Result::finishedAt`). `needsBackup()` also treats a last time in the future (a wrong clock then) as due.
4. **Prune**: `pruneOldBackups()` walks the backup directory matching the timestamped filename pattern, keeps every file from the last 30 days, then one file per calendar month for the next 4 years, then deletes the rest. The retention *decision* lives in the pure static `BackupManager::backupsToPrune(fileNames, now)` (no filesystem); `pruneOldBackups()` just lists the directory, calls it, and removes the returned names. `backupsToPrune` is unit-tested in `tests/test_backup_manager.cpp`, along with real copies of a throwaway DB (rename after verifying, stale `.part`, same second, concurrent run, a future last time).

## Filename convention

`<dbDir>/backups/laideal_yyyy-MM-dd_HHmmss.db` — same `.db` extension as the live DB so the operator manages a uniform set of files; the timestamp prefix sorts lexicographically by time, keeping the pruner trivial. The `backups/` subdir is created on demand next to `dbPath()`, isolating snapshots from the live file.

## Retention contract

| Age | Behaviour |
|-----|-----------|
| newest 3 | always kept, whatever their age |
| 0 — 30 days | every snapshot kept (`ts.addDays(30) > now`) |
| 30 days — 4 years | one snapshot per (year, month): the **last** of the month, the one closest to the month's end; the rest pruned |
| > 4 years | deleted (`ts.addYears(4) < now`) |

A wrong clock never decides what is deleted. Nothing is pruned when "now" is before the newest copy or more than 45 days after it, or when the two newest copies are more than 45 days apart (a copy made with a wrong clock).

The 4-year window matches the LGT tax-prescription period; the daily-for-30-days window keeps recent restorability cheap.

## Settings keys (`~/.laideal_settings.json`)

| Key | Type | Default | Notes |
|-----|------|---------|-------|
| `backup.enabled` | bool | `true` | gates `needsBackup()`; manual trigger and the pre-migration copy of a new version are independent |
| `backup.last_time` | string (ISO-8601) | empty | populated after each successful backup |
| `backup.last_version` | string (`X.Y`) | empty | the version whose first start backed up the DB before migrating it |
| `backup.second_folder` | string (path) | empty | optional second folder (a USB stick, another drive; Configuración → *Segunda carpeta de copias*): each verified backup is also copied there as `.part` + rename and pruned with the same retention. A failed copy leaves the main backup successful and shows why |
| `backup.last_error` | string | empty | time + reason of the last failed backup (`recordFailure`); cleared by the next success |

At every start MainWindow shows `BackupManager::currentBackupWarning()` in the result panel: backups switched off, or no correct backup for more than 3 days (with the last error). The pure `backupWarning(enabled, lastSuccess, lastError, now)` is unit-tested.

The backup directory is **not** configurable separately — `BackupManager::backupDirectory()` returns `<QFileInfo(dbPath()).absolutePath()>/backups`, so backups always sit in a `backups/` subdir next to the live DB. One location for the operator to manage; one path to back up offsite.

## Wiring (MainWindow)

- `mainwindowInitialSettings()`: creates `m_backupManager`. On the first start of a new version (`backup.last_version` differs from `Updater::currentVersion()`), it takes a synchronous backup **before** `migrateDatabase()`, so the database as the previous version left it is kept. A failure is shown in the result panel, and then `migrateDatabase(db, false)` runs only the schema steps: the repairs that rewrite rows wait until a later start takes the copy. `migrateDatabase()` returns `false` when a step failed, and the result panel says so too.
- Constructor: `if (needsBackup()) QTimer::singleShot(3000, ...)` defers the daily snapshot past the first paint and runs it on a worker thread, kept in `m_backupThread`. `~MainWindow` waits for it.
- Auto path: silent on success (8 s status-bar message), a red result-panel message on failure — a failed backup is the only signal the operator gets.
- Manual path: *Hacer copia de seguridad ahora* calls `performBackup()` under `WaitCursor` and shows path + size in the result panel.

## Why in-app instead of Windows Task Scheduler

The shop opens the app every working day. An in-app daily backup on startup covers the prescription window equivalently to a nightly scheduled task — and lets the user trigger an extra backup before leaving for offsite copy. If the shop ever runs the app irregularly (closed for vacation longer than a week), a Task Scheduler entry can be added separately via `release.ps1` without touching the module — `BackupManager::performBackup()` is callable from any context.

## What is *not* in scope

- **Offsite replication** (cloud / network share). Backups live next to `dbPath()`, so to mirror them off-machine the operator either points the DB itself at a synced folder (`dbPath` in settings) or copies the directory periodically. The module does not push files itself.
- **Encryption**. Backups are plain SQLite files. If the source DB ever gains row-level encryption, the snapshot inherits it — `VACUUM INTO` preserves the byte format.
- **Restore UI**. To restore a backup the operator copies the `.sqlite` file over `dbPath()` while the app is closed; no in-app restore flow exists yet.
