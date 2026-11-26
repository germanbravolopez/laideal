# sql_lite (`src/sql_lite/`)

Stateless free-function database API; every module includes `sql_lite.h`. Schema: [architecture.md](../architecture.md). Verifactu rules: [verifactu/README.md](verifactu/README.md).

Source: `sql_lite.h/cpp`, and `ingresos_schema.h` (`INGRESOS_COL_*` column indices, `INGRESOS_IMPORTE_VALUE`, `INGRESOS_ESTADO_ANULADO` / `_RECOGIDO` / `_EN_TIENDA`, `INGRESOS_PAGADO_SI` / `_NO`; `test_ingresosSchema_matchesColumnIndices` keeps the indices in step with the real table).

## Conventions

- **Connection**: every function opens through `ConnectionScope` and closes on return only if it opened (a caller's open connection, e.g. a table model's, stays open: `test_closureFunctions_leaveOpenConnectionOpen`). Declare the scope before any `QSqlQuery`.
- **Path**: `main()` calls `setDbPath(AppSettings::dbPath())`; code reads `dbPath()`. The path lives in `~/.laideal_settings.json` (`db.path`); if empty or missing, `main()` opens `SettingsDialog` first.
- **SQL**: prepared statements with named parameters only, never concatenation.
- **No UI**: no `QMessageBox` (links Qt Gui only). A failure is logged and returned; the window shows it.
- **Fail closed**: listings and numbering take `bool *ok` (`incomeTicketsBetweenDates`, `expensesBetweenDates`, `regularizationsBetweenDates`, `annualDetailsByQuarter`, `readMaxValueInColumnFromTable`, `nextTicketNumber`, `nextRectificationNumber`, `nextVerifactuInvoiceSeq`, `ticketHasPaidGarment`, `ticketHeader`, `rectifiedDifferences`, `differenceRectificationCount`, `liveRectificationOf`): false means the read failed, never "empty period" or "number 1". `nextTicketNumber` returns 0 then.
- **Amounts**: stored as `"28.41"` through `moneyText`. Every amount writer (`insertGarmentRow(s)`, the three `update...` amount writers, `splitGarmentRow`, `storeChargedGarments`, `insertExpenseInvoice`) refuses an unreadable amount (`acceptAmount`, built on `moneyParse`), never storing it as 0.
- **Closed quarters**: the check runs inside the INSERT / UPDATE statement (`closedQuarterSql`, a `NOT EXISTS` on the row's quarter; `kUnlockedWhere`), so it cannot race. Writers that change an amount also use `kAmountEditableWhere`: unpaid, never sent to AEAT, unlocked. A date that is empty or malformed is in no quarter.
- **Single sources**: income / expense figures come from the listings below (the report sums the same rows); `kIngresosIncomeWhere` and `kTotalsExcludedEstados` define "counts as income".
- `sql_lite` reads AeatStore's `aeat_records` only in `pendingAeatRecordCount` and `aeatInvoiceNumbersOfTicket` (`test_aeat_store` runs them against the real table).

## Dates, money, text helpers (pure)

| Function | Purpose |
|----------|---------|
| `kDbDateFormat`, `parseDbDate(text)`, `dbDateText(date)` | The stored `dd-MM-yyyy` text and its two conversions (invalid `QDate` for other text). |
| `quarterOfMonth`, `quarterFirstDay`, `quarterLastDay`, `quarterKey(q, y)` / `quarterKey(date)` | Quarter arithmetic; the key is `year * 10 + quarter`. |
| `roundToCents`, `moneyText(double/QString)` | Round half away from zero (28.405 -> 28.41); format `"28.41"`, text may use a decimal comma. |
| `moneyParse(text, &value)` | Strict: sign, digits, one separator. `"1.234,50"`, `"abc"`, `""` refused. |
| `parseDecimal(text, &ok)` | Lenient typed decimal with comma (size, quantity). |
| `displayMoney`, `displayEuro`, `displayDecimal` | Display with the decimal comma (`displayEuro` adds the euro sign); storage and AEAT keep the dot. |
| `garmentImporte(qty, size, unitPrice)` | Line price: quantity x unit price, times a non-zero size, >= 0, rounded to cents. Shared by MainWindow, AddGarment, Recogida. |
| `garmentUnmeasured(garment, size)` | An "m2" garment with no usable size: 0,00 and not chargeable. |
| `expenseIvaRate(stored)` | Whole-number rate from the TEXT `gastos.iva`, else -1. |
| `removeSpecialChars` | Accent-insensitive name matching. |
| `verifactuInvoiceId(n, seq)`, `verifactuDisplayInvoiceId(ids, fallback)` | InvoiceID (`n` for seq 0, `n-seq` otherwise); the id to print (first non-empty among the rows). |
| `garmentIsLocallyVoidable`, `garmentExcludedFromTotals` | Row-state predicates (unpaid and unsent; ANULADA / RECTIFICADA). |
| `genHash16()` | 16-character row hash. |

## Migration

`migrateDatabase(db, rewriteData = true)` returns false when a step failed (logged, the run goes on). It is idempotent and runs at every start, so each repair filters out rows it already rewrote and stays a cheap single scan. Each step is a function over a shared `MigrationRun`; multi-row steps go through `runAllOrNone` (one transaction, all rows or none). Steps in the order the code runs them:

1. `migrateSchema`: base tables (`kBaseSchema`), the `verifactu_*` / `fecha_anulacion` columns, `accounting_closures` (`migrateAccountingClosures`, which carries over the old `edit_lock` closures once), the guard triggers.
2. Only with `rewriteData` (false when no database copy could be taken first), the row repairs: `relabelUnpaidPending`, `moveVoidDates`, `reopenCancelledUnpaid`, `clearStampedUnpaid`, `relinkSplitRows`, `rehashDuplicates` (adds the `(n_recibo, hash)` unique index; before rounding), `roundOpenAmounts`, `fixCommaDecimals`, `normaliseEstadoCase`.

Rows that need a manual look are logged once (`migration_notes`).

## Reads and numbering

| Function | Purpose |
|----------|---------|
| `readMaxValueInColumnFromTable`, `readMaxNMinYearInColumnFromTable`, `readColumnFromTable` | Numeric max; max/min four-digit year of a `dd-MM-yyyy` column; one column's values. |
| `readGarmentPrice(db, garment, service)` | Unit price (`Limp.` / `Plan.`); -1 when not found or stored with a comma, 0 for an unknown service or a failed query. |
| `selectFromWhereLike`, `searchItemFromClient`, `readClientPhones` | Single-value lookups (exact or literal prefix); `clientes` lookups are exact-name. |
| `ticketHeader(db, n, &ok)` | `TicketHeader {found, client, receptionDate}`. |
| `ticketHasPaidGarment(db, n, &ok)` | Any `pagado = 'SI'` row (AddGarment refuses a paid ticket); fails closed. |
| `ticketAllGarmentsPaid` | Every row paid: the "IMPORTE PAGADO" marker on a reprint. |
| `ticketLastPaymentDate` | Latest paid date (a rectificativa may not precede it). |
| `nextTicketNumber(db, highestAtAeat, &ok)` | Above the stored numeric maximum and `highestAtAeat`; 0 with `*ok` false when unreadable. |
| `nextRectificationNumber(db, highestAtAeat, &ok)` | Next `R-<n>` of the rectificativa series. |
| `nextVerifactuInvoiceSeq(db, n, environment, &ok)` | Highest paid-row seq + 1 (0 with none), also past that environment's `aeat_records` ids (empty environment: all); `ok` false: do not charge. |
| `aeatPendingRecordCount(db, environment)` | Records the direct client generated and AEAT has not confirmed; 0 without `aeat_records`. |
| `aeatProductionStartIso(db)` | Day (ISO) the direct client first ran in production here, or empty. |
| `verifactuRecoveryFloorIso(db, floorIso, directProduction)` | The floor the pending / refused lists apply: the setting, or the production start when later and in production. |
| `verifactuEventRecordXml`, `verifactuEventFor` | One payment event's stored record XML; its own total and original `fecha_pago` (paid rows only), for a resend. |

## Contabilidad listings and quarters

| Function | Purpose |
|----------|---------|
| `incomeTicketsBetweenDates(db, start, end, &ok)` | One entry per paid `n_recibo` in `[start, end)`; an unreadable amount is counted in `invalidAmounts`, not summed. |
| `expensesBetweenDates` | Every `gastos` row in the range; NULL iva is -1; an unreadable amount sets `invalidAmount`. |
| `regularizationsBetweenDates` | Paid ANULADA / RECTIFICADA tickets by `fecha_anulacion` (subtracted in that period; the payment period keeps the income). |
| `annualDetailsByQuarter(db, year, &ok)` | The three listings for a year bucketed by quarter in one scan each (equal to the per-quarter listings). |
| `quarterIsClosed(db, q, y)` / `(db, date)` | An open closure exists; a query that cannot run reads as closed; invalid date false. |
| `closeQuarter`, `reopenQuarter`, `quarterClosures` | Append a closure / reopen the open one (false if already in that state) / the history. |
| `closedQuarterKeys`, `quarterHasMovements`, `openPeriodDate(db, preferred)` | Keys of all closed quarters; anything dated in the quarter; the nearest open date for a late subtraction. |

`accounting_closures` is append-only (triggers) with one open closure per quarter (partial unique index).

## Verifactu state of `ingresos`

| Function | Purpose |
|----------|---------|
| `updateTicketVerifactuFields(db, n, result, seq)` | Writes an AEAT reply onto the paid rows of that event only (`pagado = 'SI'` is load-bearing: unpaid siblings share seq 0). Never rewrites ENVIADA / ANULADA / RECTIFICADA. Rows changed; 0 already settled; -1 failure. |
| `applySettledVerifactuResult(db, invoiceId, result)` | An outcome nobody waited for, applied to the event the id names; an accepted substitution also marks the original RECTIFICADA in an open period. -1: id names no event. |
| `applySettledVerifactuCancellation(db, invoiceId, date, result)` | Late accepted cancellation: 1 marked, 2 already marked, 0 not, -1 no event. |
| `markInvoiceSeqCancelled(db, n, seq, date, cancelXml)` | ANULADA on the event's paid rows with `fecha_anulacion` (existing date kept); rows already ANULADA keep state and date, and an empty cancellation XML is filled. False when no paid row matched. |
| `markTicketRectified(db, n, date)` | RECTIFICADA on the paid rows with `fecha_anulacion`. |
| `markPendingEventUnreconciled(db, n, seq)` | Start-up "Marcar como error": that event's empty / PENDIENTE **paid** rows -> ERROR. |
| `reconcileVerifactuFromAeat(db, n, seq, csv, url)` | Adopts AEAT's CSV / QR: ERROR / PENDIENTE paid rows -> ENVIADA; never touches ENVIADA / ANULADA / RECTIFICADA. Caller confirms identity first. |
| `pendingVerifactuEvents(db, floorIso)` | Start-up recovery feed: paid events still PENDIENTE / empty, earliest `fecha_pago` on or after the floor. |
| `refusedInvoices` / `refusedInvoiceCount(db, floorIso)` | Paid events in ERROR on or after the floor, newest first; the count is the list's size. |
| `submittedInvoiceEvents(db, n, events)` | Anular factura feed: one entry per seq over paid rows with an estado. |
| `aeatExportRecords(db, from, to, records, &undated)` | Export feed: events AEAT holds, issued or cancelled in the range; false on query failure. |

### Rectificativas (`RectificativaRow`)

`insertRectificativaRow` (PENDIENTE; refused in a closed quarter), `resetRefusedRectificativa` (only a row still in ERROR), `applyRectificativaResult` (a refusal never rewrites ENVIADA), `markRectificativaUnanswered` (PENDIENTE -> ERROR), `markRectificationUnsent`, `markRectificationResent`. Queries: `rectifiedTicketOf`, `liveRectificationOf`, `rectifiedDifferences`, `differenceRectificationCount`, `refusedRectificationOf`, `pendingRectificationOf`, `pendingRectificationNumbers`.

## `ingresos` writers of the windows

| Function | Caller and rule |
|----------|-----------------|
| `insertGarmentRow` / `insertGarmentRows` | AddGarment / `saveTicket`; the latter is all-or-none. `verifactuEstado` is the only `verifactu_*` column written (`PENDIENTE` paid, `SIN COBRAR` unpaid). |
| `storeChargedGarments(db, n, amountByHash, date, seq, invoiceId)` | Cobrar: garments stored paid + PENDIENTE, all or none. False when one was paid meanwhile, was voided, the date is in a closed quarter, or the database fails. |
| `updateTicketPickup`, `markTicketPickedUp` | Pickup date + estado (an `Anulado` row is left alone). |
| `updateTicketObservations` | Notes. |
| `updateTicketSizeAndPrice`, `updateGarmentQtyAndImporte`, `updateGarmentServiceAndImporte` | Amount edits, only while the amount is open; a paid row is refused (it changes only via Anular / Rectificar factura). |
| `splitGarmentRow(db, n, hash, nGarm)` | Moves `nGarm` garments to a new row copying every other column (same invoice and estado); proportional cents, original keeps the remainder. Returns the new hash or empty. |
| `voidGarmentRow` | Void in place (Anulado / ANULADA, `fecha_anulacion` today, dates emptied); re-checks it is unsent and exactly one row changed. |
| `updateItemToClient`, `addNewClient`, `insertNewItemToTable` | `clientes` and the list tables (`clientes` / `prendas` / `proveedores` / `servicios`; other tables refused). |
| `insertExpenseInvoice` | A `gastos` row with the next id; false in a closed quarter. |

## Tests

`test_sql_lite` (functions, listings, row state, migrations) and `test_aeat_store`. See [unit_suites.md](../testing/unit_suites.md).
