# Testing - La Ideal

How to run the tests, the conventions for writing one, and what CI runs. Each layer has one owner document.

| Layer | What it proves | Document | Runs |
|-------|----------------|----------|------|
| Unit / integration suites | Pure logic and the `sql_lite` functions, module by module | [unit_suites.md](unit_suites.md) | `ctest`, every CI build |
| End-to-end bench | Real windows, Verifactu client, HTTP and DB together, against a fake gateway and a fake AEAT | [e2e.md](e2e.md) | `ctest`, every CI build |
| UI smoke test, AEAT pre-production script | The installed UI-test build driven like a person over UI Automation | [ui_smoke.md](ui_smoke.md) | `ui-smoke` CI job; the script locally |
| Smoke checklist and self-check | What only a person (before a release) or the shop's own data and printer (after installing) can show | [smoke_test.md](smoke_test.md) | Before and after each release |

---

## Running the tests

Everything goes through `make.ps1` (`.\make <target>` in PowerShell, `make <target>` in cmd). It puts the Qt toolchain on `PATH` itself and configures a build folder when it has none. `.\make help` lists the targets.

| Target | Does |
|--------|------|
| `build` | Release build into `build\` |
| `debug` | Debug build of the app into `build-debug\` (the tree VSCode F5 uses) |
| `test` | `build`, then every CTest suite (`-Filter <regex>` selects suites, `-Jobs <n>` runs them in parallel, default 4) |
| `xsd` | Builds, then runs `aeat_record`, `aeat_response` and `aeat_xsd` (the last needs Python `lxml`, otherwise it is skipped) |
| `uitest` | UI-test build into `build-uitest\` (never for the shop) |
| `venv` | The Python environment of the UI scripts (`.venv`) |
| `printer` | The capturing printer queue of smoke block C (asks for an administrator) |
| `ui-smoke` | UI smoke test; options `-Blocks`, `-Printer`, `-Exe`, `-Tools`, `-Installer`, `-Out` |
| `aeat-dry` | The pre-production script against the fake AEAT (no certificate) |
| `aeat-real` | The same against AEAT's test environment: `-Settings <file>` required, `-Force` skips the confirmation |
| `regression` | `build`, `test`, `uitest`, `ui-smoke`, `aeat-dry`; stops at the first failure |
| `clean`, `clean-all` | Delete test outputs; also the build folders (`-Force` skips the question) |

The UI targets need an unlocked, interactive desktop: see [ui_smoke.md](ui_smoke.md).

Without `make`:

```powershell
ctest --test-dir build --output-on-failure             # all suites
ctest --test-dir build -R e2e --output-on-failure      # suites matching a regex
build\tests\test_sql_lite.exe                          # one suite, full per-function report
```

GUI suites need `QT_QPA_PLATFORM=offscreen` when run by hand; CTest sets it for them.

## What CI runs

The single workflow `.github/workflows/ci.yml` calls the same `make.ps1` targets.

| Job | Runs | When |
|-----|------|------|
| `build` | `make build`, the executable runtime check, `make test` (with `--timeout 120`), the ticket-preview and test-result uploads | Push to `develop` / `master`, every PR to them, on demand |
| `ui-smoke` | `make uitest`, install, `smoke.py` blocks A-E, the pre-production dry run ([ui_smoke.md](ui_smoke.md#where-it-runs)) | Every PR, on demand |
| `release` | Packages the installer; publishes only on the push to `master` | Needs `build`; a failing test aborts a publish |

- Each suite writes `build/test-results-<suite>.xml` (JUnit). `.github/scripts/Render-TestSummary.ps1` turns them into a per-suite table in the run summary and appends the `test_ticket_preview` ASCII receipt. The PNG renders are uploaded as the `ticket-previews` artifact.
- The build job has `timeout-minutes: 45`.
- **Toolchain**: MinGW GCC 11.2.0 (posix-seh, MSVCRT), passed to CMake explicitly, because Qt 6.4.3 `win64_mingw` is built with MinGW 11.2. The runner's newer UCRT `C:\mingw64` produces binaries that fail to load against Qt's `libstdc++` (`0xC0000139`). A `Check runtime of built executables` step fails the build if any exe imports `api-ms-win-crt-*`. Locally use `C:\Qt\Tools\mingw1120_64` (root README, Build).

---

## Conventions

- **Link the production static library and drive it through real inputs**; assert observable results. No mocks inside the app, only fakes at the boundaries (network replies, files, the DB file, the printer transport).
- **Test the pure core.** Logic trapped in a dialog or slot is extracted into a free or static function with a thin wrapper (`sql_lite::garmentExcludedFromTotals`, `Contabilidad::figuresFromDetails`, `parseVerifactuResponse`).
- **Throwaway state only.** DB tests use SQLite files in a `QTemporaryDir`, built by `tests/support/testschema.h` (the original `ingresos` columns, then the app's own `migrateDatabase()`). Settings tests point `AppSettings` at a throwaway file with `loadFrom()`. No test reads or writes the real database, `~/.laideal_settings.json` or the real AEAT.
- **No test reaches a real host.** CTest sets `LAIDEAL_TEST=1` for every test; with it the AEAT and gateway transports refuse any host but this machine (`src/verifactu/testhostguard.h`).
- **Application type.** `QTEST_GUILESS_MAIN` by default; `QTEST_MAIN` with `QT_QPA_PLATFORM=offscreen` (a CTest `ENVIRONMENT` property) when a `QPixmap`, widget or header is involved.
- **Prove a new test can fail**: break the production line it guards, watch it fail, restore.
- **Hangs fail fast.** Every test has a 120 s CTest `TIMEOUT` (end of `tests/CMakeLists.txt`) except `sql_lite` and `e2e_app` (600 s each) and `aeat_direct_backend` (900 s: 95 fake-AEAT scenarios with many fixed AEAT delays, about 290 s on a quiet machine and up to 480 s on a saturated one).
- **Executable names** must not contain `update`, `setup` or `install`: Windows' UAC heuristic flags them and CTest fails with `BAD_COMMAND` (hence `test_versioncompare` for the updater).

### Seams

| Logic under test | Pattern | Example |
|------------------|---------|---------|
| Pure helper / static | Link the owning library, inputs to outputs | `test_contabilidad`, `test_facturas` |
| `sql_lite` function | Throwaway DB, schema in `initTestCase`, tables cleared in `init()` | `test_sql_lite` |
| Vendor / AEAT reply parsing | A captured, redacted reply fixture fed to the parser | `test_verifactu_response` |
| Settings | `AppSettings::loadFrom(tempFile)` | `test_appsettings` |
| Widget behaviour | `QTEST_MAIN` offscreen, drive the real widget by object name | `test_settingsdialog`, `test_textcolordelegate` |
| Printing | `ThermalPrinter::setTransport` replaces the printer | `test_imprimir` |
| Log files | `AppLogger::installAt(path)` | `test_applogger` |
| Updater | Loopback fake GitHub: `setApiUrl`, `setAllowLoopbackForTests` | `test_versioncompare`, `test_versiondialog` |
| Gateway / direct client over HTTP | `VerifactuConfig::setEndpointOverride`, `AeatDirectBackend::setEndpointOverride` to a fake server on 127.0.0.1 | `test_e2e_verifactu`, `test_aeat_direct_backend` ([e2e.md](e2e.md)) |
| A window across modules | Link `laideal_app`, find widgets by object name, invoke the slot a button is wired to | `test_e2e_app` ([e2e.md](e2e.md)) |

## Adding a suite

1. Create `tests/test_<name>.cpp`, copying the closest existing suite.
2. Add the `add_executable` / `target_link_libraries` / `add_test` block to `tests/CMakeLists.txt` with the same `-o ${CMAKE_BINARY_DIR}/test-results-<name>.xml,junitxml -o -,txt` arguments, plus the offscreen `ENVIRONMENT` property for `QTEST_MAIN`.
3. Reconfigure (`cmake -B build`).
4. Document it in [unit_suites.md](unit_suites.md) (or [e2e.md](e2e.md)) and update the suite count in `docs/INDEX.md`.

The `test-engineer` subagent (`.claude/agents/test-engineer.md`) knows these patterns.
