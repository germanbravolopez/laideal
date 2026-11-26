# Unit and integration suites

One entry per CTest suite, grouped by module: the CTest name, the test file, the library under test and what it covers. The test files hold the exact assertions. Conventions, seams and timeouts are in [README.md](README.md); the suites that drive whole windows against a fake server (`e2e_verifactu`, `e2e_app`, `e2e_aeat_direct`) are in [e2e.md](e2e.md).

"Offscreen" means the suite sets `QT_QPA_PLATFORM=offscreen` (it needs a widget or `QPixmap`); the others are guiless. Every suite is registered in `tests/CMakeLists.txt` with a 120 s timeout, except the three long ones (`sql_lite`, `aeat_direct_backend`, `e2e_app`): their limits are in [README.md](README.md#running-the-tests).

---

## Database and accounting

| CTest | File | Library | Covers |
|-------|------|---------|--------|
| `sql_lite` | `test_sql_lite.cpp` | `sql_lite` | The DB functions on a throwaway SQLite file (fixtures on success paths, dot-decimal amounts, so no modal error box fires). Groups: the Contabilidad detail listings every report figure comes from; cancellations and regularisations; `migrateDatabase` step by step on old-shape data; the `accounting_closures` table, triggers and row lock; the Verifactu row state (invoice seq, estado updates, reconcile, pending events, AEAT export records); garment writes (pickup, size and price in cents, split, insert, void) |
| `contabilidad` | `test_contabilidad.cpp` | `contabilidad`, `reporthtml` | Period ranges (year roll-over), the lock options and status messages, income / IVA / gastos figures, net ticket counts (cancellations and credit notes), the summary and detail report tables |
| `genlistado` | `test_genlistado.cpp` | `listado` | Listado helpers: filename suffix, which gasto rows print, the IVA split of a gasto |
| `facturas` | `test_facturas.cpp` | `facturas` | The supplier-invoice IVA split, `parseGross` refusals, the dialog's save validation, duplicates and failures on a throwaway DB |
| `add_garment` | `test_add_garment.cpp` | `add_garment` | The Añadir prendas window: validation order, closed quarters, date limits, m2 size rules, unpaid and paid row shape, invoicing blocked, the invoice seq of a paid garment; Recogida's AEAT reconcile summary (`aeatReconcileSummaryHtml`) |
| `reporthtml` | `test_reporthtml.cpp` | `reporthtml` | Euro formatting and the report HTML scaffolding |

## Verifactu and the direct AEAT client

| CTest | File | Library | Covers |
|-------|------|---------|--------|
| `verifactu_models` | `test_verifactu_models.cpp` | `verifactu` | Config validation and environment URLs, tax-item and invoice JSON and totals, the estado string round trip |
| `verifactu_response` (offscreen) | `test_verifactu_response.cpp` | `verifactu` | Gateway reply parsing (error and success shapes, huella, QR decode), query replies from captured real answers, matching a remote record, duplicate detection |
| `aeat_hash` | `test_aeat_hash.cpp` | `verifactu` | The huella and QR URL against the official AEAT vectors; value trimming, two-decimal amounts, timestamps with a `+hh:mm` offset |
| `aeat_chaincheck` | `test_aeat_chaincheck.cpp` | `verifactu` | `AeatChainCheck`: recomputed huellas, links per issuer, a chain restart, a restored copy's continuation |
| `aeat_record` | `test_aeat_record.cpp` | `verifactu` | Element order and values of the alta, anulación and SOAP envelopes; writes the samples `aeat_xsd` validates |
| `aeat_xsd` | `tests/aeat/validate_aeat_xml.py` | Python + lxml | The samples and the reply fixtures against AEAT's own XSDs (`tests/fixtures/aeat-xsd`); skipped without lxml |
| `aeat_response` | `test_aeat_response.cpp` | `verifactu` | AEAT replies and faults and their mapping, query results, record summaries, the chain tip, a lost cancellation, picking the record of an invoice number |
| `aeat_store` | `test_aeat_store.cpp` | `verifactu`, `sql_lite` | The chain head and record outbox on a throwaway DB: installation number, environments kept apart, the `reached` state machine, older tables completed, a chain restart |
| `aeat_qr` (offscreen) | `test_aeat_qr.cpp` | `verifactu` | The locally drawn QR: size, quiet zone, finder patterns |
| `aeat_direct_backend` (offscreen) | `test_aeat_direct_backend.cpp` | `verifactu`, `sql_lite` | The direct backend against the fake AEAT over plain HTTP, with throwaway certificates (`tests/fixtures/aeat-cert`): submit, wait time and batching, rejection and resend, incident flags, the outbox, queries, the first sync and restoring from a backup, a cancelled invoice AEAT returns with or without its cancellation record, certificate loading and expiry, self-test and connection probe |

## Settings, app shell and updater

| CTest | File | Library | Covers |
|-------|------|---------|--------|
| `appsettings` | `test_appsettings.cpp` | `appsettings` | DPAPI secret helpers and the on-load migration, getters through `loadFrom`, the fixed IVA, language and release-notes parity |
| `settingsdialog` (offscreen) | `test_settingsdialog.cpp` | `settingsdialog` | The real Configuración dialog: only the chosen connection's rows show, *Probar conexión* and *Ensayo en el entorno de pruebas de la AEAT*, the masked service key, production confirmations |
| `uikit` (offscreen) | `test_uikit.cpp` | `uikit` | `AmountSpinBox` input and display, the Enter rules of `setUpDialog` and `onEnter` |
| `instancelock` (offscreen) | `test_instancelock.cpp` | `laideal_app` | One copy per database: the lock path ignores case and separators, a second lock is refused, a crashed copy's lock is taken over |
| `selfcheck` (offscreen) | `test_selfcheck.cpp` | `laideal_app` | The post-update self-check: each check on good and bad data, the settings copy, the report, and one whole run leaving the live DB and settings byte for byte unchanged with no shop data in the report ([smoke_test.md](smoke_test.md)) |
| `version_compare` | `test_updater.cpp` (executable `test_versioncompare`) | `updater` | Version comparison, release parsing, the update check and installer download against a loopback fake (hash mismatch, truncated, 404, cancel) |
| `updater_dialog` (offscreen) | `test_versiondialog.cpp` | `updater`, `uikit` | `UpdaterDialog`: the error of a failed download is hidden when the download is retried |
| `backup_manager` | `test_backup_manager.cpp` | `backup` | Copies (renamed once verified, second folder, stale `.part`, concurrent run), retention rules, the failure warning |
| `applogger` | `test_applogger.cpp` | `logging` | Log rotation in a session and at start, a failed rename, an unopenable file (through `AppLogger::installAt`) |

## Grids, printing and listings

| CTest | File | Library | Covers |
|-------|------|---------|--------|
| `mysortfilterproxymodel` | `test_mysortfilterproxymodel.cpp` | `tableview` | Accent-insensitive search; date, numeric and string ordering |
| `textcolordelegate` (offscreen) | `test_textcolordelegate.cpp` | `tableview` | Cell colours by estado, the date-column placement, the decimal-comma delegates |
| `escpos` | `test_escpos.cpp` | `printing` | ESC/POS bytes and the PC858 transcoding, column maths, recibo and factura rendering, the QR raster |
| `ticket_preview` | `test_ticket_preview.cpp` | `printing` | Renders a recibo and a factura, reads the bytes back into a PNG and an ASCII mock and asserts their blocks. Native platform on Windows (real fonts), offscreen elsewhere. The reader is `tests/support/escpospreview.h`, shared with the `escpos_preview` tool |
| `imprimir` (offscreen) | `test_imprimir.cpp` | `imprimir`, `printing` | The Imprimir dialog through `ThermalPrinter::setTransport`: bytes sent to the queue, offline and disabled printing, two copies, factura dates, completa with DNI and address, QR handling, decimal comma, 58 mm width |

---

That is 30 suites here and 3 in [e2e.md](e2e.md): the 33 `add_test` entries of `tests/CMakeLists.txt`.
