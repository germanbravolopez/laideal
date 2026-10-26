# Unit and integration suites

What each `tests/test_*.cpp` suite covers, grouped by the module it tests. Conventions, patterns and how to run them are in [README.md](README.md); the end-to-end bench is in [e2e.md](e2e.md).

| Suite | Links | Application | Area |
|-------|-------|-------------|------|
| `test_sql_lite` | `sql_lite` | guiless | DB functions, accounting listings, Verifactu row state, migrations |
| `test_contabilidad` | `contabilidad`, `reporthtml` | guiless | Accounting periods, figures, ticket counts, report tables |
| `test_verifactu_models` | `verifactu` | guiless | Invoice / tax-item / config models |
| `test_verifactu_response` | `verifactu` | offscreen | AEAT reply parsing, query records, QR decode |
| `test_aeat_hash` | `verifactu` | - | Direct-client huella and QR URL against the official AEAT vectors |
| `test_aeat_chaincheck` | `verifactu` | - | `AeatChainCheck`: recomputed huellas, links per issuer, a restart, a restored copy's continuation |
| `test_aeat_record` | `verifactu` | - | Records and SOAP envelopes; writes samples for `aeat_xsd` |
| `aeat_xsd` | Python + lxml | - | The samples and reply fixtures against the official AEAT XSDs (skipped without lxml) |
| `test_aeat_response` | `verifactu` | - | AEAT replies / faults and their mapping; record summary; chain tip |
| `test_aeat_store` | `verifactu` | - | Chain head and record outbox on a throwaway SQLite DB |
| `test_aeat_qr` | `verifactu` | offscreen | The locally drawn QR (structure; a sample PNG) |
| `test_aeat_direct_backend` | `verifactu` | offscreen | The direct backend against the fake AEAT SOAP service; certificates; self-test |
| `test_appsettings` | `appsettings` | guiless | DPAPI secret, settings getters, language, fixed IVA |
| `test_settingsdialog` | `appsettings` | offscreen | Configuración dialog behaviour |
| `test_mysortfilterproxymodel` | `tableview` | guiless | Search and sort in the grids |
| `test_textcolordelegate` | `tableview` | offscreen | Cell colours, ingresos column placement |
| `test_escpos` | `printing` | guiless | ESC/POS bytes and ticket rendering |
| `test_ticket_preview` | `printing` | GUI / offscreen | Rendered recibo / factura preview |
| `test_facturas` | `facturas` | guiless | IVA split |
| `test_genlistado` | `listado` | guiless | Listado helpers, `splitGasto` (IVA for any typed rate, sums in cents) |
| `test_add_garment` | `laideal_app` | offscreen | Añadir prendas: validation order, incomplete form, closed quarter on the payment and the reception date, m2 without size, comma size, unpaid / paid row shape, invoicing blocked, the paid garment's invoice seq (0, or past a number already used at AEAT) |
| `test_backup_manager` | `backup` | guiless | Backup retention |
| `test_reporthtml` | `reporthtml` | guiless | Report HTML scaffolding |
| `test_uikit` | `uikit` | offscreen | `AmountSpinBox` (shows "12,50", takes "7.25" and "7,25"); Enter rules: `setUpDialog` presses the enabled primary button (nothing without one, no repeat), `onEnter` runs its own action (once, only for acceptable input) |
| `test_instancelock` | `laideal_app` | offscreen | One copy per database: the lock path ignores case and separators, a second lock on the same database is refused until the first is released, another database is free, and a crashed copy's lock is taken over |
| `test_selfcheck` | `laideal_app` | offscreen | The post-update self-check (checks M1a settings, M1b backup age, M1 / M1d migrations once and twice, M1e money counts, M1c chain, M2 data, M3 print, M5 installer): newest backup, the settings copy (database copy, direct client in pre-production, no gateway key), M1 passes and fails on a failed migration, M2's facts (ticket, invoice to print, closed quarter's figures, filed PDF), the report; one whole run of the window leaves the live DB and settings byte for byte unchanged, deletes the copies and reports no shop data |
| `test_versioncompare` | `updater` | guiless | Version comparison |

---

## Database — `test_sql_lite`

Links the `sql_lite` static lib and exercises its free functions against a throwaway SQLite DB in a `QTemporaryDir` (schema created in `initTestCase`, tables cleared in `init()` before each test). Fixtures stay on success paths with dot-decimal amounts so the functions' modal `QMessageBox` error paths never fire under the guiless main.

- **Contabilidad detail listings** (the single source of every report figure): `incomeTicketsBetweenDates` filters (paid, not `ANULADA`/`RECTIFICADA`, legacy blank included, half-open range) and grouping by ticket; `expensesBetweenDates`; NULL IVA kept as `-1`; comma-decimal amounts flagged, never summed; `annualDetailsByQuarter` equal to the four per-quarter listings.
- **Cancellations and regularisations**: `markInvoiceSeqCancelled` / `markTicketRectified` stamp `fecha_anulacion` on the paid rows of the event only (unpaid garments stay chargeable), never overwrite it, and fail when no paid row matches; `regularizationsBetweenDates` (income stays in the payment period, subtracted where cancelled; local voids never appear); `ticketLastPaymentDate`; `quarterIsClosed` (an empty month inside a closed quarter reads closed; gastos-only).
- **Migrations** (`migrateDatabase`): unpaid `PENDIENTE` → `SIN COBRAR`; estado casing normalised; old void dates moved into `fecha_anulacion`; unpaid remainder wrongly marked `ANULADA` by a pre-10.12 cancellation repaired (local voids and `RECTIFICADA` rows left alone); garments split off a paid row relinked to their `ENVIADA` invoice (legacy payments, cancelled and ambiguous matches left alone); open three/four-decimal amounts rounded to cents, paid ones left as stored; rows of one ticket sharing a hash, or with an empty one, get a new one (the row kept is one with a CSV; a second row AEAT holds is only logged; the same hash on two tickets left alone; idempotent); the decimal comma replaced in `gastos` (not in a closed quarter, not when a dot is there too) and `prendas`, never in `ingresos`. The suite's schema itself is built by `migrateDatabase` (`tests/support/testschema.h`) and checked against `INGRESOS_COL_*`.
- **Accounting closures** (`accounting_closures`): the one-time migration carries over exactly the quarters the old `edit_lock` flags closed (blank / garbage dates and `'0'` / NULL flags ignored), runs once, and never re-closes a quarter reopened later; `closeQuarter` refuses a second close and `reopenQuarter` an open quarter; `quarterClosures` keeps the close / reopen history in order with the report files; the triggers refuse a `DELETE`, a rewrite and a second reopen, the unique index a second open closure, the `CHECK` a quarter 5; `quarterIsClosed` by date (year roll-over, invalid date) and `quarterKey`; `quarterHasMovements` (payment, gasto or cancellation); the row lock follows the closed quarter of `fecha_pago` (`updateTicketSizeAndPrice`, `splitGarmentRow`), and a stale `edit_lock` value is ignored.
- **Verifactu row state**: `nextVerifactuInvoiceSeq`, `verifactuInvoiceId` / display id, `updateTicketVerifactuFields` (unpaid siblings untouched, transport failure stays PENDIENTE, AEAT rejection is final, settled rows never rewritten and the call returns 0), `verifactuEventFor` (earliest payment date by date, not text), `reconcileVerifactuFromAeat` (adopt CSV, refusals), `pendingVerifactuEvents` (excludes unpaid / SIN COBRAR, returns the payment date), `aeatExportRecords` (one record per payment event with its total and literal id; CSV-only invoices, cancellations in the period and the rectificativa link included; undated invoices counted), `submittedInvoiceEvents` (one per seq over paid rows, earliest date by date), `ticketAllGarmentsPaid`, `ticketHasPaidGarment`.
- **Garment writes** behind Recogida de Prendas and MainWindow (all scoped by `(n_recibo, hash)` so siblings stay untouched): `updateTicketPickup` / `Observations` / `SizeAndPrice`, `updateGarmentQtyAndImporte` / `ServiceAndImporte` (stored in cents; a paid, pending, legacy-paid, voided or locked row refused), `splitGarmentRow` (a paid row's split stays in its invoice, split-off part in cents and the exact remainder kept - an older 22.515 gives 7.51 + 15.005 -, refuses a split leaving no garment or a locked row), `insertGarmentRow` (`saveTicket` and AddGarment shapes; the all-or-none `insertGarmentRows` is covered end to end by `test_mainWindow_failedInsertStopsTheSave`), `markTicketPickedUp` (never revives a void), `voidGarmentRow` (refuses a paid, sent, voided or locked garment in SQL), `garmentImporte` (rounded to cents, also from a float list price), `garmentUnmeasured` (m2 name with an empty / 0 size), `moneyText` / `roundToCents` (half away from zero despite binary error).
- **Voids and totals**: `garmentIsLocallyVoidable`, `voidGarmentRow` (estado `Anulado`, `ANULADA`, `fecha_anulacion` = today, payment / pickup dates empty), `garmentExcludedFromTotals` (the Recogida total rule).
- **Misc**: `genHash16`, `readMaxValueInColumnFromTable`, `readClientPhones`, `removeSpecialChars`.

---

## Accounting — `test_contabilidad`

Links `contabilidad` (and `reporthtml` for amount formatting). All pure statics, no DB.

- **Periods**: `periodRangeFor` — half-open quarter / month ranges incl. the year roll-over; annual uses quarter units.
- **Locks**: `lockOptionAvailable` ("Bloquear el trimestre" only in Trimestral, never while reverting); `lockStatusMessage` follows the closure history (closed with its date, reverted with its date, a migrated closure without a date).
- **Figures**: `figuresFromDetails` (income, IVA split at 21 %, gastos by rate, unrecognised / NULL rates counted not summed, regularisations netted out of income).
- **Ticket counts**: `netTicketCount` (paid and cancelled in the same period not counted; partial cancellation still counts; credit notes not counted; an earlier period's cancelled payment does not offset a new sale), `yearTicketCount` (distinct over the year, netted).
- **Report tables**: the gastos summary (columns IVA 21 % / IVA 10 % / Subtotal con IVA / Sin IVA / Total, each row adding up), `reportRelativePath` (summary and `_detalle` files per mode), detail ingresos / gastos / regularisation tables (rows, HTML escaping, base / IVA by rate, flagged comma amounts and odd rates, NULL rate shown as `?`, totals, rounding note, empty period).

---

## Verifactu — `test_verifactu_models`, `test_verifactu_response`, `test_aeat_hash`

- **`test_verifactu_models`**: `VerifactuConfig` validation, environment URLs and the legacy INI removed only by `removeLegacyIni()` (never by the constructor); `VerifactuTaxItem` JSON and operation type; `VerifactuInvoice` JSON, totals, validation and rectificativa fields; the `verifactu_estado` string round-trip.
- **`test_verifactu_response`** (offscreen, because `QPixmap` needs a `QGuiApplication`): `parseVerifactuResponse` error and success shapes, `Huella` extraction, base64 → `QPixmap` QR decode; `parseVerifactuQueryResponse` against captured real replies (empty list, a populated record, several stored attempts where the accepted one must win, a numeric `InvoiceID`); `verifactuRemoteMatches`; `verifactuErrorIsDuplicate`.
- **`test_aeat_hash`** (pure; the direct AEAT client, the connection 12.0 offers next to the gateway): `AeatHash` against the three official huella vectors (first alta with an empty `Huella=`, chained alta, chained anulación) and the official QR URLs; value trimming, two-decimal amounts, the timestamp always with its `+hh:mm` offset (never `Z`), percent-encoded QR values. Each rule fails the suite when broken.
- **`test_aeat_record`** / **`aeat_xsd`**: element order and values of `RegistroAlta` (F2, R5 S / I, after a rejection), `RegistroAnulacion`, the envelopes (escaping, query paging); every sample and the reply fixtures (`tests/fixtures/aeat-responses`) validated against AEAT's own XSDs (`tests/fixtures/aeat-xsd`, unchanged).
- **`test_aeat_response`**: accepted, accepted with errors, partly correct, duplicate of an accepted / cancelled record, cancellation, SOAP fault, garbage; query found / cancelled / empty; `summarizeRecord`; `chainTip` (links first, whatever the list order; time as tie-break); `remoteRecordFor` with several records of one number (the asked date wins).
- **`test_aeat_store`**: chain progression, failed build stores nothing, environments apart, moving the head and the sync flag, an unreadable chain refused, outcomes / pending / latest, null texts, an older `aeat_chain` / `aeat_records` table completed (the `reached` column), the `reached` state machine, connection state kept.
- **`test_aeat_qr`**: size, quiet zone and finder patterns of the QR of a verification URL (a sample was decoded back with zxing-cpp).
- **`test_aeat_direct_backend`** (fake AEAT over plain HTTP, throwaway certificates in `tests/fixtures/aeat-cert`): accepted submit, wait time and batching, lost reply resent unchanged, rejection then a flagged new record, already accepted answered locally, cancellation, server / client faults, outbox sent at the next start, query (and an unconfigured backend's query saying why), a lost cancellation reply resent and accepted, a client fault on a batch isolating the bad record, a duplicate with another amount not adopted, waiters answered when the backend is released, chain of our own records only (other systems' records ignored), first sync from AEAT (reads every month and the query pages, fails on a record without its huella or its system block, restore from a backup, chain marker outside the database), self-test, R5 mapping, certificate unusable (no record before the first sync; afterwards generated and kept pending), requests held until the first sync, duplicate of another system's record / of a cancelled invoice, automatic retry and `recordSettled`, certificate loaded from a .pfx via the Windows crypto API (wrong password, expired, unknown thumbprint) and its expiry warning.

---

## Settings — `test_appsettings`, `test_settingsdialog`

- **`test_appsettings`**: DPAPI helpers (marker, encrypt ↔ decrypt round-trip, legacy plaintext passthrough, empty input); getters through `loadFrom(path)` against a throwaway file (derived report paths); `encryptSecretsAtRest` on-load migration and no re-encryption; fixed IVA (`ivaRate()` = 21 whatever the JSON says); language — `initialLanguage` from the installer choice, `releaseNotesResource`, and a parity check that `releases_notes.txt` and `releases_notes_es.txt` list the same versions in the same order.
- **`test_settingsdialog`** (offscreen): drives the real `SettingsDialog` on a throwaway settings file, never accepted. Service key masked by default; `Mostrar` reveals it without changing the text; a second click hides it; a new dialog starts masked.

---

## Grids — `test_mysortfilterproxymodel`, `test_textcolordelegate`

- **`test_mysortfilterproxymodel`**: `removeDiacritics`, accent-insensitive `filterAcceptsRow`, and the `lessThan` comparators (chronological dates, numeric importe, locale-string fallback).
- **`test_textcolordelegate`** (offscreen): `TextColorDelegate::classify` (Anulado or `ANULADA` green, SI / Recogido green, NO / En tienda red), `placeIngresosDateColumns` on a real `QTableView` header (Anulación right after the other three dates, idempotent), `NumberFormatDelegate` storing an edit with a dot (`storedNumber`, `setModelData`), and both decimal delegates showing the comma under an English locale (an empty amount stays empty, the editor opens on "4,5").

---

## Printing — `test_escpos`, `test_ticket_preview`

- **`test_escpos`** (guiless; `QImage` works without a platform plugin): `EscPosBuilder` exact control bytes (init, align, bold, font, size, cut), the PC858 transcode incl. unmapped → `?`, the dots / character-width column math; `TicketRenderer` recibo and factura fragments, the IVA split, and that a QR is rastered.
- **`test_ticket_preview`**: renders a sample recibo and factura through the real `TicketRenderer`, then interprets the ESC/POS bytes back into a **PNG** simulation of the thermal paper and an **ASCII** mock (written next to the test exe), asserting the expected blocks (totals, copy marker, QR on the factura but not the recibo). The ESC/POS reader lives in `tests/support/escpospreview.h`, shared with the `escpos_preview` tool that decodes the UI smoke test's captured print jobs. On Windows it uses the native platform so system fonts render real glyphs; elsewhere offscreen. A committed PNG pair lives in `docs/modules/printer/`.

---

## Other modules

- **`test_facturas`**: the pure IVA split `taxBaseFromGross` / `taxAmountFromGross`.
- **`test_genlistado`**: `GenListado::filenameSuffix` and `shouldPrintGastoRow`.
- **`test_backup_manager`**: `BackupManager::backupsToPrune` (recent kept, one per month, beyond 4 years dropped, non-matching names ignored).
- **`test_reporthtml`**: `ReportHtml::formatEuro`, `tableOpen`, `documentClose`.
- **`test_versioncompare`** (suite `TestUpdater`): `Updater::compareVersions` (incl. 10.10 > 10.9) and `currentVersion`.
