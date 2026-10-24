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

`Contabilidad` is a `QDialog` built in code (no `.ui` file, like `RectifyInvoiceDialog`), window-modal over MainWindow, created with `WA_DeleteOnClose`, so each instance self-deletes when closed — callers `show()` it and drop the pointer. Layout, top to bottom: an explanation panel (generate / revert); a **Periodo** group (Tipo de informe, Año, Trimestre / Mes); an **Opciones** group (Bloquear el trimestre, Incluir el detalle); the actions, **Comprobar bloqueo** on the left and the primary **Generar contabilidad** (**Revertir contabilidad** in revert mode) on the right; a **result panel** (`lblResult`) where every outcome is written - success in green, nothing to do in amber, comma-decimal amounts in red, with a link that opens the PDF - instead of pop-up message boxes; and **Cerrar**. The dialog **stays open** after each report or revert, so the operator can produce another period, the detailed version or check a lock; it has no minimise / help button and only Cerrar (or the window's close) dismisses it. Widgets carry object names (`cbConfig`, `sbYear`, `sbPeriod`, `chkLock`, `chkDetail`, `btnCheckLock`, `btnGenerate`, `lblResult`, `btnClose`) for the e2e bench. The `db` handle is a private member set through the constructor.

`MainWindow` opens this dialog twice: once with `revertirOn=false` for normal accounting generation, and once with `revertirOn=true` for the "revert accounting" action. In revert mode (`resetAllContents()` after `revertirOn = true`) the title, explanation and primary button switch to "Revertir contabilidad", the report type is forced to Trimestral and disabled, and the detail option is disabled (no report is written).

## Report modes

Modes are the `Contabilidad::ConfigMode` enum (`Mensual=0`, `Trimestral=1`, `Anual=2`), whose values match the `cb_config` combobox item order. Logic compares `currentMode()` (the combobox index) rather than the Spanish display text, so renaming an item cannot break the comparisons.

| Enum value | Mode | Description |
|----------|------|-------------|
| `Mensual` | Monthly | Report for a single month |
| `Trimestral` | Quarterly | Report for a quarter (Q1–Q4) |
| `Anual` | Annual | Full year report |

## On accept

In Trimestral mode the quarter's state is read first from `accounting_closures` (`sql_lite::quarterIsClosed()`): a closed quarter is one with a closure not yet reopened, whatever rows it has. Then:

- **Revertir**: a closed quarter is reopened (`reopenQuarter`: its closure gets `reopened_at` and is never deleted); an open one reports that there is nothing to revert.
- **Already closed**: the report is generated again; the quarter stays closed.
- **Unreadable data**: if any listing query fails (`*ok` false) no report is written and nothing is closed; the panel says the data could not be read.
- **No movements** (`quarterHasMovements`: no payment, cancellation or gasto dated in it): a warning, no report and no close.
- **Otherwise**: `generateContabilidad()` builds the report (see [Report content](#report-content)) and writes it to PDF. If the PDF could not be written (`writePdf` false: a folder in its place, the old PDF open in a viewer, no permission) the result panel shows the error and **the quarter is not closed** - a quarter is never closed without its report. With *Bloquear el trimestre* ticked, `closeQuarter()` then adds the closure with the report's file name. That happens only from the quarter's last day on (`Contabilidad::quarterLastDay`): earlier, the report is written but the quarter stays open, with an amber note giving the first day it can be closed. Closing a quarter that has not ended would refuse every payment of today. On the last day itself the closure goes ahead, with a warning that today's payments can no longer be recorded until it is reverted. A gasto whose IVA text is not a whole number (`sql_lite::expenseIvaRate`: NULL, empty, "21,5", "abc") reads as unrecognised (-1). It is left out of the sums, flagged in the detail, and counted in the red result-panel warning with the comma-decimal amounts. A quarter whose only movement is the cancellation of an earlier quarter's invoice is closed like any other.

The **Bloquear el trimestre** checkbox is enabled only in Trimestral mode and never while reverting (`Contabilidad::lockOptionAvailable`). Switching to Mensual or Anual unticks and greys it out, because only the quarterly flow closes the books.

**Incluir el detalle de tickets y facturas** (unticked by default, disabled while reverting) adds the [detail tables](#detail-tables-audit-annex) to the report; without it the PDF has only the summary blocks. The two versions are separate files (`Contabilidad::reportRelativePath`): `contabilidad_trimestral_<year>_<q>.pdf`, `Mensual/reporte_mensual_<year>_<m>.pdf`, `Anual/reporte_anual_<year>.pdf`, and the same names ending in `_detalle.pdf` for the detailed one, whose title also ends in "· Detalle".

**Comprobar bloqueo** shows in the result panel whether the selected period is closed, without generating anything (`Contabilidad::lockStatusMessage`, from `quarterClosures`): a quarter reads locked with the date it was closed (no date for one carried over from the old `edit_lock` flags), not locked with the date it was reverted, not locked, or without records; a month reports its quarter (`quarterIsClosed`), a year lists its four quarters.

## Report content

Every figure comes from the **detail rows** of the period, the same rows the report lists at the end. `sql_lite::incomeTicketsBetweenDates()` and `sql_lite::expensesBetweenDates()` are fetched once per period (annual: `sql_lite::annualDetailsByQuarter()`, one scan per table bucketed by quarter), and the pure static `Contabilidad::figuresFromDetails(income, expenses, ivaRate)` turns them into one `PeriodFigures`. The summary therefore cannot disagree with its detail tables. Per period the report renders three summary blocks plus the [detail tables](#detail-tables-audit-annex):

- **Ingresos** — importe (IVA incl.), base imponible, IVA repercutido at the fixed 21 % (`AppSettings::ivaRate()`, not configurable).
- **Gastos** — importe / base / IVA in the columns IVA 21 %, IVA 10 %, **Subtotal con IVA** (21 % + 10 %), Sin IVA and Total, in that order (`PeriodFigures::gastosConIvaImporte` / `gastosConIvaBase`). Rows with another or NULL rate are counted but summed in no column.
- **Resumen** — the figures added in the report-visualisation pass:
  - **Liquidación de IVA**: IVA repercutido − IVA soportado = **Resultado IVA**, labelled "a ingresar" / "a compensar" by sign (the modelo-303 figure).
  - **Resultado del periodo**: base ingresos − base gastos (beneficio / pérdida).
  - **Operation counts**: net paid tickets (`Contabilidad::netTicketCount`: a ticket whose income in the period is fully offset by a regularisation of the same period, i.e. paid and cancelled there, is not counted; partially cancelled tickets still count; a by-differences credit note with a negative net is not a ticket). A regularisation offsets the count only when the cancelled payment was made in the same period, so a Q1 payment cancelled in Q2 never cancels out a new Q2 sale of the same ticket. Gastos rows are counted as-is.

The page header (business name / address / city / NIF / phone + issue date), the stylesheet and the euro formatting come from the shared `src/reporthtml/` lib (`ReportHtml::documentOpen/documentClose/formatEuro`), shared with the listados PDFs. The annual report renders one section per quarter plus a **Resumen anual consolidado** summing the four (`PeriodFigures::accumulate`). Its ticket count is the exception: it uses `Contabilidad::yearTicketCount()`, the distinct tickets over the whole year netted against the year's regularisations, because a ticket paid across two quarters would otherwise count twice, and one paid and cancelled within the year should not count at all. The period date range and the four-way quarter/month switch both flow through `periodRange()`. Everything stays table-based because `QTextDocument` only renders a subset of HTML/CSS.

**Comma-decimal amounts** (an `importe` stored as `10,50`) are never summed, in any mode. The collectors count them (`IncomeTicketDetail::invalidAmounts`, `ExpenseDetail::invalidAmount`), the detail tables mark them, and `generateContabilidad()` shows one error dialog per report pointing at the decimal clean-up tool. The quarterly and annual reports therefore always agree.

### Detail tables (audit annex)

When **Incluir el detalle** is ticked, each period gets a **Detalle** block after the summary, listing the rows behind its figures, so every number can be audited:

- **Detalle de ingresos**: one line per paid ticket (`n_recibo`, fecha de pago, cliente, number of garments, base, IVA, importe) and a total row. Garment rows are aggregated by `n_recibo`. A ticket with a comma-decimal garment is marked `*` and that garment is not summed.
- **Detalle de gastos**: one line per `gastos` row (fecha, nº factura, empresa, servicio, IVA %, base, cuota, importe) and a total row. A row with an unrecognised or NULL IVA is marked `*`, a comma-decimal amount `**`, and neither enters the total, with a note under the table.

Both tables end with a note that per-line base/IVA are rounded to cents while the totals are computed unrounded. The total rows equal the summary by construction. The listings filter with the shared `kIngresosIncomeWhere` / `kGastosPeriodWhere` predicates in `sql_lite.cpp`. Trimestral and mensual reports append one Detalle block for the period, and the annual report one per quarter after the consolidated summary. The HTML comes from the pure statics `Contabilidad::createHtmlDetailIngresos` / `createHtmlDetailGastos`, which are unit-tested. They use the borderless, zebra-striped `ReportHtml::tableOpen()` style because these tables span pages.

## IVA as invoiced

The IVA repercutido (and the modelo 303 result) is the sum of each invoice's cuota as registered at AEAT: every payment event of a ticket (`verifactu_invoice_seq`, `IncomeTicketDetail::invoices`) is split on its own with `splitGross`, and regularisations the same way; the base is the gross minus that IVA. A gasto is one supplier invoice, split on its own too. The detail tables use the same split, so their totals equal the summary to the cent. Owner's decision 20-10-2026.

## Output

Regenerating a quarter that is already closed never touches its filed report: the new PDF is written as `<name>_regenerado_YYYYMMDD[_N].pdf` (`Contabilidad::regeneratedPath`). Every PDF is written as `.part` and renamed (`ReportHtml::writePdf`), so a failed or locked write keeps the previous file.

The report (PDF) is written to `AppSettings::instance()->contabilidadPath()` (= `<reports.root>/Contabilidad`) and opened automatically via `QDesktopServices::openUrl()`. Trimestral reports land directly in that folder; mensual reports under `Contabilidad/Mensual`, anual reports under `Contabilidad/Anual` (appended in `contabilidad.cpp`). `QDir::mkpath()` is called on demand.

## Verifactu interaction

The income predicate (`kIngresosIncomeWhere`) counts only `pagado = 'SI'` rows and excludes `verifactu_estado` `ANULADA` (voided in place or cancelled at AEAT) and `RECTIFICADA` (superseded by a substitution rectificativa, whose new row carries the corrected total), **unless the row has a `fecha_anulacion`** (see below). All other estados (`ENVIADA`, `ERROR`, `PENDIENTE`, and legacy NULL/empty rows from before Verifactu) are included normally. The excluded states come from the single list `kTotalsExcludedEstados`, shared with `garmentExcludedFromTotals`.

## Cancellations are counted where they happen: regularisations

A filed quarter must not change when one of its tickets is later cancelled at the AEAT or replaced by a substitution rectificativa. Every such cancellation therefore records **`fecha_anulacion`** through `sql_lite::markInvoiceSeqCancelled` / `markTicketRectified`: the cancellation date (today), or the rectificativa's date. The date is never overwritten once set. A paid row with a `fecha_anulacion`:

- still counts as income in its **payment period** (`kIngresosIncomeWhere` lets it through), so regenerating that period reproduces its figures, closed or not;
- is subtracted in the period containing `fecha_anulacion`. `sql_lite::regularizationsBetweenDates()` (annual: `QuarterlyDetails::regularizations`) lists it, `figuresFromDetails` nets it out of income (`PeriodFigures::ingRegularizacion`), the Ingresos table shows "Ingresos del periodo" / "Anulaciones / rectificaciones del periodo" / net "Importe total", and the detail annex adds an **Anulaciones y rectificaciones del periodo** table with negative amounts.

When the payment and the cancellation fall in the same period the two lines net out, so that period's figure equals simply leaving the ticket out. Two guards keep a filed report from moving. The rectificativa date must fall in an open quarter (`RectifyInvoiceDialog`, via `sql_lite::quarterIsClosed`). A cancellation is refused while today's quarter is closed (`CancelInvoiceDialog`), because the subtraction is dated today. A refused rectificativa counted in a quarter closed since is resent with its own date, amount and mode, its row untouched; the original's subtraction then goes to an open period (`sql_lite::openPeriodDate`: today, or the first day of the next open quarter when today's is closed). `quarterIsClosed` checks the whole quarter across `ingresos` and `gastos`, so a month without rows inside a closed quarter still reads as closed. Cancellations made before 10.12 have no `fecha_anulacion` and stay excluded from their own period.

Garments **voided in place** (Anular prendas) also get `fecha_anulacion`, but they are unpaid (`pagado = 'NO'`), so they are never income nor a regularisation.

## Date range

Both detail listings use a half-open interval `[startDate, endDate)` — `>= startDate AND < endDate`. `endDate` is always the first day of the next quarter, so this correctly excludes that boundary day from the current quarter. The range itself is computed by the pure static `Contabilidad::periodRangeFor(mode, unit, year, &start, &endExclusive)` (the `periodRange()` member just reads the widgets and delegates), unit-tested in `tests/test_contabilidad.cpp`.
