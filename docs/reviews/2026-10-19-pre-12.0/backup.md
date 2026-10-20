# Review: backup (src/backup/) - release 12.0

## Summary
- Small, readable module (226 lines) with a sound core: VACUUM INTO on a dedicated connection, read-only integrity_check, copy deleted on failure.
- Weak points are at the edges: partial or same-second files, one manager used from a worker thread and the GUI thread at once, retention keyed to a trusted clock, AppSettings touched off-thread.
- Unit coverage is only the pure `backupsToPrune` (2 tests); performBackup, needsBackup, pruneOldBackups and the failure paths have none. Docs have 8+ drifts (architecture.md still describes an older design).
- Legal (Req. 4) is met in normal use; clock jumps and same-disk storage are the risks. No Critical issue; 4 High.

## Findings

| Sev | Cat | file:line | Finding | Proposal | Effort |
|---|---|---|---|---|---|
| High ✅ FIXED 19-10-2026 | bug | backup_manager.cpp:84-85,106,116-118 | Target name has 1 s resolution. If a second backup starts in the same second (manual click right after auto, or double click), `VACUUM INTO` fails (output file exists) and the failure path `QFile::remove(target)` deletes the EXISTING good backup. | Never remove a file you did not create. Write to a temp name (next row), or if the target already exists add a suffix / fail without remove. | S |
| High ✅ FIXED 19-10-2026 | bug/design | backup_manager.cpp:106,160-163 | `VACUUM INTO` writes straight into `laideal_*.db`. A crash, power cut or kill during the copy (or a OneDrive lock) leaves a truncated file matching the pattern: it counts as a valid monthly survivor (so good copies of that month get pruned, :203-208) and `SelfCheck::newestBackup` (selfcheck.cpp:50-53) picks it as newest. | VACUUM INTO `laideal_<ts>.db.part`, verify, then `QFile::rename` to the final name. At start delete stale `*.part`. | S |
| High ✅ FIXED 19-10-2026 | bug | mainwindow.cpp:122-141 vs 1620-1632; backup_manager.cpp:21-22,94,130 | One `BackupManager` is shared by the auto worker thread and the manual GUI action. Connection names are constants: a manual backup while the worker is running (seconds on a big DB) makes `addDatabase` replace a connection in use (Qt warning, undefined behaviour) and both use the same timestamp. Nothing blocks the second call. | `std::atomic<bool> m_running` / QMutex in BackupManager (second call returns "ya hay una copia en curso"); or unique connection names (counter/QUuid). Disable the menu action while the worker runs. | S |
| High ✅ FIXED 19-10-2026 | design | backup_manager.cpp:165-167 (runs on worker, mainwindow.cpp:130) | `AppSettings` (QJsonObject `m_data`, no mutex in appsettings.h) is mutated and `save()` (tmp file + rename, appsettings.cpp:178+) runs on the worker thread while the GUI thread may read or save settings (aeat_chain_marker, Configuración). Data race and possible `.tmp` clash. | `performBackup()` should not touch AppSettings; the queued lambda (mainwindow.cpp:132) on the GUI thread calls `setBackupLastTime` + `save`. | S |
| Medium ✅ FIXED 19-10-2026 | bug | mainwindow.cpp:127-141,147-149 | Worker `QThread::create` has no parent and captures `this`; `~MainWindow` neither waits nor interrupts it. Closing the window during the backup (starts 3 s after launch, lasts seconds) gives "QThread destroyed while running" or a queued call to a dead object, and kills VACUUM midway (partial file, see above). | Keep the thread/QFuture as a member and `wait()` in the destructor; use a QPointer/context object for invokeMethod. | S |
| Medium ✅ FIXED 20-10-2026 | compliance/bug | backup_manager.cpp:195-202,219,165,71 | Retention depends on `QDateTime::currentDateTime()`. A clock jump forward (dead CMOS battery, wrong year) makes old copies `ageDays > 1460` or collapses the last 30 days to monthly: all backups can be deleted in one run, silently. A jump backward stores a future `last_time` (:165) so `needsBackup` (:71, negative secsTo) stays false until the clock catches up. | (a) `backupsToPrune`: if `now` is before the newest file timestamp or implausibly far after it, prune nothing. (b) Always keep the newest N (>=3) copies. (c) `needsBackup`: treat `last > now` as needing a backup. | M |
| Medium ✅ FIXED 19-10-2026 | compliance | backup_manager.cpp:44-49 | Backups sit next to the live DB (same disk); Req. 4 durability holds only if that folder is synced/offsite (backup.md says so; nothing warns or checks). Disk full: VACUUM INTO fails, handled and removed, but the failure shows only a transient message (mainwindow.cpp:133); repeated failure for weeks is invisible after a restart. | Persist `backup.last_success`/`last_error`; persistent warning when last success > 3 days (also in the self-check). Optional second folder setting (USB/second drive). Pre-check `QStorageInfo::bytesAvailable()` against DB size. | M |
| Medium ✅ FIXED 20-10-2026 | design | backup_manager.cpp:203-208 | The monthly survivor is the EARLIEST file of the month. The copy that lives 4 years lacks the last ~29 days of that month's invoices; only the live DB (never pruned) has them. If the live DB is lost, month-end copies are the useful ones. | Keep the LAST file per (year, month); update tests and docs. Product decision. | S |
| Medium ✅ FIXED 20-10-2026 | bug | backup_manager.cpp:196-201 | Boundaries: `ageDays < 30` keeps, `> 1460` deletes. 4*365 ignores leap days (deleted up to 1 day early); `daysTo` on local naive time shifts over DST; docs say "31 days". | Use `ts.addYears(4) < now` and `ts.addDays(30) > now`; boundary tests (29/30/31, 1460/1461). | S |
| Medium ✅ FIXED 19-10-2026 | bug | backup_manager.cpp:96,101 | No busy handling beyond Qt's default: if the GUI thread holds a write transaction VACUUM INTO can fail with SQLITE_BUSY and is retried only at the next start (>24 h). No WAL anywhere in src, so VACUUM INTO's read lock makes GUI commits wait for the whole copy; the docs claim no interruption. | `QSQLITE_BUSY_TIMEOUT=30000` for the snapshot connection; retry once; correct the docs wording. | S |
| Medium ✅ FIXED 20-10-2026 | design | appsettings.cpp:405; e2efixture.h:78 | `backup.enabled` has no UI toggle (only JSON); a false value silently disables Req. 4 backups with no warning. | Warn at start when disabled, or drop the switch. | S |
| Low | bug | backup_manager.cpp:216-225; backup_manager.h:262 | Header says "negative count = scan error"; never negative. `QFile::remove` failures (OneDrive lock) silent. | Log failures; fix the comment. | S |
| Low ✅ FIXED 20-10-2026 | bug | backup_manager.cpp:165-167 | `save()` result ignored. | Check and log. | S |
| Low | design | backup_manager.cpp:57-58 | Local-time names: DST fall-back hour can sort two copies out of chronological order (also `newestBackup`). | UTC or offset in the name (optional). | S |
| Low | design | backup_manager.cpp:44-49 | `backupDirectory()` is a const getter with a side effect (mkpath); `pruneOldBackups` creates the dir even just to prune. | Split into getter and `ensureBackupDirectory()`. | S |
| Low | bug | backup_manager.cpp:29-34,138-141 | When `q.next()` is false with no SQL error, `errorIfNotOk` returns empty: message "Fallo al verificar la copia: " with no detail. | Fallback text. | S |
| Low | compliance | (module) | Settings JSON (aeat_chain_marker, issuer data) is not in the snapshot; a restore on another PC needs it (requirements.md:48). | Document; optionally copy settings without secrets beside each snapshot. | S |
| Low | dead-code | - | None found. | - | - |

Coding-guidelines: English identifiers and comments, Spanish only in tr(); VACUUM INTO cannot bind a parameter and the quote doubling at :104-105 is the right handling. No other violations.

## Documentation drift

| Doc | Says | Code reality |
|---|---|---|
| docs/architecture.md:152 | `backupDirectory()` resolves "the configured root", default `<DocumentsLocation>/laideal_backups` | backup_manager.cpp:44-49: `<dbDir>/backups`, not configurable |
| docs/architecture.md:158 | filename `...HHmmss.sqlite` | backup_manager.cpp:57 `.db` |
| docs/architecture.md:155 | auto failure = `QMessageBox::warning` | mainwindow.cpp:132-134 sets the `m_result` label |
| docs/architecture.md:156 | manual result in a `QMessageBox` | mainwindow.cpp:1631-1632 `m_result` + file link |
| docs/modules/backup.md Wiring section | `QMessageBox` for manual and auto failure; no worker thread mentioned | worker thread (mainwindow.cpp:127-140); label |
| docs/modules/verifactu/verifactu-requirements.md:58 | auto failure `QMessageBox` warning | m_result text (mainwindow.cpp:133) |
| docs/modules/backup.md ("Restore UI") | copy "the `.sqlite` file" | files are `.db` |
| docs/modules/backup.md retention table | "31 days - 4 years" | code: `< 30` days kept (backup_manager.cpp:196), `> 1460` days deleted (:199) |
| docs/modules/backup.md:~21, architecture.md:149 | VACUUM INTO "without exclusive lock, session not interrupted" | read lock held for the copy in rollback-journal mode; commits wait |
| docs/modules/backup.md "Why in-app" | `performBackup()` "callable from any context" | not safe against itself or AppSettings from threads |
| backup_manager.h:262-263 | negative count = scan error | never negative |

## Test gaps

Covered: `backupsToPrune` - recent kept, one per month (earliest), over 4 years dropped, non-matching ignored, empty list (tests/test_backup_manager.cpp:21-51). Everything else is untested. Tests need a small seam to inject `now` into `pruneOldBackups`/`needsBackup`, plus QTemporaryDir + `setDbPath` + `AppSettings::loadFrom`.

### Unit (test_backup_manager)
- `test_performBackup_createsVerifiedCopy`: success, file in `<dir>/backups` matching the regex, `bytesWritten == size`, copy row count equals source, `backupLastTime` parsable.
- `test_performBackup_missingSource`: empty and non-existent dbPath -> success=false, message set, no file, last_time unchanged.
- `test_performBackup_notADatabase`: text file as dbPath -> failure, no leftover in backups/.
- `test_performBackup_sameSecondDoesNotDestroyExisting` (red today): two calls with the same timestamp; first file still exists and valid.
- `test_performBackup_unwritableBackupsDir`: a file named `backups` -> failure, no crash.
- `test_performBackup_noPartialLeftOnFailure`: no `laideal_*.db` / `.part` after failure.
- `test_performBackup_connectionsRemoved`: `QSqlDatabase::contains` false for both names after success and failure.
- `test_performBackup_snapshotIgnoresUncommitted`: open uncommitted write on a second connection; copy has only committed rows.
- `test_performBackup_onWorkerThread`: run via `QThread::create`, succeeds (guards mainwindow.cpp:127).
- `test_performBackup_rejectsConcurrentCall` (after the guard fix).
- `test_needsBackup_disabled / neverRun / olderThan24h / recent / invalidTimestamp / futureTimestamp`.
- `test_pruneOldBackups_removesOnlyOwnedOld`: temp dir with matching old files plus `notes.txt`, `laideal_manual.db`; only expected removed, count returned; missing dir returns 0.
- `test_backupsToPrune_boundaries`: ages 29/30/31 and 1460/1461 days; year-end (2025-12 vs 2026-01); malformed `laideal_2026-13-40_999999.db` ignored; unsorted input gives the same result.
- `test_backupsToPrune_clockJump`: now 5 years ahead / before newest file -> nothing deleted, newest N kept (after fix).
- `test_backupsToPrune_keepsLastOfMonth` (if the design changes).

### E2E (tests/test_e2e_app.cpp; backup is off in the fixture, e2efixture.h:78)
- `test_e2e_manualBackupAction`: trigger `actionHacer_copia_de_seguridad`; label "Copia de seguridad creada", file exists, integrity ok, rows equal live.
- `test_e2e_startupBackupWorker`: backup on, empty last_time, wait past the 3 s timer, status message appears; destroying the window mid-backup does not crash.
- `test_e2e_backupFailureShowsError`: unwritable target -> error label.
- `test_e2e_manualWhileAutoRunning`: second call refused cleanly.

### UI smoke (tests/ui/block_e.py E1, :80-86)
- E1 only checks that a new `*.db` exists. Add: size > 0, read-only open + `PRAGMA integrity_check` = ok, row count equals live, second click right after the first keeps the first file.
- E1b: auto path never runs (settings turn it off, ui_smoke.md:57): one scenario with backup on, empty last_time -> status "Copia de seguridad creada" within ~10 s.
- E1c: pre-seed `laideal_2020-01-01_000000.db` and a foreign file in backups/; after the backup the old one is gone and the foreign one stays.

### Self-check (selfcheck.cpp:50-53; test_selfcheck.cpp:57-65,184-186)
- `newestBackup` globs `laideal_*.db` and takes the last by name; `laideal_manual.db` / `laideal_old.db` sort after dated names and would be chosen, as would a partial file. Add `test_newestBackup_ignoresNonTimestampNames` and `test_newestBackup_skipsNonDatabase`; production fix: expose the name regex from BackupManager (`isBackupName()`/`newestBackup()`) and share it. S.
- New check (warn): newest backup older than 3 days -> surfaces silently failing auto backups. S.

## Questions / cross-module notes
- MainWindow reviewer: worker thread + shared manager + destructor race (mainwindow.cpp:122-149); label vs QMessageBox doc drift.
- AppSettings reviewer: no mutex; `setBackupLastTime`/`save` called from a worker thread (backup_manager.cpp:165-167).
- Self-check reviewer: `newestBackup` filter (selfcheck.cpp:50-53) should share BackupManager's pattern.
- Verifactu/compliance: earliest-of-month retention is a product decision; backups share the live DB's disk and omit the settings JSON (aeat_chain_marker), tied to the restore limit in progress_tracker.md:39 (3).
- Product: should `backup.enabled` be exposed in Configuración, and an extra backup folder (second drive) be offered for Req. 4 durability?
