# Contabilidad (`src/contabilidad/`)

Generates HTML accounting reports and manages accounting period locks.

## Source files

- `src/contabilidad/contabilidad.h/cpp`

## Key interface

```cpp
Contabilidad *ui = new Contabilidad(db, this);  // db injected via constructor
ui->revertirOn = false;  // set true to unlock instead of lock
ui->show();
```

`Contabilidad` is a `QDialog` (window-modal) created with `WA_DeleteOnClose`, so each instance self-deletes when closed — callers `show()` it non-modally and drop the pointer. The `db` handle is a private member set through the constructor.

`MainWindow` opens this dialog twice: once with `revertirOn=false` for normal accounting generation, and once with `revertirOn=true` for the "revert accounting" action. In revert mode the mode combobox is forced to Trimestral and disabled.

## Report modes

Modes are the `Contabilidad::ConfigMode` enum (`Mensual=0`, `Trimestral=1`, `Anual=2`), whose values match the `cb_config` combobox item order. Logic compares `currentMode()` (the combobox index) rather than the Spanish display text, so renaming an item cannot break the comparisons.

| Enum value | Mode | Description |
|----------|------|-------------|
| `Mensual` | Monthly | Report for a single month |
| `Trimestral` | Quarterly | Report for a quarter (Q1–Q4) |
| `Anual` | Annual | Full year report |

## On accept

In Trimestral mode the quarter's lock state is read first via `sql_lite::readLockForQuarter()` (all three months in one query, so a quarter with income only in its early months is not mistaken for empty). Then:

1. `generateContabilidad()` builds the report (see [Report content](#report-content)) and writes it to PDF.
2. `updateLock()` sets `edit_lock` on all affected `ingresos` and `gastos` rows (via `updateLockForMonth`):
   - `revertirOn=false` → sets `edit_lock=1` (locks the period)
   - `revertirOn=true` → sets `edit_lock=0` (unlocks the period)

## Report content

Per period the report renders three blocks, all from one `PeriodFigures` struct computed once by `computeFigures(trim)`:

- **Ingresos** — importe (IVA incl.), base imponible, IVA repercutido (single rate from `AppSettings::ivaRate()`).
- **Gastos** — importe / base / IVA across the 10%, 21% and sin-IVA columns plus a Total column.
- **Resumen** — the figures added in the report-visualisation pass:
  - **Liquidación de IVA**: IVA repercutido − IVA soportado = **Resultado IVA**, labelled "a ingresar" / "a compensar" by sign (the modelo-303 figure).
  - **Resultado del periodo**: base ingresos − base gastos (beneficio / pérdida).
  - **Operation counts**: distinct paid tickets (ingresos) and invoice rows (gastos), via `sql_lite::countOperationsBetweenDates()`.

The page header (business name / address / city / NIF / phone + issue date), the stylesheet and the euro formatting come from the shared `src/reporthtml/` lib (`ReportHtml::documentOpen/documentClose/formatEuro`), shared with the listados PDFs. The annual report renders one section per quarter plus a **Resumen anual consolidado** summing the four (`PeriodFigures::accumulate`). The period date range and the four-way quarter/month switch both flow through `periodRange()`. Everything stays table-based because `QTextDocument` only renders a subset of HTML/CSS.

## Output

The report (PDF) is written to `AppSettings::instance()->contabilidadPath()` (= `<reports.root>/Contabilidad`) and opened automatically via `QDesktopServices::openUrl()`. Trimestral reports land directly in that folder; mensual reports under `Contabilidad/Mensual`, anual reports under `Contabilidad/Anual` (appended in `contabilidad.cpp`). `QDir::mkpath()` is called on demand.

## IVA breakdown

`getTotalIncome(table, iva, trimForYearConfig)` sums `importe` for a specific IVA rate and period. Called separately for 21%, 10%, and 0% rates when generating the report.

## Verifactu interaction

`totalPriceBetweenDates` (in `sql_lite.cpp`) excludes `ingresos` rows where `verifactu_estado = 'ANULADA'` from the quarterly sum. A Verifactu-cancelled invoice must not appear in taxable income. All other estados (`ENVIADA`, `ERROR`, `PENDIENTE`, and legacy NULL/empty rows from before Verifactu) are included normally.

## Date range

Both `ingresos` and `gastos` queries use a half-open interval `[startDate, endDate)` — `>= startDate AND < endDate`. `endDate` is always the first day of the next quarter, so this correctly excludes that boundary day from the current quarter. The range itself is computed by the pure static `Contabilidad::periodRangeFor(mode, unit, year, &start, &endExclusive)` (the `periodRange()` member just reads the widgets and delegates), unit-tested in `tests/test_contabilidad.cpp`.
