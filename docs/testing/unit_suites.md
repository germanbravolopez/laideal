# Unit and integration suites

What each `tests/test_*.cpp` suite covers, grouped by the module it tests. Conventions, patterns and how to run them are in [README.md](README.md); the end-to-end bench is in [e2e.md](e2e.md).

| Suite | Links | Application | Area |
|-------|-------|-------------|------|
| `test_sql_lite` | `sql_lite` | guiless | DB functions, accounting listings, Verifactu row state, migrations |
| `test_contabilidad` | `contabilidad`, `reporthtml` | guiless | Accounting periods, figures, ticket counts, report tables |
| `test_verifactu_models` | `verifactu` | guiless | Invoice / tax-item / config models |
| `test_verifactu_response` | `verifactu` | offscreen | AEAT reply parsing, query records, QR decode |
| `test_appsettings` | `appsettings` | guiless | DPAPI secret, settings getters, language, fixed IVA |
| `test_settingsdialog` | `appsettings` | offscreen | Configuración dialog behaviour |
| `test_mysortfilterproxymodel` | `tableview` | guiless | Search and sort in the grids |
| `test_textcolordelegate` | `tableview` | offscreen | Cell colours, ingresos column placement |
| `test_escpos` | `printing` | guiless | ESC/POS bytes and ticket rendering |
| `test_ticket_preview` | `printing` | GUI / offscreen | Rendered recibo / factura preview |
| `test_facturas` | `facturas` | guiless | IVA split |
| `test_genlistado` | `listado` | guiless | Listado helpers |
| `test_backup_manager` | `backup` | guiless | Backup retention |
| `test_reporthtml` | `reporthtml` | guiless | Report HTML scaffolding |
| `test_versioncompare` | `updater` | guiless | Version comparison |

---

## Database — `test_sql_lite`

Links the `sql_lite` static lib and exercises its free functions against a throwaway SQLite DB in a `QTemporaryDir` (schema created in `initTestCase`, tables cleared in `init()` before each test). Fixtures stay on success paths with dot-decimal amounts so the functions' modal `QMessageBox` error paths never fire under the guiless main.

- **Contabilidad detail listings** (the single source of every report figure): `incomeTicketsBetweenDates` filters (paid, not `ANULADA`/`RECTIFICADA`, legacy blank included, half-open range) and grouping by ticket; `expensesBetweenDates`; NULL IVA kept as `-1`; comma-decimal amounts flagged, never summed; `annualDetailsByQuarter` equal to the four per-quarter listings.
- **Cancellations and regularisations**: `markInvoiceSeqCancelled` / `markTicketRectified` stamp `fecha_anulacion` on the paid rows of the event only (unpaid garments stay chargeable), never overwrite it, and fail when no paid row matches; `regularizationsBetweenDates` (income stays in the payment period, subtracted where cancelled; local voids never appear); `ticketLastPaymentDate`; `quarterIsClosed` (an empty month inside a closed quarter reads closed; gastos-only).
- **Migrations** (`migrateDatabase`): unpaid `PENDIENTE` → `SIN COBRAR`; estado casing normalised; old void dates moved into `fecha_anulacion`; unpaid remainder wrongly marked `ANULADA` by a pre-10.12 cancellation repaired (local voids and `RECTIFICADA` rows left alone).
- **Accounting locks**: `readLockForMonthAndYear`, `readLockForQuarter` (incl. a quarter with income only in its first month).
- **Verifactu row state**: `nextVerifactuInvoiceSeq`, `verifactuInvoiceId` / display id, `updateTicketVerifactuFields` (unpaid siblings untouched, transport failure stays PENDIENTE, AEAT rejection is final), `verifactuEventFor`, `reconcileVerifactuFromAeat` (adopt CSV, refusals), `pendingVerifactuEvents` (excludes unpaid / SIN COBRAR, returns the payment date), `ticketAllGarmentsPaid`, `ticketHasPaidGarment`.
- **Garment writes** behind Recogida de Prendas and MainWindow (all scoped by `(n_recibo, hash)` so siblings stay untouched): `updateTicketPickup` / `Observations` / `SizeAndPrice`, `updateGarmentQtyAndImporte` / `ServiceAndImporte`, `insertGarmentRow` (split-off, `saveTicket` and AddGarment shapes), `markTicketPickedUp` (never revives a void), `garmentImporte`.
- **Voids and totals**: `garmentIsLocallyVoidable`, `voidGarmentRow` (estado `Anulado`, `ANULADA`, `fecha_anulacion` = today, payment / pickup dates empty), `garmentExcludedFromTotals` (the Recogida total rule).
- **Misc**: `genHash16`, `readMaxValueInColumnFromTable`, `readClientPhones`, `removeSpecialChars`.

---

## Accounting — `test_contabilidad`

Links `contabilidad` (and `reporthtml` for amount formatting). All pure statics, no DB.

- **Periods**: `periodRangeFor` — half-open quarter / month ranges incl. the year roll-over; annual uses quarter units.
- **Locks**: `lockOptionAvailable` ("Bloquear datos" only in Trimestral, never while reverting); `combinedLockState` (ingresos + gastos; gastos-only quarter).
- **Figures**: `figuresFromDetails` (income, IVA split at 21 %, gastos by rate, unrecognised / NULL rates counted not summed, regularisations netted out of income).
- **Ticket counts**: `netTicketCount` (paid and cancelled in the same period not counted; partial cancellation still counts; credit notes not counted; an earlier period's cancelled payment does not offset a new sale), `yearTicketCount` (distinct over the year, netted).
- **Report tables**: detail ingresos / gastos / regularisation tables (rows, HTML escaping, base / IVA by rate, flagged comma amounts and odd rates, NULL rate shown as `?`, totals, rounding note, empty period).

---

## Verifactu — `test_verifactu_models`, `test_verifactu_response`

- **`test_verifactu_models`**: `VerifactuConfig` validation and environment URLs; `VerifactuTaxItem` JSON and operation type; `VerifactuInvoice` JSON, totals, validation and rectificativa fields; the `verifactu_estado` string round-trip.
- **`test_verifactu_response`** (offscreen, because `QPixmap` needs a `QGuiApplication`): `parseVerifactuResponse` error and success shapes, `Huella` extraction, base64 → `QPixmap` QR decode; `parseVerifactuQueryResponse` against captured real replies (empty list, a populated record, several stored attempts where the accepted one must win); `verifactuRemoteMatches`; `verifactuErrorIsDuplicate`.

---

## Settings — `test_appsettings`, `test_settingsdialog`

- **`test_appsettings`**: DPAPI helpers (marker, encrypt ↔ decrypt round-trip, legacy plaintext passthrough, empty input); getters through `loadFrom(path)` against a throwaway file (derived report paths); `encryptSecretsAtRest` on-load migration and no re-encryption; fixed IVA (`ivaRate()` = 21 whatever the JSON says); language — `initialLanguage` from the installer choice, `releaseNotesResource`, and a parity check that `releases_notes.txt` and `releases_notes_es.txt` list the same versions in the same order.
- **`test_settingsdialog`** (offscreen): drives the real `SettingsDialog` on a throwaway settings file, never accepted. Service key masked by default; `Mostrar` reveals it without changing the text; a second click hides it; a new dialog starts masked.

---

## Grids — `test_mysortfilterproxymodel`, `test_textcolordelegate`

- **`test_mysortfilterproxymodel`**: `removeDiacritics`, accent-insensitive `filterAcceptsRow`, and the `lessThan` comparators (chronological dates, numeric importe, locale-string fallback).
- **`test_textcolordelegate`** (offscreen): `TextColorDelegate::classify` (Anulado or `ANULADA` green, SI / Recogido green, NO / En tienda red) and `placeIngresosDateColumns` on a real `QTableView` header (Anulación right after the other three dates, idempotent).

---

## Printing — `test_escpos`, `test_ticket_preview`

- **`test_escpos`** (guiless; `QImage` works without a platform plugin): `EscPosBuilder` exact control bytes (init, align, bold, font, size, cut), the PC858 transcode incl. unmapped → `?`, the dots / character-width column math; `TicketRenderer` recibo and factura fragments, the IVA split, and that a QR is rastered.
- **`test_ticket_preview`**: renders a sample recibo and factura through the real `TicketRenderer`, then interprets the ESC/POS bytes back into a **PNG** simulation of the thermal paper and an **ASCII** mock (written next to the test exe), asserting the expected blocks (totals, copy marker, QR on the factura but not the recibo). On Windows it uses the native platform so system fonts render real glyphs; elsewhere offscreen. A committed PNG pair lives in `docs/modules/printer/`.

---

## Other modules

- **`test_facturas`**: the pure IVA split `taxBaseFromGross` / `taxAmountFromGross`.
- **`test_genlistado`**: `GenListado::filenameSuffix` and `shouldPrintGastoRow`.
- **`test_backup_manager`**: `BackupManager::backupsToPrune` (recent kept, one per month, beyond 4 years dropped, non-matching names ignored).
- **`test_reporthtml`**: `ReportHtml::formatEuro`, `tableOpen`, `documentClose`.
- **`test_versioncompare`** (suite `TestUpdater`): `Updater::compareVersions` (incl. 10.10 > 10.9) and `currentVersion`.
