# Coding-guidelines audit — method

How to check `src/` against the rules in [SKILL.md](SKILL.md). The [`guidelines-auditor`](../../agents/guidelines-auditor.md) subagent runs it (before a release, or after large feature work) and returns tiered findings; the main agent fixes Tier 1 items or files them in `docs/progress_tracker.md`. No findings snapshot is kept: what is open lives in the tracker, what was fixed in `docs/completed_milestones.md`.

The greps use ripgrep syntax (the `Grep` tool) and are independent, so run them in parallel. Paths are from the repo root. Never report generated files (`ui_*.h`, `moc_*`, anything under `build*/`).

---

## Checklist

### 1. Language

| What to find | Grep pattern | Interpretation |
|---|---|---|
| Non-ASCII punctuation (em-dash, en-dash, curly quotes, ellipsis) | `[—–“”‘’…]` over `src/` | Any hit is a finding (Windows code-page hazard). Spanish letters `áéíóúñ¿¡` are allowed everywhere. |
| Spanish text in `qDebug` / `qWarning` / `qCritical` | `q(Debug\|Warning\|Critical)\(\)\s*<<\s*"[^"]*[áéíóúñ¿¡]` | Diagnostic logs must be English. UI-facing strings stay Spanish. |
| Spanish words as local variables (DB-column mirroring) | `\b(QString\|bool\|int\|float\|double)\s+(prenda\|recibo\|cliente\|importe\|cantidad\|pagado\|estado\|servicio\|observaciones\|nombre\|fecha\|tipo\|trim\|mes\|año\|direccion\|telefono)\b` | New code uses English (`client`, `date`, `state`). |
| Spanish comments | `^\s*//\s*(Comprueba\|Busca\|Crea\|Revisa\|Comprobar\|Borrar\|Eliminar\|Insertar\|Actualizar\|Cuando\|Aquí\|Si\s\|Y\s\|Para\s)` | Any hit is a finding. |

### 2. Naming

| What to find | Grep pattern | Interpretation |
|---|---|---|
| `snake_case` method declarations | `\b(void\|bool\|int\|QString)\s+[a-z][a-z0-9]+_[a-z0-9_]+\s*\(` over `src/**/*.h` | Skip Qt auto-connect slots `on_<obj>_<signal>` (snake_case is required) and the legacy names listed below. Anything else is a finding. |
| Header guards | `^#ifndef\|^#define\|^#endif` over `src/**/*.h` | Each header has `#ifndef CLASSNAME_H` / `#define CLASSNAME_H` / `#endif // CLASSNAME_H`. |
| File name vs class name | `^class\s+([A-Z]\w+)` over `src/**/*.h`, compared with the file name | Only the legacy mismatches below are accepted. |

### 3. Qt

| What to find | Grep pattern | Interpretation |
|---|---|---|
| `SIGNAL()` / `SLOT()` macros | `SIGNAL\s*\(\|SLOT\s*\(` over `src/` | Each hit is a finding: use `&Class::signal`. |
| `new Q...()` without a parent | `\bnew Q[A-Z][a-zA-Z]+\s*\(\s*\)` over `src/` | Read the next 2-3 lines: added to a layout / cell / parent there → Tier 2; no ownership transfer → real leak, Tier 1. |

### 4. Database

| What to find | Grep pattern | Interpretation |
|---|---|---|
| SQL built by concatenation | `prepare\([^"]*"[^"]*"\s*\+` and `"\s*\+\s*\w+\s*\+\s*"` near `prepare`/`exec` over `src/` | Identifiers (table / column names) from internal constants → Tier 2 (`bindValue` cannot bind identifiers). Any value or user input concatenated → Tier 1, SQL-injection risk. |
| Comma written as decimal separator | `\.replace\(\s*"\."\s*,\s*","\s*\)` over `src/` | Writing `,` into stored numbers is a finding. The reverse (`replace(",", ".")` on input) is the defensive fix, not a finding. |

### 5. Forbidden constructs

| What to find | Grep pattern | Interpretation |
|---|---|---|
| `std::exit` / `abort()` outside `main` | `std::exit\|::abort\(\)\|\babort\(\)` over `src/` | Each hit is a finding. |
| `using namespace std/Qt` | `using namespace (std\|Qt)` over `src/` | Each hit is a finding. |
| Hardcoded paths | `"[A-Z]:[/\\\\]\|"/[a-z]+/[a-z]+\|\.bat"\)\|\.xlsx"\)` over `src/` | Each hit is a finding: paths come from `AppSettings`. |

### 6. Structure

| What to find | How | Interpretation |
|---|---|---|
| Two classes in one header | `^class\s+[A-Z]\w+\s*(:\|\{)` over `src/**/*.h`, files with more than one hit | Finding ("one class per .h/.cpp pair"); small private structs are fine. |
| Private members without `m_` | Read the `private:` sections | Finding in new code; public members are covered under exceptions below. |

---

## Severity

1. **Tier 1 — fix.** New code breaking a rule for no architectural reason. Always actionable; fix in the current work or file a tracker row.
2. **Tier 2 — justified.** Breaks the letter of a rule but is the practical way to express the design. State the justification; usually leave alone.
3. **Tier 3 — legacy.** Older identifiers / files protected by the legacy note in SKILL.md. Never propose renames: diff noise and Qt auto-connect breakage outweigh the benefit.

---

## Accepted exceptions — do not report again

These were reviewed and deliberately kept. Report one only if it **grows** (a new instance of the same pattern in new code is Tier 1).

**Tier 2**
- **Identifier interpolation in generic SQL helpers**: `sql_lite.cpp` helpers taking `table` / `column` arguments, and Recogida's date search (`dateType` from a fixed combo). All callers pass internal constants; values are always bound.
- **Widgets created without a parent and then added to a layout or table cell** in the next lines (e.g. `CancelInvoiceDialog`, `MainWindow` combo cells): no leak, ownership transfers.
- **Public mutable members** set by the parent after construction (`qrCode`, `isRecibo`, `revertirOn`, `pbAddedRows`, `isCellClicked`, `m_verifactu`, `invoiceSeq`...) on the older dialogs (`Imprimir`, `Contabilidad`, `Facturas`, `AddGarment`, `Listado`, `GenListado`, `InsertNewItem`, `CancelInvoiceDialog`, `RectifyInvoiceDialog`, `PayDialog`, `RecogPrendas`, `MainWindow`). The DB handle is already private and constructor-injected everywhere; moving the rest behind accessors is a separate refactor.

**Tier 3**
- snake_case methods in `src/listado/genlistado.h` (`print_table`, `initial_settings`, `set_cb_fechas`, `generate_html_*`, `write_html`, `check_years_invoice_type_for_row`, `add_suffix_to_filename`) and in `src/app/` / `src/add_garment/`.
- Spanish identifier `revertirOn` in `src/contabilidad/contabilidad.h`.
- File name vs class name: `add_garment.h` (`AddGarment`), `recog_prendas.h` (`RecogPrendas`), `pay_dialog.h` (`PayDialog`).
