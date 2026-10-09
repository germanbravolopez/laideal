# Testing — La Ideal

How the application is tested, how to run the tests, and where each kind of test is documented. Three layers, from fastest to most manual:

| Layer | What it proves | Where | Runs |
|-------|----------------|-------|------|
| **Unit / integration suites** | Pure logic and the `sql_lite` DB functions, each module in isolation | `tests/test_*.cpp` — see [unit_suites.md](unit_suites.md) | `ctest`, every CI run |
| **End-to-end test bench** | Real objects working together (PayDialog, MainWindow and the app dialogs + Verifactu client + HTTP + DB) against a fake Verifactu server | `tests/test_e2e_*.cpp` + `tests/support/` — see [e2e.md](e2e.md) | `ctest`, every CI run |
| **Manual smoke test** | What no automated test can see: the release build on a real schema, the real printer, PDF / screen rendering, menu wiring, the Listado lock, backup, language, installer. Never contacts AEAT | [smoke_test.md](smoke_test.md) | Before each release (`/release` step 1) |

---

## Running the tests

`tests/` holds Qt Test suites, one executable each, registered with CTest (`enable_testing()` + `add_subdirectory(tests)` in the root `CMakeLists.txt`). They build with the normal build:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure        # all suites
ctest --test-dir build -R e2e --output-on-failure  # one suite, by name regex
```

A suite can also be run directly for its full per-function report, e.g. `build\tests\test_sql_lite.exe`. GUI suites need `QT_QPA_PLATFORM=offscreen` when run by hand; CTest sets it for them.

**In CI** the single `ci.yml` workflow runs the same `ctest` on every push and PR to `develop`, `master` and `feature/**`, and on `X.Y` tags. Its `release` job `needs: build`, so a failing test aborts a publish. Each suite writes `build/test-results-<suite>.xml` (JUnit); `.github/scripts/Render-TestSummary.ps1` turns those into a foldable per-suite, per-method table in the run's step summary, and appends the `test_ticket_preview` ASCII receipt (GitHub strips images from summaries; the PNG renders are uploaded as the `ticket-previews` artifact).

---

## Conventions every suite follows

- **Link the production static lib and drive it through real inputs**; assert observable results. No mocks inside the app — only fakes at the boundaries (network replies, files, the DB file).
- **Test the pure core.** When logic is trapped in a dialog or slot, extract it into a library function (free function or static) and keep a thin wrapper — e.g. `sql_lite::garmentExcludedFromTotals`, `Contabilidad::figuresFromDetails`, `parseVerifactuResponse`.
- **Throwaway state only.** DB tests create SQLite files in a `QTemporaryDir`; settings tests point `AppSettings` at a throwaway file with `loadFrom()`. No test may read or write the real database, `~/.laideal_settings.json` or the real AEAT.
- **Application type.** `QTEST_GUILESS_MAIN` (no display) by default; `QTEST_MAIN` under `QT_QPA_PLATFORM=offscreen` (CTest `ENVIRONMENT` property) when a `QPixmap`, widget or header is involved.
- **Prove each new test can fail**: temporarily break the production line it guards, watch it fail, restore. A test that passes against the bug guards nothing.
- **Hangs fail fast.** Every suite has a 120 s CTest `TIMEOUT` (set for all tests at the end of `tests/CMakeLists.txt`; CI also passes `--timeout 120`) and the CI build job has `timeout-minutes: 45`.
- **Executable names** must not contain `update`, `setup` or `install`: Windows' UAC installer heuristic flags them and CTest fails with `BAD_COMMAND` (hence `test_versioncompare` for the updater).

### Patterns, by kind of logic

| Logic under test | Pattern | Example |
|------------------|---------|---------|
| Pure helper / static | Link the owning lib, inputs → outputs | `test_contabilidad`, `test_facturas` |
| `sql_lite` DB function | Throwaway SQLite DB, schema in `initTestCase`, tables cleared in `init()` | `test_sql_lite` |
| AEAT / vendor reply parsing | Captured reply fixture (redacted) fed to the parser | `test_verifactu_response` |
| Settings | `AppSettings::loadFrom(tempFile)` | `test_appsettings`, `test_settingsdialog` |
| Widget / header behaviour | `QTEST_MAIN` offscreen, drive the real widget | `test_settingsdialog`, `test_textcolordelegate` |
| Flow across modules + network | Fake server + seeded DB + real objects | `test_e2e_verifactu` ([e2e.md](e2e.md)) |
| A window driven at screen level | Link `laideal_app`, find widgets by object name, invoke the slot a button is wired to | `test_e2e_app` ([e2e.md](e2e.md)) |

---

## Adding a suite

1. Create `tests/test_<name>.cpp` (copy the shape of the closest existing suite).
2. Add an `add_executable` / `target_link_libraries` / `add_test` block to `tests/CMakeLists.txt`, with the same `-o ${CMAKE_BINARY_DIR}/test-results-<name>.xml,junitxml -o -,txt` arguments as the others, plus `set_tests_properties(... ENVIRONMENT "QT_QPA_PLATFORM=offscreen")` if it uses `QTEST_MAIN`.
3. Reconfigure (`cmake -B build`) so CTest picks it up.
4. Document it in [unit_suites.md](unit_suites.md) (or [e2e.md](e2e.md)) and bump the suite count in `docs/INDEX.md`.

The `test-engineer` subagent (`.claude/agents/test-engineer.md`) knows these patterns and can write the coverage for a change.

---

## CI toolchain

Both CI jobs build with **MinGW GCC 11.2.0** (posix-seh, MSVCRT; `choco install mingw --version=11.2.0.07112021`), passed to CMake explicitly, because Qt 6.4.3 `win64_mingw` is built with MinGW 11.2. The runner's preinstalled `C:\mingw64` is a newer UCRT toolchain whose test binaries fail to load against Qt's bundled `libstdc++` (`0xC0000139`, a modal error dialog that hung `ctest`). A `Check runtime of built executables` step fails the build if any exe imports `api-ms-win-crt-*`. Locally use `C:\Qt\Tools\mingw1120_64` (root README §Build).
