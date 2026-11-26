# backup: automated SQLite snapshots (Verifactu requirement 4)

`src/backup/` (`backup_manager.h/cpp`, static lib `backup`: `Qt::Core`, `Qt::Sql`, `appsettings`, `sql_lite`). Closes the record-retention requirement (Art. 8.2.c RD 1007/2023; 4-year LGT prescription period), see [Verifactu](verifactu/README.md). Tests: `tests/test_backup_manager.cpp` (`backupsToPrune`, `backupWarning`, real copies of a throwaway DB: rename after verifying, stale `.part`, same second, concurrent run, future last time, second folder), see [unit suites](../testing/unit_suites.md).

## Public API

| Member | Purpose |
|--------|---------|
| `performBackup()` | Snapshot, verify, rename, prune. Returns a `Result` |
| `setSecondFolder(folder)` | Optional second destination; set on the GUI thread before `performBackup()` |
| `recordSuccess(result)` / `recordFailure(result)` | Persist the outcome in `AppSettings` (GUI thread only) |
| `needsBackup(minIntervalSeconds = 24 h)` | True when the last backup is older, never ran, or is dated in the future |
| `pruneOldBackups()` | Applies the retention policy; returns files deleted |
| `backupsToPrune(fileNames, now)` | Pure retention decision, no I/O |
| `backupWarning(enabled, lastSuccessIso, lastError, now)` / `currentBackupWarning()` | Pure warning text / the same fed from `AppSettings` |

`Result`: `success`, `skipped`, `backupPath`, `errorMessage` (Spanish), `bytesWritten`, `finishedAt`, `secondCopyPath`, `secondCopyError`.

- **`skipped`** means refused, not failed: a run is already going, or a copy with this second's name exists (that copy is left as it is). Callers show a notice (manual) or stay silent (automatic), and it is never stored as the last error.

## How a backup runs

1. **Guards.** One backup at a time in the process (the automatic one runs on a worker thread, the manual one on the GUI thread). Stale `laideal_*.db.part` files are removed. A copy that cannot be removed while pruning (locked by OneDrive, say) is logged and left for the next run. A run only removes files it created.
2. **Snapshot.** A dedicated connection (`laideal_backup_snapshot`, busy timeout 30 s) runs `VACUUM INTO '<final name>.part'`: a consistent point-in-time copy. The read lock it holds (rollback-journal mode) makes a ticket being saved wait, and the backup waits for a save in progress.
3. **Verify.** The `.part` is reopened read-only on `laideal_backup_verify`; `PRAGMA integrity_check` must return `ok`, then it is renamed to its final name. Any other verdict deletes it and reports failure, so a cut-off copy never matches the backup pattern.
4. **Second folder** (when set): the verified backup is also copied there as `.part` + rename and pruned with the same retention. A failed copy leaves the main backup successful and `secondCopyError` says why. A second folder equal to the backups folder is reported as such ("elija otra").
5. **Persist.** `performBackup()` never touches `AppSettings` (it may run off the GUI thread); the caller passes the result to `recordSuccess()` (saves `backup.last_time` from `finishedAt`) and `recordFailure()`.
6. **Prune** on success.

## Files and retention

`<dbDir>/backups/laideal_yyyy-MM-dd_HHmmss.db` (local time; created on demand next to `dbPath()`, not configurable). The timestamp sorts by time, which keeps the pruner trivial.

| Age | Kept |
|-----|------|
| newest 3 | always, whatever their age |
| 0 - 30 days | every snapshot |
| 30 days - 4 years | the last snapshot of each (year, month) |
| over 4 years | none |

A wrong clock never decides what is deleted: nothing is pruned when "now" is before the newest copy or more than 45 days after it, or when the two newest copies are more than 45 days apart. Names that do not match the pattern are ignored. `pruneOldBackups()` lists the directory, calls `backupsToPrune` and removes the returned names.

## Settings (`~/.laideal_settings.json`)

| Key | Default | Notes |
|-----|---------|-------|
| `backup.enabled` | `true` | gates `needsBackup()`; the manual backup and the pre-migration copy are independent |
| `backup.last_time` | empty | ISO-8601, set after each successful backup |
| `backup.last_version` | empty | the version whose first start backed up the DB before migrating it |
| `backup.second_folder` | empty | Configuración → *Segunda carpeta de copias* (USB stick, another drive) |
| `backup.last_error` | empty | time + reason of the last failed backup; cleared by the next success |

At every start MainWindow shows `currentBackupWarning()` in the result panel: backups switched off, or no correct backup for more than 3 days (with the last error).

## Wiring (MainWindow)

- `mainwindowInitialSettings()` creates `m_backupManager`. On the first start of a new version (`backup.last_version` differs from `Updater::currentVersion()`) it takes a synchronous backup before `migrateDatabase()`. If that fails, the failure is shown and `migrateDatabase(db, false)` runs only the schema steps (row-rewriting repairs wait for a start that has the copy). A failed migration step is reported in the result panel.
- The constructor defers the daily snapshot (`needsBackup()`, 3 s timer) to a worker thread, kept in `m_backupThread`; `~MainWindow` waits for it. Success is silent (8 s status-bar message); failure is a red result-panel message.
- *Hacer copia de seguridad ahora* runs `performBackup()` under `WaitCursor` and shows path and size.

## Out of scope

- **Offsite replication.** Point `dbPath` at a synced folder or copy the directory.
- **Settings.** The snapshot is the database only; `~/.laideal_settings.json` (issuer data, `aeat_chain_marker`, the service key) is not copied and must be kept apart for a restore on another PC.
- **Encryption.** Backups are plain SQLite; `VACUUM INTO` keeps whatever format the source has.
- **Restore UI.** Copy the `.db` over `dbPath()` with the app closed.
