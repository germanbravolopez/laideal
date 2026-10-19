# Review: updater (src/updater/) - before release 12.0

## Summary
Small, readable module (491 lines) that works on the happy path; 11.10 vs 11.9 is handled correctly and tested.
Main weakness is trust: the installer is downloaded and executed with no integrity or authenticity check (TLS + GitHub only), and no Authenticode signature.
Robustness gaps: no download timeout/cancel, whole file buffered in RAM, shared `m_silentOnNoUpdate`, re-entrant dialogs, process-wide TEMP redirect never restored.
Tests: only two pure-helper tests; asset selection, JSON handling, download, dialog and menu flow are untested and no e2e/UI/self-check layer touches it.
Docs drift: architecture.md:135 and updater.h:20,52-53 still say `%TEMP%`; code uses `AppLocalDataLocation/updates`.

## Findings
| Sev | Cat | file:line | Finding | Proposal | Effort |
|---|---|---|---|---|---|
| High ✅ FIXED 19-10-2026 | security | updater.cpp:174-189, updaterdialog.cpp:112 | Downloaded .exe is launched with no integrity/authenticity check (no SHA-256, no signature, no PE/size check). Tampering with the release (account compromise) or the TLS path gives code execution as the shop user, who owns the DB and AEAT certificate. A truncated body is only caught if Qt raises an error. | ci.yml publishes `laideal_setup_X.Y.exe.sha256` (or use the per-asset `digest` field in the release JSON); Updater hashes with QCryptographicHash::Sha256 and refuses to launch on mismatch; also compare byte count with asset `size`. Longer term Authenticode-sign the installer and verify with WinVerifyTrust. | M (hash) / L (signing) |
| Medium ✅ FIXED 19-10-2026 | security | updater.cpp:111, 154-155 | `browser_download_url` from the JSON is trusted blindly (any scheme/host) and `NoLessSafeRedirectPolicy` follows redirects to any https host; no allow-list. | Require https and host github.com / *.githubusercontent.com before emitting `updateAvailable`; check redirect target the same way. | S |
| Medium ✅ FIXED 20-10-2026 | bug | updater.cpp:149-165 | Download request has no transfer timeout (only the check has one, :58) and no abort; a stalled connection leaves "Descargando" with both buttons disabled (updaterdialog.cpp:84-85). | `setTransferTimeout(30000)` on the download; keep the reply pointer, add Cancel and abort on reject. | S |
| Medium ✅ FIXED 20-10-2026 | bug | updaterdialog.cpp:84-85 | Esc / window close during download still calls `reject()` (only buttons are disabled): dialog closes, reply keeps running; a second manual check opens another dialog and the stale-file cleanup (updater.cpp:141) races the first download. | Override `reject()` to ignore while downloading (or abort the reply); in-flight guard in Updater. | S |
| Medium ✅ FIXED 20-10-2026 | bug | updater.cpp:49, mainwindow.cpp:1592,1605,1612 | `m_silentOnNoUpdate` is one member shared by startup and manual checks. A manual click during the 1.5 s startup check flips it, so a startup result can be shown or a manual result hidden; two overlapping checks emit two `updateAvailable` -> two dialogs. | Carry `silent` per request (capture in lambda / pass in signal) and ignore a second `checkForUpdates` while one is in flight. | S |
| Medium ✅ FIXED 20-10-2026 | bug | updaterdialog.cpp:110-112, 113-120 | `qputenv("TMP"/"TEMP")` is process-wide and never restored; if `startDetached` fails the app keeps running with TEMP redirected (QTemporaryDir, preview/PDF temp files, `--self-check` copies). | Use a `QProcess` instance with `setProcessEnvironment`, or restore the old values on failure. | S |
| Medium ✅ FIXED 19-10-2026 | bug | updater.cpp:179-186 | Whole installer is `readAll()`-ed after finish and written non-atomically to the final name; a crash mid-write leaves a truncated `laideal_setup_X.Y.exe` that the failure hint (updaterdialog.cpp:118) tells the user to run manually. | Stream to `.part`, verify hash/size, then `QFile::rename`. | M |
| Medium ✅ FIXED 20-10-2026 | bug | updater.cpp:36-40,53-55 | Version regex is an unanchored prefix match: `12.0.1`, `12.0-beta` compare as `12.0`; on a non-match it falls back to `QString::compare(a,b)` so e.g. tag "nightly" > "12.0" would offer an update. Tags are strictly X.Y today (ci.yml:123), so latent. | Anchor with `$`; treat non-match as checkFailed, never as newer. | S |
| Low | bug | updater.cpp:108-118 | Asset = first `laideal_setup_*.exe`; not checked against `laideal_setup_<tag>.exe`. `QUrl::isValid()` is true for an empty URL, so an asset without `browser_download_url` passes the check at :116 and the dialog would try to download "". | Match exact name; test `isEmpty()` too. | S |
| Low | bug | updater.cpp:141-142, 144-147 | Every `*.exe` in the updates folder is deleted before each download (errors ignored); local filename comes from the server-supplied URL (`installerUrl.fileName()`, percent-decoded, could carry odd names such as `x.exe:stream`). | Delete only `laideal_setup_*.exe`; build the name from the validated tag. | S |
| Low | design | updaterdialog.cpp:107-124 | After `startDetached` returns true the app calls `accept()` + `quit()` without knowing the installer really ran (AV kill); only a `qDebug` records it. | Write an entry to the app log/Logger before quitting so the self-check/support report shows an attempted update. | S |
| Low | design | updaterdialog.cpp:60-62; ci.yml:239 | Release body is bilingual Markdown ("## Español ... ## English ...") shown with `setPlainText`: raw `##` markers and both languages whatever the UI language. | `setMarkdown` and show only the section of the UI language. | S |
| Low | design | updater.cpp:62-66, mainwindow.cpp:1610-1616 | Manual failure text is the raw `errorString()` (English Qt text); GitHub anonymous rate limit (60/h per IP, shared in a shop network, HTTP 403) not distinguished. | Map `NetworkError`/HTTP status to Spanish messages. | S |
| Low | design | updater.cpp:19-21 | URL hardcoded and parsing inside the network slot: no seam to test anything beyond `compareVersions`. | Extract pure `parseRelease(QByteArray) -> {tag, notes, installerUrl, error}`; add `setApiUrl()`; injectable launcher in the dialog. | S |
| Low | design | updater.cpp:97-101 | `/releases/latest` ignores drafts/pre-releases (no beta channel) and a downgrade reports "ya tiene la más reciente". By design but undocumented. | Document in architecture.md / CONTRIBUTING.md. | S |
| Low | bug | ci.yml:238 | Notes regex lookahead `^r?\d+\.\d+` has a stray `r?` (probably `\r?`); harmless now. | Fix to `^\d+\.\d+\r?\n`. | S |
| Low | design | releases/laideal.iss:84-85, 87-88 | New Start-menu shortcut "Comprobar tras actualizar" is correct (no admin, runs `--self-check`), but nothing prompts the shop to run it after an in-app update (the only post-install action relaunches the app). | Optional second `[Run]` entry (unchecked checkbox) or a first-start-after-version-change notice. | M |
| Medium ✅ FIXED 19-10-2026 | security | ci.yml:195-210 (cross-module) | Required-DLL guard does not list Qt's TLS backend plugin (`tls\qschannelbackend.dll` / OpenSSL). If absent the updater fails with a TLS error that is silent at startup and unnoticed. UI smoke does not exercise the network. | Verify with the packaging reviewer; add to the required list and to the self-check. | S |

No dead code found: every public member is used by MainWindow/UpdaterDialog. No coding-guidelines violations seen beyond long Spanish literals inside `Updater` (non-`tr()` user strings in updater.cpp:62,70,87,117,131,166,171,182; UiKit/dialog strings use `tr()`, Updater's do not - inconsistent with the language support, see Questions).

Checked and fine: 11.10 > 11.9 numeric compare (updater.cpp:46-52, test_updater.cpp:25-26); offline/GitHub down -> `checkFailed`, silent at startup (mainwindow.cpp:1612); 10 s check timeout (:58); error text HTML-escaped; no `ignoreSslErrors` anywhere (Qt default certificate verification); per-user installer avoids UAC; ci.yml fails the release if tag != CMake version (:123) or notes are missing/empty.

## Documentation drift
| Doc (file:line) | Says | Code (file:line) | Fix |
|---|---|---|---|
| docs/architecture.md:135 | installer saved to `%TEMP%` | updater.cpp:128-147: `AppLocalDataLocation/updates`, stale `*.exe` deleted first | Update text |
| src/updater/updater.h:20,52-53 | "saves to %TEMP%\laideal_setup_X.Y.exe", "reused across runs" | same | Fix comments |
| docs/architecture.md:135 | `compareVersions` "major.minor numeric (tolerates a leading v)" | prefix match + lexicographic fallback (updater.cpp:36-40) | Say so or change the code |
| docs/architecture.md:131-142 | No mention that the installer is unverified, that the release body is bilingual Markdown shown as plain text, or that drafts/pre-releases are invisible | updater.cpp:174-189; ci.yml:239; updaterdialog.cpp:60 | Add "Trust model" paragraph |
| docs/architecture.md:136 | `Updater`/dialog desc. | New launch uses TMP/TEMP of the whole process (updaterdialog.cpp:110) | Mention it is process-wide |
| CONTRIBUTING.md:84-86 | assets and bare tag consumed by the updater | ci.yml consistent; contract not stated (asset prefix `laideal_setup_`, `.exe`, release must not be draft/prerelease) | Add one line |
| README.md:24 | in-app updater bullet | no pointer to "Comprobar tras actualizar" after updating | Link docs/testing/smoke_test.md part 2 |
| docs/testing/e2e.md:15, ui_smoke.md:57, tests/ui/seed.py:58 | update check is switched off | no scenario enables it | See gaps |
| docs/testing/unit_suites.md:30,101 | `test_versioncompare` covers compareVersions + currentVersion | tests/test_updater.cpp:14-40 correct | none |

## Test gaps
Existing: `test_versioncompare` (tests/test_updater.cpp): `test_compareVersions`, `test_currentVersion`. Nothing else.

### Unit (most need the seam `parseRelease()` / `setApiUrl()`)
| Proposed test | Assertions | Effort |
|---|---|---|
| `test_compareVersions_malformedNeverNewer` | "nightly" vs "12.0" -> no update; "12.0.1"/"12.0-beta" behaviour pinned | S |
| `test_compareVersions_boundary11to12` | "12.0" > "11.10" > "11.9"; "12.00" == "12.0" | S |
| `test_parseRelease_picksInstallerAsset` | zip + `laideal_setup_12.0.exe` -> exe url; zip only -> error naming the tag; two exes -> exact-name one; empty `browser_download_url` -> error | S |
| `test_parseRelease_missingTagOrBadJson` | empty tag -> "tag_name" error; garbage / array JSON -> "Respuesta JSON inválida" | S |
| `test_parseRelease_sameOrOlderTag_noUpdate` | tag == current and tag < current | S |
| `test_parseRelease_rejectsForeignHostOrHttp` (after hardening) | non-github host / http -> error | S |
| `test_checkForUpdates_localServer_updateAvailable` | QTcpServer canned JSON; QSignalSpy `updateAvailable` args (tag, notes, url) | M |
| `test_checkForUpdates_serverDown_http404_timeout` | exactly one `checkFailed`, no `updateAvailable` | M |
| `test_downloadInstaller_writesFileAndCleansStale` | stale `x.exe` removed; content equal; `downloadFinished(path)`; progress emitted | M |
| `test_downloadInstaller_hashMismatch_noLaunch` (after hashing) | `downloadFailed`, no file left | M |
| `test_downloadInstaller_truncated_noFinalFile` | server closes early -> `downloadFailed`, no final file | M |
| `test_silentFlagPerRequest` (after fix) | manual check during silent one is not hidden/shown wrongly; second concurrent check ignored | S |

### E2E (test_e2e_app, offscreen)
- `test_updater_dialog_showsVersionsAndNotes`: header has current + new version, notes shown, `btnLater`/`btnUpdate` present.
- `test_updater_dialog_downloadFailed_reenablesButtons`: error panel visible and escaped, Actualizar enabled.
- `test_updater_dialog_rejectDuringDownload_ignored` (after fix).
- `test_mainwindow_manualCheck_noUpdate_and_failure`: `m_result` shows the Spanish message; the silent path writes nothing.
- Injectable launcher so no real `QProcess::startDetached`; assert it gets the downloaded path and TEMP is restored after failure.

### UI smoke (tests/ui)
- New E-block step: Ayuda -> Buscar actualizaciones against a fake release endpoint (hidden `--update-url` option or env var) on the VM: "Actualización disponible" dialog, notes, "Más tarde" closes; second run with no newer version shows the green "versión más reciente" message. One run with `check_on_startup=True` and an unreachable endpoint proves the app starts silently offline.
- One manual M-step (smoke_test.md): Buscar actualizaciones on the installed build reaches GitHub (catches a missing TLS backend plugin).

### Self-check (`--self-check`)
- New item "Actualizaciones": `QSslSocket::supportsSsl()` and active backend, GET `/releases/latest` once (rate-limit aware), tag parses, installer asset present with exact name and hash, updates folder writable and executable, %TEMP% execute-denied detection. Report only, never download or run.
- Report header: installed version vs latest release ("al día / actualización pendiente").

## Proposals summary
1. Installer hash (then signature) verification published by ci.yml - M / L.
2. Scheme/host allow-list for asset URL and redirects - S.
3. Download timeout, cancel, reject handling, `.part` + atomic rename, size check - M.
4. Per-request silent flag and in-flight guard - S.
5. Child-only TMP/TEMP, restore on failure - S.
6. Test seam (`parseRelease`, `setApiUrl`, injectable launcher) and the tests above - M.
7. Language-aware Markdown release notes, Spanish network error messages, `tr()` for Updater strings - S.
8. Doc fixes and contract line in CONTRIBUTING.md - S.
9. Self-check "Actualizaciones" item and UI smoke fake-release scenario - M.
10. TLS plugin in ci.yml required list - S.

## Questions / cross-module notes
- Packaging/CI reviewer: does windeployqt ship Qt's TLS plugin? ci.yml:195-210 does not guard it.
- Self-check reviewer (src/app/selfcheck*): can it host an "Actualizaciones" item?
- i18n reviewer: Updater emits raw Spanish literals (updater.cpp:62,70,87,117,131,166,171,182) without `tr()`, so English UI shows Spanish errors; confirm policy.
- Supervisor: item 1 (unverified installer) is the only release-relevant security gap; the rest can be tracker items. The installer runs as the shop user (PrivilegesRequired=lowest), so a malicious one still reaches the DB and AEAT certificate in that profile.
- MainWindow reviewer: manual-check result appears in the `m_result` status panel, not a dialog; confirm it is visible from any screen.
