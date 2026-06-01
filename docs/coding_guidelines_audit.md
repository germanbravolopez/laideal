# Coding Guidelines Audit — La Ideal

How to audit the codebase against [`/coding-guidelines`](../.claude/commands/coding-guidelines.md), plus the latest findings. Re-run periodically (before releases, or after large feature work) and update the **Current findings** section in place.

**Last run**: 2026-05-26.

---

## Purpose

The coding guidelines are easy to drift from without noticing — especially the Spanish-vs-English language rule (the DB schema is Spanish and bleeds into local-variable names) and the rule against SQL string concatenation (necessary for generic helpers, easy to overreach). This document gives a repeatable methodology so a fresh agent can reproduce the audit in one session.

---

## How to run the audit

For each rule, run the listed Grep pattern over `src/` and interpret the results against the **Severity grading** below. The greps are written for ripgrep syntax (the project's `Grep` tool). All paths assume the working tree root.

### 1. Language rules

| What to find | Grep pattern | Interpretation |
|---|---|---|
| Non-ASCII punctuation (em-dash, en-dash, curly quotes, ellipsis) | `[—–“”‘’…]` over `src/` | Any hit is a real discrepancy (Windows code-page hazard). Spanish letters `áéíóúñ¿¡` are allowed everywhere. |
| Spanish text in `qDebug` / `qWarning` / `qCritical` | `qDebug\(\)\s*<<\s*"[^"]*[áéíóúñ¿¡]` | Diagnostic logs must be English. UI-facing messages stay Spanish. |
| Spanish words used as local variables (DB-column mirroring) | `\b(QString\|bool\|int\|float\|double)\s+(prenda\|recibo\|cliente\|importe\|cantidad\|pagado\|estado\|servicio\|observaciones\|nombre\|fecha\|tipo\|trim\|mes\|año\|direccion\|telefono)\b` | New code should use English (`client`, `date`, `state`, …). Code that pre-dates the guidelines is grandfathered per the legacy note. |
| Spanish leading words in single-line comments | `^\s*//\s*(Comprueba\|Busca\|Crea\|Revisa\|Comprobar\|Borrar\|Eliminar\|Insertar\|Actualizar\|Cuando\|Aquí\|Si\s\|Y\s\|Para\s)` | Any hit is a real discrepancy (purged in May 2026). |

### 2. Naming conventions

| What to find | Grep pattern | Interpretation |
|---|---|---|
| `snake_case` method declarations | `\b(void\|bool\|int\|QString)\s+[a-z][a-z0-9]+_[a-z0-9_]+\s*\(` over `src/*.h` | Filter out Qt auto-connect slots (`on_<obj>_<signal>` — required to be snake_case). Remaining hits in `src/app/`, `src/add_garment/`, `src/listado/` are legacy per the note. New methods must be camelCase. |
| Header guards | `^#ifndef\|^#define\|^#endif` over `src/**/*.h` | Each header should have matching `#ifndef CLASSNAME_H` / `#define CLASSNAME_H` / `#endif // CLASSNAME_H`. |
| File-name vs class-name mismatch | `^class\s+([A-Z]\w+)` over `src/**/*.h` + manual cross-check vs the file path | Legacy modules don't match (would break many includes to rename). |

### 3. Qt-specific conventions

| What to find | Grep pattern | Interpretation |
|---|---|---|
| Old `SIGNAL()` / `SLOT()` macro syntax | `SIGNAL\s*\(\|SLOT\s*\(` over `src/` | Each hit is a discrepancy. Convert to new-style `&Class::signal` pointer syntax. |
| `new QObject(...)` without explicit parent | `\bnew Q[A-Z][a-zA-Z]+\s*\(\s*\)` over `src/` | Look at the next 2-3 lines for an `addWidget`/`setLayout`/`setCellWidget` ownership transfer. If absent, real leak. If present, pragmatically safe but technically a discrepancy. |

### 4. Database rules

| What to find | Grep pattern | Interpretation |
|---|---|---|
| SQL built by string concatenation | `q\.prepare\([^"]*"[^"]*"\s*\+` over `src/` | If the concatenated parts are *identifiers* (table/column names) from internal constants, it's the only way to write generic helpers — flag but don't necessarily fix. If user input is concatenated, hard discrepancy with SQL-injection risk. |
| Comma decimal separator in numeric literals | `\.replace\("\.",\s*","\)` over `src/` | This is the *defensive* direction (replacing comma input with dot before binding). Not a discrepancy. The reverse (`\.replace\(",",\s*"\.")` writing comma into DB) would be one — none found in May 2026. |

### 5. Forbidden constructs

| What to find | Grep pattern | Interpretation |
|---|---|---|
| `std::exit` / `abort()` outside `main` | `std::exit\|::abort\(\)` over `src/` | Each hit is a discrepancy (known issue in `MainWindow` constructor — do not repeat). |
| `using namespace std/Qt` | `using namespace std\|using namespace Qt` over `src/` | Each hit is a discrepancy. |
| Hardcoded paths | `"[A-Z]:[/\\\\]\|"/[a-z]+/[a-z]+\|\.bat"\\)\|\.xlsx"\)` over `src/` | Each hit is a discrepancy — paths must come from `AppSettings`. |

### 6. Structure

| What to find | How | Interpretation |
|---|---|---|
| Two classes in one header | `^class\s+([A-Z]\w+)` over `src/**/*.h` and look for files with multiple `class X : public Y` lines | Each file with > 1 class definition is a discrepancy ("One class per .h/.cpp file pair"). |
| `m_` prefix on private members | Read each `.h` and inspect the `private:` section | Public members are not subject to the rule, but consider whether they should be private + accessor instead. |

---

## Severity grading

When listing findings, classify into one of three tiers:

1. **Real discrepancies — should fix in new code.** New code that violates the rule for no architectural reason. Easy fixes (rename, switch syntax). Always actionable.
2. **Discrepancies but architecturally justified.** Violates the letter of a rule but is the only practical way to express the design (e.g., generic SQL helpers that interpolate identifiers; widgets parented via subsequent layout calls). Document the justification; usually leave alone.
3. **Legacy — grandfathered.** Pre-existing identifiers / files / method names that the legacy note in `/coding-guidelines` explicitly protects (mostly `src/app/`, `src/add_garment/`, older listado / recog_prendas methods). Don't rename — diff noise and Qt auto-connect breakage outweigh the benefit.

---

## Current findings — 2026-05-26

### Tier 1 — Real discrepancies

All six Tier-1 findings from the 2026-05-26 audit are now closed (see `docs/progress_tracker.md` Completed Milestones): SIGNAL/SLOT macro in [src/tableview/tableview.cpp](../src/tableview/tableview.cpp); Spanish locals in [src/imprimir/imprimir.cpp](../src/imprimir/imprimir.cpp), [src/app/cancelinvoicedialog.cpp](../src/app/cancelinvoicedialog.cpp) and [src/recog_prendas/recog_prendas.cpp](../src/recog_prendas/recog_prendas.cpp); `add_sufix_to_filename` typo in [src/listado/genlistado.h](../src/listado/genlistado.h)/[.cpp](../src/listado/genlistado.cpp); two-classes-in-one-header in [src/verifactu/verifactuinvoice.h](../src/verifactu/verifactuinvoice.h) - now split into [verifactutaxitem.h](../src/verifactu/verifactutaxitem.h)/[.cpp](../src/verifactu/verifactutaxitem.cpp) (the tax-line class) and [verifactuinvoice.h](../src/verifactu/verifactuinvoice.h)/[.cpp](../src/verifactu/verifactuinvoice.cpp) (now `VerifactuInvoice`-only). Re-run the audit before re-populating this section.

### Tier 2 — Architecturally justified

| # | File(s) | Issue | Justification |
|---|---------|-------|---------------|
| 7 | [src/sql_lite/sql_lite.cpp:197,231,383,401,417](../src/sql_lite/sql_lite.cpp#L197) + [src/recog_prendas/recog_prendas.cpp:390](../src/recog_prendas/recog_prendas.cpp#L390) | SQL built by `+` concatenation of `table` / `column` / `dateType` arguments into prepared statements | `bindValue()` only binds values, not identifiers. All callers pass internal constants — no user input reaches these. Mitigation if ever needed: a small whitelist check inside each helper. |
| 8 | [src/app/cancelinvoicedialog.cpp:19,21,41](../src/app/cancelinvoicedialog.cpp#L19), [src/app/mainwindow.cpp:179,193](../src/app/mainwindow.cpp#L179) | `new QHBoxLayout()` / `new QLineEdit()` / `new QLabel()` / `new QComboBox()` without explicit parent at construction | All are added to a layout or table cell on the next few lines, transferring ownership. No actual leak; explicit parents would be more defensive. |
| 9 | Many headers — `Imprimir`, `Contabilidad`, `Facturas`, `AddGarment`, `Listado`, `GenListado`, `InsertNewItem`, `CancelInvoiceDialog`, `RectifyInvoiceDialog`, `PayDialog`, `RecogPrendas`, `MainWindow` | Public mutable members (e.g. `qrCode`, `isRecibo`, `revertirOn`, `pbAddedRows`, `isCellClicked`, `m_verifactu`) instead of private + accessor | Project pattern: parent assigns from outside. Anti-encapsulation but pervasive. **Resolved for `db`**: it is now a private member injected via each dialog's constructor (`new Foo(db, this)`) rather than a public field assigned post-construction — `MainWindow::db` is the sole remaining public DB handle (the owner). The remaining public flags/pointers are still a separate, larger task. |

### Tier 3 — Legacy / grandfathered

| # | What | Where |
|---|------|-------|
| 10 | snake_case methods (non-Qt-slot) | [src/listado/genlistado.h:30-44](../src/listado/genlistado.h#L30) (`print_table`, `initial_settings`, `set_cb_fechas`, `generate_html_*`, `write_html`, `check_years_invoice_type_for_row`, `add_sufix_to_filename`); `src/app/` and `src/add_garment/` also have similar legacy names. |
| 11 | Spanish identifier `revertirOn` | [src/contabilidad/contabilidad.h:27](../src/contabilidad/contabilidad.h#L27) — `bool revertirOn = false;` |
| 12 | File-name vs class-name mismatch | [src/add_garment/add_garment.h](../src/add_garment/add_garment.h) (class `AddGarment`), [src/recog_prendas/recog_prendas.h](../src/recog_prendas/recog_prendas.h) (class `RecogPrendas`) — keep as-is to avoid renaming every include site. |

### Confirmed clean

- Non-ASCII punctuation in `src/`: **0** hits.
- Spanish comments: **0**.
- Spanish text in `qDebug` / `qWarning` / `qCritical`: **0**.
- `std::exit` / `abort()` / `using namespace`: **0**.
- Hardcoded paths in `src/`: **0** (all in `AppSettings`).
- Comma-as-decimal-separator stored to DB: **0** (all comma occurrences are `.replace(",", ".")` calls that *fix* user input before binding).
- Header-guard format: all match `CLASSNAME_H`.

---

## How to update this report

1. Re-run the greps above (a fresh agent can run them in parallel — they are independent).
2. For each finding, decide which tier it belongs to using the **Severity grading** rules.
3. Replace the **Current findings** section in this file; do not maintain a history (the goal is "what's open today", not "everything we ever found"). Past closures live in `progress_tracker.md` Completed Milestones if they warranted a fix.
4. Bump the **Last run** date at the top.
5. Register any new doc files (this one is already in `INDEX.md` and `README.md`).
