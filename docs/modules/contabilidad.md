# Contabilidad (`src/contabilidad/`)

Generates the HTML/PDF accounting reports (monthly, quarterly, annual) and closes or reopens accounting quarters. Source: `src/contabilidad/contabilidad.h/cpp`. Tests: `tests/test_contabilidad.cpp` (`Contabilidad` suite, see [unit suites](../testing/unit_suites.md)). Tables and columns: [architecture](../architecture.md).

## The dialog

`Contabilidad` is a `QDialog` built in code (no `.ui`), window-modal, `WA_DeleteOnClose`: callers `show()` it and drop the pointer.

```cpp
new Contabilidad(db, this);                         // generate
new Contabilidad(db, this, Contabilidad::Revert);   // revert
```

Layout, top to bottom: explanation, **Periodo** group (Tipo de informe, Año, Trimestre / Mes), **Opciones** group (Bloquear el trimestre, Incluir el detalle), **Comprobar bloqueo** and the primary **Generar contabilidad** (**Revertir contabilidad** in revert mode), a result panel, **Cerrar**.

- Every outcome goes to the result panel (`lblResult`): green success with a link to the PDF, amber nothing to do, red comma-decimal warnings. No message boxes.
- The dialog stays open after each action; only Cerrar dismisses it.
- Widget object names (`cbConfig`, `sbYear`, `sbPeriod`, `chkLock`, `chkDetail`, `btnCheckLock`, `btnGenerate`, `lblResult`, `btnClose`) are used by the e2e bench.
- Revert mode: report type forced to Trimestral and disabled, detail option disabled (no report is written).
- The **Año** spinner starts at the oldest year in `ingresos.fecha_recepcion` or `gastos.fecha` (rows without date ignored).

## Report modes and options

`ConfigMode` (`Mensual=0`, `Trimestral=1`, `Anual=2`) matches the `cbConfig` item order; the code compares `currentMode()`, never the display text.

- **Bloquear el trimestre** is enabled only in Trimestral and never while reverting (`lockOptionAvailable`).
- **Incluir el detalle** (off by default) adds the [detail tables](#detail-tables). The two versions are separate files (`reportRelativePath`): `contabilidad_trimestral_<year>_<q>.pdf`, `Mensual/reporte_mensual_<year>_<m>.pdf`, `Anual/reporte_anual_<year>.pdf`, the detailed ones ending `_detalle.pdf`.
- **Comprobar bloqueo** (`lockStatusMessage`, from `quarterClosures`) reports whether the period is closed without generating anything: a quarter shows its closed or reverted date, a month its quarter, a year its four quarters. An unreadable closure history reads as locked (`quarterIsClosed` fails closed).

## Generating a quarter

The quarter's state is read from `accounting_closures` (`quarterIsClosed`): closed means a closure not yet reopened, whatever rows it has.

| Case | Result |
|------|--------|
| Revert, closed quarter | Sí / No confirmation (`confirmRevert`, injectable); `reopenQuarter` sets `reopened_at`, the closure row is never deleted; logged with `qWarning` |
| Revert, open quarter | Nothing to revert |
| Any listing query fails | No report, nothing closed, the panel says the data could not be read |
| No movements (`quarterHasMovements`: no payment, cancellation or gasto) | Warning, no report, no close |
| Otherwise | `generateContabilidad()` writes the PDF; with *Bloquear* ticked, `closeQuarter()` stores the closure with the report's file name |

- A quarter is never closed without its report: if the PDF cannot be written the panel shows the error and the quarter stays open.
- Closing happens only from the quarter's last day on (`quarterLastDay`, forwards to `sql_lite`). Earlier, the report is written, the quarter stays open and an amber note gives the first day it can be closed. On the last day itself the closure goes ahead with a warning that today's payments can no longer be recorded until it is reverted.
- A quarter already closed is regenerated and stays closed. The new PDF never overwrites the filed one: it is `<name>_regenerado_YYYYMMDD[_N].pdf` (`regeneratedPath`).
- A quarter whose only movement is the cancellation of an earlier quarter's invoice closes like any other.

## Report content

Every figure comes from the detail rows of the period, fetched once (`incomeTicketsBetweenDates`, `regularizationsBetweenDates`, `expensesBetweenDates`; annual: `annualDetailsByQuarter`, one scan per table bucketed by quarter). The one pure builder is `figuresFromDetails(income, regularizations, expenses, ivaRate, periodStart, periodEnd)`, which returns a `PeriodFigures`; the summary therefore cannot disagree with its detail tables.

- **Ingresos**: importe (IVA incl.), base, IVA repercutido at `AppSettings::ivaRate()` (21 %). Regularisations are netted out (`ingRegularizacion`): the table shows "Ingresos del periodo", "Anulaciones / rectificaciones del periodo" and the net "Importe total".
- **Gastos**: columns IVA 21 %, IVA 10 %, **Subtotal con IVA**, Sin IVA, Total. Only rates 0, 10 and 21 are summed (`expenseIvaIsSummarised` in `sql_lite.h`, the single list of supported rates).
- **Resumen**: Liquidación de IVA (repercutido − soportado = **Resultado IVA**, "a ingresar" / "a compensar"), Resultado del periodo (base ingresos − base gastos), and the operation counts.
- **Gastos not summed**: `gasFacturas` counts only the supplier invoices that are summed. Those left out (`gasNoSumadas`: unreadable amount, or an IVA rate other than 0 / 10 / 21, including NULL or "21,5") are named in the summary and flagged in the detail. They also feed `invalidAmountCount`, which raises the red warning after generating.
- **Comma-decimal and non-numeric amounts** (`10,50`, "abc", empty) are never summed: the collectors flag them (`IncomeTicketDetail::invalidAmounts`, `ExpenseDetail::invalidAmount`), the detail marks them, and the panel's red line points at the decimal clean-up tool.
- **Ticket counts**: `netTicketCount` counts paid tickets and drops one fully offset by a regularisation of the same period (a regularisation offsets only a payment made in that period; an empty or unreadable payment date counts as this period; a negative by-differences credit note is not a ticket). `gasFacturas` is a plain count. The annual count is `yearTicketCount`, distinct tickets over the year netted against its regularisations.
- **IVA as invoiced**: each invoice's cuota (every payment event of a ticket, `IncomeTicketDetail::invoices`, each regularisation, each gasto) is split on its own with `splitGross`; base is gross minus that IVA. The detail totals use the same split. Owner's decision 20-10-2026.
- **Annual report**: one section per quarter plus **Resumen anual consolidado** (`PeriodFigures::accumulate`); mensual and quarterly headings end "Contabilidad cerrada" / "no cerrada" (a month shows its quarter's state).
- Header, stylesheet and euro formatting come from `src/reporthtml/` (`ReportHtml::documentOpen/Close/formatEuro`). Everything is table-based because `QTextDocument` renders only a subset of HTML/CSS.

### Detail tables

Rendered by `createHtmlDetailIngresos`, `createHtmlDetailRegularizaciones` (only when there are any) and `createHtmlDetailGastos` (pure statics, unit-tested), in the borderless zebra `ReportHtml::tableOpen()` style. One block per period; the annual report one per quarter after the consolidated summary.

- **Detalle de ingresos**: one line per paid ticket (`n_recibo`, fecha de pago, cliente, garments, base, IVA, importe) and a total row; a ticket with a comma-decimal garment is marked `*`.
- **Anulaciones y rectificaciones del periodo**: one line per regularisation, negative amounts.
- **Detalle de gastos**: one line per `gastos` row (fecha, nº factura, empresa, servicio, IVA %, base, cuota, importe) and a total row; an unrecognised IVA is marked `*`, a comma-decimal amount `**`, and neither enters the total.
- Lines are rounded to cents, totals are not (a note says so); total rows equal the summary. The listings use the shared `kIngresosIncomeWhere` / `kGastosPeriodWhere` predicates in `sql_lite.cpp`.

## Date range

The three listings (income by `fecha_pago`, regularisations by `fecha_anulacion`, gastos by `fecha`) use the half-open interval `[start, endExclusive)`, where `endExclusive` is the first day after the period. `periodRangeFor(mode, unit, year, &start, &endExclusive)` is the pure computation (`periodRange()` reads the widgets and delegates).

## Output

PDFs are written to `AppSettings::contabilidadPath()` (`<reports.root>/Contabilidad`; mensual under `Mensual/`, anual under `Anual/`), created on demand, as `.part` then renamed (`ReportHtml::writePdf`), so a failed write keeps the previous file. The PDF is opened and linked in the result panel.

## Verifactu interaction

Rules and states: [Verifactu](verifactu/README.md). Here:

- `kIngresosIncomeWhere` counts only `pagado = 'SI'` rows and excludes `verifactu_estado` `ANULADA` and `RECTIFICADA` (the list `kTotalsExcludedEstados`, shared with `garmentExcludedFromTotals`), unless the row has a `fecha_anulacion`. Every other estado, and legacy NULL / empty, counts.
- **Cancellations are counted where they happen.** A cancellation or substitution records `fecha_anulacion` (today, or the rectificativa's date; never overwritten) through `markInvoiceSeqCancelled` / `markTicketRectified`. Such a paid row still counts in its payment period, so regenerating that period reproduces its figures, and is subtracted in the period containing `fecha_anulacion` (`regularizationsBetweenDates`). When both fall in one period they net out.
- Two guards keep a filed report from moving. The rectificativa date must fall in an open quarter (`RectifyInvoiceDialog`). A cancellation is refused while today's quarter is closed (`CancelInvoiceDialog`). A rectificativa refused for a quarter closed since is resent with its own date, and the original's subtraction goes to `openPeriodDate` (today, or the first day of the next open quarter).
- Garments voided in place (Anular prendas) are unpaid, so they are never income nor a regularisation.

## Public API

| Group | Functions |
|-------|-----------|
| Period | `periodRangeFor`, `quarterLastDay`, `lockOptionAvailable`, `lockStatusMessage` |
| Files | `reportRelativePath`, `regeneratedPath` |
| Figures | `figuresFromDetails`, `PeriodFigures` (+ `accumulate`), `netTicketCount`, `yearTicketCount`, `invalidAmountCount` |
| HTML | `createHtmlDetailIngresos`, `createHtmlDetailRegularizaciones`, `createHtmlDetailGastos`, `createHtmlTableGastos` |
| Dialog | `confirmRevert` (injectable), `Purpose` (`Generate`, `Revert`) |
