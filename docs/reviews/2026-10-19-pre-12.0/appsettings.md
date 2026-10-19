# Review: appsettings (src/appsettings/) - release 12.0

Reviewer scope: AppSettings, SettingsDialog, AppLanguage, their docs and tests. Read-only; nothing built or run.

## Summary

- Code is small, readable, mostly well guarded; DPAPI at rest works and secrets are not logged (only key names).
- Two real behaviour bugs: (1) `aeat_installation` (NumeroInstalacion) and `aeat_chain_marker` are set in memory but never saved by their writers, so they depend on an unrelated later `save()`; (2) leaving the direct connection with production checked silently keeps `aeat_direct_production=true`, so coming back needs no production confirmation.
- Gateway PRODUCCION checkbox has no confirmation (only the direct one has); a corrupt/half-written settings file is silently replaced by defaults on the next save.
- Docs: README configuration table and the Verifactu README table list several wrong JSON key names and miss most keys. Unit tests cover DPAPI and the dialog's direct-AEAT flow, but not load/save/migration/defaults, and e2e/UI/self-check barely touch the module.

## Findings

| # | Severity | Category | file:line | Finding | Proposal | Effort |
|---|----------|----------|-----------|---------|----------|--------|
| 1 | High | bug/compliance | appsettings.cpp:381-389, 378-379; verifactuintegration.cpp:34,44-45; mainwindow.cpp:633,641 | `aeatInstallationNumber()` generates a random `LAIDEAL-xxxxxxxx` and `setAeatChainMarker()` stores the marker, but neither calls `save()`. Only `Configuración` accept and `BackupManager` (backup.cpp:167, skipped when `backup.enabled=false`) save. If the app closes/crashes before one of those, the next start generates a different NumeroInstalacion (it is in every record's SistemaInformatico) and a stale chain marker (forces a full 2-year resync). Also the getter is non-const with a hidden write side effect. | Save immediately in `aeatInstallationNumber()` when it creates the value, and in `setAeatChainMarker()` (cheap, rare) or have the callers call `save()`. Add test (see Test gaps). | S |
| 2 | Medium | bug/compliance | settingsdialog.cpp:312-342, 365 | Switching from direct to the gateway with `chkDirectProduction` still checked: `directProduction` is false (connection != aeat) so the "Salir de PRODUCCION?" question and the pending-record warning fire, but line 365 then saves `aeat_direct_production = m_vDirectProduction->isChecked()` = true. Later switching back to direct finds the stored flag already true, so the "Pasar a PRODUCCION?" confirmation (line 322) is skipped: production re-entered silently. The hidden group's state is the cause. | Save `directProduction` (connection==aeat && checked) or, when leaving the direct connection, force the flag to false and uncheck the box. Add dialog test. | S |
| 3 | Medium | compliance/design | settingsdialog.cpp:218-219, 363 | The gateway environment checkbox (`verifactu.environment` PRODUCTION) switches to real AEAT through IreneSolutions with no confirmation, unlike the direct one. Docs say production switches need confirmation. | Same Yes/No question when going TESTING->PRODUCTION (and leaving). Test mirrors `test_directProductionNeedsConfirmation`. | S |
| 4 | Medium | bug (data loss) | appsettings.cpp:149-157, 143-147, 178-209 | A settings file that fails to parse (or cannot be opened) is ignored with defaults and returns false; the next `save()` (any Configuración accept, backup timestamp) overwrites the corrupt file, losing db path, NIF, service key, certificate and password with no copy. Same for a missing main file after a crash between `QFile::remove` and `rename` (line 203-204): `load()` sees "no file", runs the first-run path and the leftover `.tmp` (the only good copy) is overwritten by the next save. | On parse error rename the bad file to `.laideal_settings.json.corrupt-<timestamp>` before defaults; on load, if the main file is missing but `<file>.tmp` exists and parses, adopt it. Log it. | M |
| 5 | Medium | bug (robustness) | appsettings.cpp:184-208 | `save()` comment says atomic and "fsync-equivalent" but `QFile::flush()` is not an fsync, and remove+rename leaves a window with no file (see 4). `QSaveFile` (or `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`) does this properly. The code also has no return-check on `tmp.flush()`. | Replace with `QSaveFile` + `commit()`. | S |
| 6 | Medium | security | appsettings.cpp:274-281 | Migration of `~/.verifactu_key` copies the key into the encrypted setting but never removes or warns about the legacy plaintext file; `~/.laideal_cfg` the same (not secret). The plaintext key stays on disk indefinitely. | After successful `save()`, delete (or rename to `.migrated`) the legacy key file; or document it for the shop. | S |
| 7 | Low | security | appsettings.cpp:33-43 | `encryptSecret` falls back to storing plaintext on DPAPI failure (only a qWarning). `encryptSecretsAtRest` then retries every load, so it self-heals; acceptable, but the dialog setter path (`setVerifactuServiceKey`) stores plaintext silently on failure with no user signal. | Keep; add a test hook or return value so the dialog can warn. Document in architecture.md. | S |
| 8 | Low | bug | appsettings.cpp:361-362,375-376 | `setVerifactuServiceKey/ setAeatCertificatePassword` encrypt unconditionally: a value that already starts with `dpapi:v1:` (e.g. a user pastes a stored blob) is double-encrypted and then decrypts to the blob text. Also `decryptSecret` of a corrupt base64 blob yields empty and the dialog then saves "" back, silently clearing the key on the next OK (warning only in log). | Guard: if `isEncrypted(v)` store as is; in the dialog, warn when stored value is non-empty but decrypts to empty ("clave ilegible, vuelva a introducirla"). | S |
| 9 | Low | bug | settingsdialog.cpp:397-406, 366 | A saved thumbprint that is no longer in the Windows store (certificate expired/removed) is not in the combo, so the combo shows ".pfx" and accepting Configuración silently erases the thumbprint. | Add the stored thumbprint as an entry "no encontrado en el almacén (thumbprint)" or keep it unless the user picks another. | S |
| 10 | Low | design | settingsdialog.cpp:229; appsettings.cpp:114 | Floor date default `2026-09-01` is hardcoded twice (dialog fallback and applyDefaults). The dialog allows any date (including future, which hides all pending/refused invoices from the status bar and the startup recovery) with no warning. | One shared constant; warn when the floor is in the future or later than the oldest unsent record. | S |
| 11 | Low | design | appsettings.cpp:103-108 | Default business name, address and city are the shop's real data hardcoded in source; `applyDefaults` also re-fills them if the user blanks name/address/city (user cannot have an empty city on tickets). Acceptable for a single-shop app but counters the "no shop data" rule. | Decide: keep (document) or default to empty with a first-run prompt. | S |
| 12 | Low | design | appsettings.cpp:111-127 vs getters 391,399,405 | Defaults are written in `applyDefaults` and repeated as `bln(..., true)` fallbacks in getters; paper width `80` appears in three places. Harmless but easy to drift. | One `kDefault*` set of constants. | S |
| 13 | Low | docs/design | main.cpp:78-90; settingsdialog.cpp:346 | DB path: no validation in the dialog (empty, nonexistent, folder); the dialog note says restart is needed but accept never prompts, and main only re-validates at startup. A typo is saved and the app fails on next launch (main then reopens the dialog, so recoverable). NIF/service key/phone are not validated either (e.g. NIF format). | Validate `QFileInfo(db).isFile()` on accept (warn, allow override); optional NIF check shared with verifactu. | S |
| 14 | Low | design | appsettings.h:14-182 | Singleton with no locking; `aeat chain marker` setters are called from the direct backend (lambda config). If any ever runs from `QThreadPool` (aeattransport.cpp:137 runs transport in a pool) the `m_data` QJsonObject is not thread-safe. Callbacks appear to be delivered on the main thread, but nothing enforces it. | Confirm with verifactu reviewer; add `Q_ASSERT(QThread::currentThread()==qApp->thread())` in the setters. | S |
| 15 | Low | dead-code/docs | appsettings.h:161, 28; test_appsettings.cpp:97-99; test_settingsdialog.cpp:5 | Stale comments: `encryptSecretsAtRest` doc says only the service key (it also does the certificate password); `loadFrom` "Not used in production" but main.cpp:29-37 (`--settings`) and selfcheckdialog.cpp:179 use it; a misplaced "ingresos IVA rate" comment above `test_initialLanguageFromInstaller`; test_settingsdialog header says "never accepted" but several tests call accept. `taxes.iva_rate` key kept "for older versions" is never written, so the comment in h:82-85 describes a key nothing creates. | Fix comments. | S |
| 16 | Low | design | main.cpp:29-37; appsettings.cpp:134-141 | `--self-check` promises "settings only read" (smoke_test.md:28), but `settings->load()` in main may `save()` the live file (first run, or encrypting a plaintext secret) before self-check starts, and `--settings <nonexistent>` creates that file. | Document, or have self-check start before any load that can write. | S |
| 17 | Low | dead-code | appsettings.cpp:350-351, 9 | Empty "Taxes" section header; `QTextStream`, `QFileInfo` fine; `<QDebug>` used. Only the empty comment block is dead. `ivaRate()`/`kIngresosIvaRate` are used. `paperWidthMm` stored as double. | Remove empty banner. | S |
| 18 | Low | security | selfcheck.cpp:62-73 | Self-check copy blanks service key/cert password, which is right; but `applyDefaults` + `encryptSecretsAtRest` run on that copy and write the temp settings with the certificate thumbprint/path; the temp dir is deleted at the end, a crashed run is cleaned next start (selfcheckdialog.cpp:147-151). Fine; noted for completeness. | None. | - |

No secret is logged: `qDebug` lines print key names, paths and "encrypted" only. `SettingsDialog` never logs values. Secrets pass through signals (`testConnectionRequested`, `aeatSelfTestRequested`) in memory only. AppLanguage is trivial and correct (static translator guarded by `installed`; `en` removes it; unknown languages behave like es).

## Documentation drift

| Doc location | Says | Code reality |
|--------------|------|--------------|
| README.md:130 | `db.path` | `database.path` (appsettings.cpp:304). Same wrong key in docs/architecture.md:177 (`db.path`) and docs/modules/sql_lite.md:23 |
| README.md:131 | IVA rate `app.ivaRate` "default VAT percentage" | Not a setting: fixed 21 % (appsettings.h:86); architecture.md:177 is right. Remove the row (UI smoke E2 even checks the dialog has no IVA field) |
| README.md:132 | `report.*` "monthly/quarterly/annual HTML reports" | `reports.root` single root (appsettings.cpp:310); output is PDF in `Contabilidad/Listados` subfolders |
| README.md:134 | `verifactu.name` | `verifactu.company_name` (appsettings.cpp:357). Same in docs/modules/verifactu/README.md:~187 |
| README.md:135 | `verifactu.serviceKey` | `verifactu.service_key` (appsettings.cpp:361), stored `dpapi:v1:` encrypted. Same wrong key in verifactu/README.md:~188, architecture.md:319 area and INDEX.md:160 wording is fine |
| README.md:136 | `verifactu.production` false/true | `verifactu.environment` = `TESTING` / `PRODUCTION` string (appsettings.cpp:364-365). Same in verifactu/README.md `verifactu.production` row |
| README.md:129-136 (whole table) | Lists 8 settings | Missing: `print.enable` ("Yes"/"No" strings, not bool), `print.printer_name`, `print.paper_width_mm`, `print.use_status_api`, `business.phone`, `updater.check_on_startup`, `backup.enabled`/`backup.last_time`, `verifactu.connection`, `aeat_*` keys, `pending_recovery_enabled`/`pending_recovery_floor_date` (only in prose in verifactu README:120). Also `print.enable`/`use_status_api` are the only string-typed booleans; document the "Yes"/"No" format for anyone hand-editing the JSON (the UI smoke seed.py:56 relies on it) |
| docs/modules/verifactu/README.md:183-203 | Config table | Missing `verifactu.pending_recovery_enabled`, `pending_recovery_floor_date` (and its default 2026-09-01 and semantics: payment date vs reception date), `aeat_direct_production` OK. |
| docs/modules/verifactu/README.md:~205 | "`initialize()` is non-fatal - it only warns if NIF or name is empty" | verifactuintegration.cpp:30-34 returns false and `qCritical`s |
| docs/architecture.md:171 | Only the service key is encrypted, "`encryptSecretsAtRest()` ... legacy plaintext value" | Also `aeat_certificate_password` (appsettings.cpp:287). Also not documented: plaintext fallback on DPAPI failure; legacy `~/.verifactu_key` stays on disk |
| docs/architecture.md:175-177 | Settings groups list | Lacks `print.use_status_api`, `backup.*`, `app.language` key name only in the previous paragraph, `verifactu.connection`, `aeat_*`, pending recovery; "Verifactu NIF/name/serviceKey/production" is not key names. Dialog description lacks the Direct-AEAT group, certificate choice, production confirmations, "Prueba con la AEAT", status API diagnostic |
| docs/INDEX.md:136 | `test_settingsdialog` = "service-key show/hide toggle" | Also direct connection, production confirmations, self-test button state, leaving-direct guard |
| docs/testing/unit_suites.md:76-77 | test_appsettings/test_settingsdialog description; "never accepted" | Dialog tests accept; direct-AEAT group and production guards untested in the doc. Update both |
| docs/INDEX.md:171 | "All configurable settings (paths, IVA, ...)" | IVA is not configurable |
| appsettings.h:28, 161 | see finding 15 | |

## Test gaps

Behaviour -> test map (existing): DPAPI marker/round trip/passthrough/empty (test_appsettings); derived report paths; fixed IVA; encrypt-at-rest migration and no re-encrypt (service key only); `initialLanguage`; release notes resource and parity; dialog: key mask/reveal, direct group visibility, certificate enabling, self-test signal, direct production confirm and leaving-guards, self-test button state.

### Unit (test_appsettings) - untested behaviours

| Proposed test | Assertions |
|---------------|-----------|
| `test_defaultsOnEmptyFile` | `loadFrom("{}")`: language es (or installer), business defaults, environment TESTING, pending_recovery_enabled true, floor date `2026-09-01`, reports root `~/Tintoreria`, updater/backup true, paper width 80; and that defaults are persisted (rawValue) |
| `test_defaultsDoNotOverrideExplicitFalse` | file with `backup.enabled=false`, `updater.check_on_startup=false`, `pending_recovery_enabled=false` stay false after load/save round trip |
| `test_missingFileCreatesDefaultsAndLegacyMigration` | path not existing: file created, `load()` returns false; (needs HOME seam, or a `legacyDir` parameter; today `migrateFromLegacyFiles` reads `QDir::homePath()` and is untestable) |
| `test_migrateLegacyReportPaths` | `contabilidad_path=/r/Contabilidad` -> root `/r`; only `listados_prendas_path=/r/Listados/Prendas` -> `/r`; gastos only; unmatched suffix -> parent of contabilidad; legacy keys removed from the saved file; idempotent when `root` set; obsolete `print.template_path/script_path`, `app.icon_path` removed |
| `test_corruptJsonFallsBackToDefaults` | invalid JSON: `loadFrom` false, defaults present, file untouched until save (and, after fix #4, `.corrupt` copy created) |
| `test_saveLoadRoundTrip` | set every setter, `save()`, `loadFrom` same file, all getters equal; `.tmp` file absent afterwards |
| `test_saveFailureReturnsFalse` | unwritable path (non-existing directory) returns false and does not throw |
| `test_aeatCertificatePasswordEncryptedAtRest` | plaintext `aeat_certificate_password` in file -> encrypted on load; getter plaintext; same for setter (Windows: not in raw JSON) |
| `test_decryptFailureClearsField` | `dpapi:v1:AAAA` (garbage) -> getter empty, `dpapiDecrypt("dpapi:v1:")` empty |
| `test_serviceKeyUnicodeAndWhitespace` | round trip of a key with non-ASCII/space |
| `test_aeatInstallationNumberStableAndPersisted` | first call creates `LAIDEAL-` + 8 hex upper, second call same, file on disk contains it without any other save (fails today: finding 1); reload returns same |
| `test_chainMarkerPersisted` | `setAeatChainMarker` then reload from file returns it (fails today) |
| `test_environmentAndConnectionFlags` | `setVerifactuProduction(true)` stored `"PRODUCTION"`; unknown string e.g. `"production"` -> false (production only on exact value); `connection` other than `aeat` -> gateway; `aeatDirectProduction` default false |
| `test_wrongTypesFallBackToDefaults` | `"updater":{"check_on_startup":"yes"}` -> default true; `paper_width_mm` as string -> 80; `print.enable` bool instead of string -> false |
| `test_paperWidthStoredAsNumber` | `setPaperWidthMm(58)` -> JSON number 58 |
| `test_printFlagsYesNo` | `enable`/`use_status_api` stored `"Yes"/"No"`; anything else false |
| `test_languageDefaultAndInvalid` | empty -> "es" (getter), `setLanguage("fr")` returned raw; `applyQtTranslations` with unknown value acts like es (needs QCoreApplication; or a pure helper) |
| `test_setNestedRemoveNested` | via migration: non-object parent replaced; removing a missing key is a no-op |
| `test_loadFromResetsState` | load file A with key, load file B without: key gone except defaults |
| `test_pendingRecoveryFloorDefaultAndInvalid` | blank -> default `2026-09-01`; stored `garbage` is returned verbatim (consumers must tolerate; cross-check with `refusedInvoiceCount` test) |

### Unit (test_settingsdialog)

| Proposed test | Assertions |
|---------------|-----------|
| `test_acceptSavesEveryGeneralField` | edit db path, printer, paper width 58, status API, updates, reports root, business fields, language en; accept; getters/JSON equal; all values trimmed |
| `test_leavingDirectKeepsProductionOffOrAsks` (finding 2) | direct+production true, switch to gateway, confirm; after accept `aeatDirectProduction()==false`; switch back to direct: no flag silently true |
| `test_gatewayProductionNeedsConfirmation` (finding 3) | after fix: checking PRODUCCION and answering No leaves TESTING |
| `test_saveFailureShowsWarning` | read-only settings path: `QMessageBox` "No se pudo guardar", dialog still closes; `qWarning` expected |
| `test_certificateThumbprintNotInStoreIsKept` (finding 9) | stored thumbprint absent from choices, accept: thumbprint unchanged |
| `test_pendingRecoveryFloorRoundTrip` | stored `2026-09-01` shows 01-09-2026; invalid string shows fallback; accept stores ISO; checkbox round trips |
| `test_testConnectionSignalCarriesTrimmedValues` | NIF/name/key trimmed, production flag |
| `test_serviceKeyUnreadableWarns` | after fix #8 |
| `test_languageChangeAppliesLive` | accept with `en` removes the translator; accept with `es` installs; second accept idempotent |
| `test_dbPathValidation` | after fix #13 |
| `test_printerComboKeepsUnknownQueue` | stored name not installed stays as editable text after accept |

### E2E (test_e2e_verifactu / test_e2e_app)

- Both load a throwaway file, never exercise save-failure or `Configuración` accept re-initialisation. Propose `test_e2e_settingsChangeReinitialisesBackend` (M): accept the dialog switching gateway<->direct, assert `VerifactuIntegration::initialize` yields the right backend, the direct one reads thumbprint/pfx password plaintext from the DPAPI-encrypted store, and the installation number in the produced record's SistemaInformatico is identical across a settings reload (guards finding 1).
- `test_e2e_directRecordKeepsInstallationNumberAcrossRestart` (S): create records, drop the `AppSettings` singleton state via `loadFrom` of the same file, assert same `NumeroInstalacion`.

### UI smoke (tests/ui)

Block E only tests language, "no IVA field", backup. Proposed steps (M each, via `settings_dialog(run)`):
- `E5 Configuración round trip`: change business name, reports root, paper width, tick/untick update check, OK; read the settings file and verify the keys by their JSON names (also catches README drift), reopen and see the values.
- `E6 Production confirmations`: Verifactu tab, direct group, tick `Enviar a la AEAT real`, press OK, expect the question, answer No; assert file still `aeat_direct_production=false`. This also guards that the UI-test build never ends in production.
- `E7 Service key masked and not in plaintext on disk`: type a key, OK, assert the settings file does not contain it (`dpapi:v1:` prefix), reopen, field masked, `Mostrar` shows it.
- `E8 Settings file corrupt`: start with a truncated JSON, expect the first-run dialog "Configuración inicial", set DB path, OK, relaunch ok (after fix #4 also the `.corrupt` copy).
- `E9 First run without DB path`: settings with `database.path` empty/missing: the initial dialog appears (main.cpp:78), Cancel -> error box and exit code 1.
- `E10 Pending recovery floor`: set a future floor in the dialog, restart, the recovery dialog from block A2 does not list ticket 3; restore.
- `E11 Printer settings visible`: printer combo lists installed printers, status API test button opens the diagnostics box (printer.py fake queue).

### Self-check (`--self-check`, smoke_test part 2)

The shop-PC check exercises none of the settings. Proposed new automatic check `M0 Ajustes` (M) in `SelfCheck`, run on the copy but reading the live file's decryption, per user:
- settings file parses; `database.path` exists; `reports.root` exists/writable;
- service key / certificate password decrypt non-empty when `dpapi:v1:` is present (detects "file copied from another Windows user", the main DPAPI risk after a reinstall or Windows reset);
- if `verifactu.connection=aeat`: the thumbprint exists in the Windows store, or the `.pfx` file exists and opens with the stored password; expiry weeks away (the investigation doc item "warn before it expires");
- floor date is a valid ISO date, not in the future, `aeat_installation` present;
- `app.language` is `es`/`en`; printer name exists in `QPrinterInfo` when printing is on (M3 already needs it).
- also report whether the live settings file or `.tmp` leftover exist (finding 4/5).
Test: `test_checkSettings_*` in test_selfcheck.cpp with a settings JSON containing a garbage `dpapi:v1:` value (Fail), a good one (Pass), missing db (Fail).

## Questions or cross-module notes

- Verifactu reviewer / supervisor: is NumeroInstalacion intended to be stable across restarts? If yes finding 1 is a compliance defect (it changes per start whenever no save happens in between). Also, are `readChainMarker`/`writeChainMarker` ever invoked off the GUI thread (finding 14)?
- Backup reviewer: `BackupManager` is the only non-dialog `save()` caller (backup_manager.cpp:165-167) and so currently persists the installation number and chain marker as a side effect; with `backup.enabled=false` nothing does.
- Main/selfcheck reviewer: `main.cpp` calls `load()` (possible write) before `--self-check`, contradicting "only read" in smoke_test.md:28 (finding 16).
- Printing reviewer: the dialog calls `StatusApiPrinter::runDiagnosticsBounded` on the GUI thread with a 12 s wait cursor (settingsdialog.cpp:125); acceptable as bounded, but it freezes the dialog.
- Docs owners: README Configuration table is the highest-value fix (users hand-edit it); it needs the real key names and the "Yes"/"No" string booleans.
