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

In Trimestral mode the quarter's lock state is read first via `sql_lite::readLockForQuarter()` for **both** `ingresos` and `gastos` (all three months in one query each), merged by `Contabilidad::combinedLockState`. The quarter is "no data" only if both tables are empty, and locked if either is, so a quarter with only gastos can be closed and reverted too. The closed/open labels in the Mensual and Anual headers use the same merge. Then:

1. `generateContabilidad()` builds the report (see [Report content](#report-content)) and writes it to PDF.
2. `updateLock()` sets `edit_lock` on all affected `ingresos` and `gastos` rows (via `updateLockForMonth`):
   - `revertirOn=false` → sets `edit_lock=1` (locks the period)
   - `revertirOn=true` → sets `edit_lock=0` (unlocks the period)

The **Bloquear datos** checkbox is enabled only in Trimestral mode and never while reverting (`Contabilidad::lockOptionAvailable`). Switching to Mensual or Anual unticks and greys it out, because only the quarterly flow closes the books.

## Report content

Every figure comes from the **detail rows** of the period, the same rows the report lists at the end. `sql_lite::incomeTicketsBetweenDates()` and `sql_lite::expensesBetweenDates()` are fetched once per period (annual: `sql_lite::annualDetailsByQuarter()`, one scan per table bucketed by quarter), and the pure static `Contabilidad::figuresFromDetails(income, expenses, ivaRate)` turns them into one `PeriodFigures`. The summary therefore cannot disagree with its detail tables. Per period the report renders three summary blocks plus the [detail tables](#detail-tables-audit-annex):

- **Ingresos** — importe (IVA incl.), base imponible, IVA repercutido at the fixed 21 % (`AppSettings::ivaRate()`, not configurable).
- **Gastos** — importe / base / IVA across the 10%, 21% and sin-IVA columns plus a Total column. Rows with another or NULL rate are counted but summed in no column.
- **Resumen** — the figures added in the report-visualisation pass:
  - **Liquidación de IVA**: IVA repercutido − IVA soportado = **Resultado IVA**, labelled "a ingresar" / "a compensar" by sign (the modelo-303 figure).
  - **Resultado del periodo**: base ingresos − base gastos (beneficio / pérdida).
  - **Operation counts**: net paid tickets (`Contabilidad::netTicketCount`: a ticket whose income in the period is fully offset by a regularisation of the same period, i.e. paid and cancelled there, is not counted; partially cancelled tickets still count) and gastos rows.

The page header (business name / address / city / NIF / phone + issue date), the stylesheet and the euro formatting come from the shared `src/reporthtml/` lib (`ReportHtml::documentOpen/documentClose/formatEuro`), shared with the listados PDFs. The annual report renders one section per quarter plus a **Resumen anual consolidado** summing the four (`PeriodFigures::accumulate`). Its ticket count is the exception: it uses `Contabilidad::yearTicketCount()`, the distinct tickets over the whole year netted against the year's regularisations, because a ticket paid across two quarters would otherwise count twice, and one paid and cancelled within the year should not count at all. The period date range and the four-way quarter/month switch both flow through `periodRange()`. Everything stays table-based because `QTextDocument` only renders a subset of HTML/CSS.

**Comma-decimal amounts** (an `importe` stored as `10,50`) are never summed, in any mode. The collectors count them (`IncomeTicketDetail::invalidAmounts`, `ExpenseDetail::invalidAmount`), the detail tables mark them, and `generateContabilidad()` shows one error dialog per report pointing at the decimal clean-up tool. The quarterly and annual reports therefore always agree.

### Detail tables (audit annex)

After the summary, each period gets a **Detalle** block listing the rows behind its figures, so every number can be audited:

- **Detalle de ingresos**: one line per paid ticket (`n_recibo`, fecha de pago, cliente, number of garments, base, IVA, importe) and a total row. Garment rows are aggregated by `n_recibo`. A ticket with a comma-decimal garment is marked `*` and that garment is not summed.
- **Detalle de gastos**: one line per `gastos` row (fecha, nº factura, empresa, servicio, IVA %, base, cuota, importe) and a total row. A row with an unrecognised or NULL IVA is marked `*`, a comma-decimal amount `**`, and neither enters the total, with a note under the table.

Both tables end with a note that per-line base/IVA are rounded to cents while the totals are computed unrounded. The total rows equal the summary by construction. The listings filter with the shared `kIngresosIncomeWhere` / `kGastosPeriodWhere` predicates in `sql_lite.cpp`. Trimestral and mensual reports append one Detalle block for the period, and the annual report one per quarter after the consolidated summary. The HTML comes from the pure statics `Contabilidad::createHtmlDetailIngresos` / `createHtmlDetailGastos`, which are unit-tested. They use the borderless, zebra-striped `ReportHtml::tableOpen()` style because these tables span pages.

## Output

The report (PDF) is written to `AppSettings::instance()->contabilidadPath()` (= `<reports.root>/Contabilidad`) and opened automatically via `QDesktopServices::openUrl()`. Trimestral reports land directly in that folder; mensual reports under `Contabilidad/Mensual`, anual reports under `Contabilidad/Anual` (appended in `contabilidad.cpp`). `QDir::mkpath()` is called on demand.

## Verifactu interaction

The income predicate (`kIngresosIncomeWhere`) counts only `pagado = 'SI'` rows and excludes `verifactu_estado` `ANULADA` (voided in place or cancelled at AEAT) and `RECTIFICADA` (superseded by a substitution rectificativa, whose new row carries the corrected total), **unless the row has a `fecha_anulacion`** (see below). All other estados (`ENVIADA`, `ERROR`, `PENDIENTE`, and legacy NULL/empty rows from before Verifactu) are included normally. The excluded states come from the single list `kTotalsExcludedEstados`, shared with `garmentExcludedFromTotals`.

## Cancellations are counted where they happen: regularisations

A filed quarter must not change when one of its tickets is later cancelled at the AEAT or replaced by a substitution rectificativa. Every such cancellation therefore records **`fecha_anulacion`** through `sql_lite::markInvoiceSeqCancelled` / `markTicketRectified`: the cancellation date (today), or the rectificativa's date. The date is never overwritten once set. A paid row with a `fecha_anulacion`:

- still counts as income in its **payment period** (`kIngresosIncomeWhere` lets it through), so regenerating that period reproduces its figures, closed or not;
- is subtracted in the period containing `fecha_anulacion`. `sql_lite::regularizationsBetweenDates()` (annual: `QuarterlyDetails::regularizations`) lists it, `figuresFromDetails` nets it out of income (`PeriodFigures::ingRegularizacion`), the Ingresos table shows "Ingresos del periodo" / "Anulaciones / rectificaciones del periodo" / net "Importe total", and the detail annex adds an **Anulaciones y rectificaciones del periodo** table with negative amounts.

When the payment and the cancellation fall in the same period the two lines net out, so that period's figure equals simply leaving the ticket out. Two guards keep a filed report from moving. The rectificativa date must fall in an open quarter (`RectifyInvoiceDialog`, via `sql_lite::quarterIsClosed`). A cancellation is refused while today's quarter is closed (`CancelInvoiceDialog`), because the subtraction is dated today. `quarterIsClosed` checks the whole quarter across `ingresos` and `gastos`, so a month without rows inside a closed quarter still reads as closed. Cancellations made before 10.12 have no `fecha_anulacion` and stay excluded from their own period.

Garments **voided in place** (Anular prendas) also get `fecha_anulacion`, but they are unpaid (`pagado = 'NO'`), so they are never income nor a regularisation.

## Date range

Both detail listings use a half-open interval `[startDate, endDate)` — `>= startDate AND < endDate`. `endDate` is always the first day of the next quarter, so this correctly excludes that boundary day from the current quarter. The range itself is computed by the pure static `Contabilidad::periodRangeFor(mode, unit, year, &start, &endExclusive)` (the `periodRange()` member just reads the widgets and delegates), unit-tested in `tests/test_contabilidad.cpp`.
