# Smoke Test — manual pre-release checklist

Run this on the working branch **before** the version-bump commit of a release (step 1 of the
`/release` skill). The automated suite (`ctest`, 14 suites) proves the pure and DB-level logic;
this file covers what it structurally cannot: the real network, the real printer, the migration
running against a real database, and Qt signal/slot wiring that has no testable seam.

The 10.9 run of this checklist found **three** bugs the unit tests could not have caught
(see "Findings from the 10.9 run" at the end). Treat it as load-bearing, not ceremony.

---

## 0. Safety rules — read before touching anything

**`TESTING` is not a sandbox.** In `verifactuconfig.h`, `TEST_ENDPOINT` and `PROD_ENDPOINT` are
the **same URL**. The environment setting only switches the AEAT *validation* URL embedded in the
QR (`prewww2` vs `www2`). Whether a submission creates a real, legally-registered AEAT invoice
depends entirely on the **ServiceKey**. Confirm with Irene Solutions that the key is a test key
before running any block marked *registers at AEAT*.

**Never test against the live database.** `migrateDatabase()` rewrites `verifactu_estado` on first
launch. Always work on a copy, and keep a genuine pre-migration snapshot as the undo.

**Prefer a `backups/` snapshot as the source.** `BackupManager` writes dated `VACUUM INTO`
snapshots to `<db folder>/backups/`. The newest one taken *before* the new build ever ran is a
true pre-release baseline; a copy of the live DB made afterwards is not (it is already migrated).

---

## 1. Setup

```powershell
# Genuine pre-migration snapshot -> scratch copy
Copy-Item "<db folder>/backups/laideal_<newest pre-release>.db" `
          "$env:USERPROFILE\Desktop\laideal_SMOKE.db" -Force

# Keep a clean log to read afterwards
Move-Item "$env:USERPROFILE\.laideal.log" "$env:USERPROFILE\.laideal.log.old" -ErrorAction SilentlyContinue
```

Launch the new build, open **Configuración**, point `database.path` at `laideal_SMOKE.db`, and set
printing off unless testing print output.

### Seed rows

Run in [DB Browser for SQLite](https://sqlitebrowser.org/) **before the first launch** of the new
build, so the migration sees the pre-release shapes. Every base column of `ingresos` is
`TEXT NOT NULL` with no default — all 15 must be supplied even when empty.

```sql
-- S1 unpaid, old PENDIENTE shape -> migration must relabel to SIN COBRAR
INSERT INTO ingresos (n_recibo,cliente,fecha_recepcion,fecha_pago,fecha_recogida,importe,pagado,estado,cantidad,prenda,size,servicio,observaciones,edit_lock,hash,verifactu_estado,verifactu_invoice_seq)
VALUES ('S1','SMOKE','15-09-2026','','','20.00','NO','En tienda','1','Camisa','','Limp.','','0','smokehash000001','PENDIENTE',0);
-- S2 paid, genuinely pending -> migration must NOT touch it; also feeds the recovery dialog
INSERT INTO ingresos (n_recibo,cliente,fecha_recepcion,fecha_pago,fecha_recogida,importe,pagado,estado,cantidad,prenda,size,servicio,observaciones,edit_lock,hash,verifactu_estado,verifactu_invoice_seq)
VALUES ('S2','SMOKE','15-09-2026','15-09-2026','','30.00','SI','En tienda','1','Abrigo','','Limp.','','0','smokehash000002','PENDIENTE',0);
-- S3 legacy split row, blank estado -> must STAY blank
INSERT INTO ingresos (n_recibo,cliente,fecha_recepcion,fecha_pago,fecha_recogida,importe,pagado,estado,cantidad,prenda,size,servicio,observaciones,edit_lock,hash,verifactu_estado,verifactu_invoice_seq)
VALUES ('S3','SMOKE','15-09-2026','','','10.00','NO','En tienda','1','Falda','','Plan.','','0','smokehash000003','',0);
-- S4 multi-garment unpaid ticket -> partial-payment / seq-0 test
INSERT INTO ingresos (n_recibo,cliente,fecha_recepcion,fecha_pago,fecha_recogida,importe,pagado,estado,cantidad,prenda,size,servicio,observaciones,edit_lock,hash,verifactu_estado,verifactu_invoice_seq)
VALUES ('S4','SMOKE','15-09-2026','','','40.00','NO','En tienda','1','Camisa','','Limp.','','0','smokehash000004','PENDIENTE',0);
INSERT INTO ingresos (n_recibo,cliente,fecha_recepcion,fecha_pago,fecha_recogida,importe,pagado,estado,cantidad,prenda,size,servicio,observaciones,edit_lock,hash,verifactu_estado,verifactu_invoice_seq)
VALUES ('S4','SMOKE','15-09-2026','','','25.00','NO','En tienda','1','Pantalon','','Plan.','','0','smokehash000005','PENDIENTE',0);
-- S5 paid row already rejected as duplicate -> Consultar en AEAT test
INSERT INTO ingresos (n_recibo,cliente,fecha_recepcion,fecha_pago,fecha_recogida,importe,pagado,estado,cantidad,prenda,size,servicio,observaciones,edit_lock,hash,verifactu_estado,verifactu_error,verifactu_invoice_seq)
VALUES ('S5','SMOKE','15-09-2026','15-09-2026','','12.10','SI','En tienda','1','Jersey','','Limp.','','0','smokehash000006','ERROR','Registro de facturacion duplicado',0);
```

Verification query used throughout:

```sql
SELECT n_recibo,pagado,verifactu_estado,verifactu_csv,verifactu_invoice_seq,verifactu_invoice_id
FROM ingresos WHERE cliente='SMOKE' ORDER BY n_recibo;
```

> The seeded **paid** rows (S2 + S5 = 42.10 EUR, dated 15-09-2026) are counted as income in their
> quarter. Add them to any expected accounting figure taken from the un-seeded snapshot, or the
> report will look wrong when it is not.

---

## Block A — Migration and the estado split · *no AEAT contact*

| # | Step | Expected |
|---|------|----------|
| A1 | Launch the new build on the smoke DB | Starts normally |
| A2 | Verification query | **S1 → `SIN COBRAR`**, **S2 stays `PENDIENTE`**, **S3 stays blank** (empty, not `SIN COBRAR`) |
| A3 | Log | `migrateDatabase: re-labelled N unpaid PENDIENTE rows as SIN COBRAR` |
| A4 | Close and relaunch | No second re-label line — the migration is idempotent |
| A5 | Save a new **unpaid** ticket in MainWindow | Rows read `SIN COBRAR` |
| A6 | Save a new **paid** ticket | Rows read `PENDIENTE`, then `ENVIADA` if AEAT answers |
| A7 | Herramientas → Anular prendas on a `SIN COBRAR` garment | Still selectable and voidable. **If greyed out, stop** — that is the regression the estado split most risks |
| A8 | Herramientas → Añadir nuevas prendas on an unpaid ticket | Added row reads `SIN COBRAR` |

A2's three outcomes are the whole point. S3 staying blank is what protects printed invoices: several
print/cancel queries detect legacy split rows via `verifactu_estado != ''`.

## Block B — Recovery dialog and seq-0 scoping · *no AEAT contact*

| # | Step | Expected |
|---|------|----------|
| B1 | Restart, watch for "Envíos Verifactu pendientes" | Lists only **paid** pending events (S2). Unpaid rows absent — this was the original customer complaint |
| B2 | Press **Posponer**, restart | Reappears (nothing written) |
| B3 | Recogida de Prendas → ticket S4 → **pay-all button** → untick the Pantalon → **Cobrar** | Only the Camisa is charged |
| B3b | Verification query | Camisa paid with a seq. **Pantalon still `pagado=NO`, `SIN COBRAR`, empty CSV, empty invoice_id** |
| B3c | Anular prendas on S4 | The Pantalon is still voidable |

> Partial payment is only reachable through the **pay-all button**, which opens `PayDialog` where
> garments are unticked. The per-row payment button is permanently disabled by design
> (per-garment submission would collide at AEAT), so `updateDb(PAY_YES)` is unreachable.

B3b/B3c are the seq-0 fix: a ticket's first payment event gets seq 0, which the unpaid remainder
also carries, so an unscoped write-back used to stamp it with the AEAT result and make it
un-voidable.

## Block C — Offline behaviour · *no AEAT contact, safe with any key*

Turn off Wi-Fi / unplug the network, or add a Windows Firewall outbound block for `laideal.exe`.

| # | Step | Expected |
|---|------|----------|
| C1 | Offline; pay a garment in Recogida de Prendas | Dialog waits ~5 s, then closes |
| C2 | The printed ticket | A **recibo showing `IMPORTE PAGADO`**, no QR |
| C3 | Verification query | Row is `pagado=SI` and **`PENDIENTE`** — not `ERROR` |
| C4 | Offline; save a **paid** ticket in MainWindow | Waits ~3 s, prints a recibo with `IMPORTE PAGADO`, no QR; row `PENDIENTE` |
| C5 | Restart offline, press **Marcar como error** in the recovery dialog | Estado reads **`ERROR`** (upper-case) |
| C5b | Open that row's Verifactu dialog | **Reintentar envío a AEAT is present** |

## Block D — Late-reply adoption · *registers at AEAT*

AEAT answering between 5 s and 10 s is hard to arrange, so force it: temporarily change
`QTimer::singleShot(5000, …)` to `(150, …)` in `pay_dialog.cpp` and build the debug build. A normal
fast reply then arrives "late" and must be adopted.

| # | Step | Expected |
|---|------|----------|
| D1 | Pay a garment | Dialog closes at once, QR-less recibo printed, row `PENDIENTE` |
| D2 | Wait 1–3 s | Log `RecogPrendas: adopted in-flight submit …`; row flips to **`ENVIADA` with a CSV**; status bar offers the factura with QR |
| D3 | Reprint the factura | Prints with the QR |
| D4 | **Revert the 150 back to 5000** and rebuild | — |

## Block E — Retry and AEAT reconciliation · *registers at AEAT*

To test a duplicate rejection deliberately, make a **registered** invoice look failed locally
(with the app closed):

```sql
UPDATE ingresos SET verifactu_estado='ERROR', verifactu_csv='', verifactu_url_qr='',
       verifactu_error='Prueba E6: forzado a ERROR'
 WHERE n_recibo='<ticket>' AND verifactu_invoice_seq=<seq>;
```

**Do not change `fecha_pago` or `importe`** — the retry rebuilds the AEAT identity from them, so
altering either registers a genuine second invoice instead of being rejected as duplicate.

| # | Step | Expected |
|---|------|----------|
| E1 | Open the Verifactu dialog on that row | Estado `ERROR`; **Reintentar** and **Consultar en AEAT** both offered |
| E2 | Press **Reintentar envío a AEAT** | AEAT rejects as duplicate; the app **auto-queries** and opens the comparison dialog |
| E3 | All three fields match → press **Actualizar con los datos de AEAT** | Row returns to `ENVIADA` with AEAT's CSV; log `reconcileVerifactuFromAeat: reconciled N row(s)` |
| E4 | **Consultar en AEAT** on a ticket AEAT does not hold | "AEAT no ha devuelto ninguna factura…" and the apply button disabled |
| E5 | Press **Ver respuesta completa** | Raw JSON shown (also written to the log) |
| E6 | **Consultar en AEAT** on an `ENVIADA` row | Informational only; apply disabled with the "solo informativa" tooltip |

If the comparison dialog does **not** open automatically at E2, capture the exact AEAT error text:
`verifactuErrorIsDuplicate()` matches on wording, and an unseen phrasing needs adding to it.

## Block F — Regression sweep · *no AEAT contact*

| # | Area | Check |
|---|------|-------|
| F1 | Accounting totals | Compare income per quarter against the pre-migration snapshot — see the script pattern in the 10.9 findings below. Must be identical to the cent |
| F1b | Accounting rendering | Generate one quarterly PDF; figures match, IVA/resumen blocks render |
| F2 | Quarter lock | Lock a quarter, attempt an edit inside it (blocked), one outside it (allowed), then revert the lock |
| F3 | Printing | Reprint a recibo and a factura: QR on the `ENVIADA` factura, none on the recibo |
| F4 | Anular factura Verifactu | Enabled on `ENVIADA`, disabled on `PENDIENTE`/`SIN COBRAR` |
| F5 | Rectificar factura | Form enabled on `ENVIADA`; refused on an unpaid ticket |
| F6 | Listado / búsqueda | Accent-insensitive client search, estado column shows `SIN COBRAR`, PDF export works |
| F7 | Backup | Herramientas → Hacer copia de seguridad ahora succeeds |
| F8 | Estado casing | A row marked as error reads `ERROR` and offers Reintentar |
| F9 | Query on a settled row | Informational; apply disabled |
| F10 | Query on an unpaid row | **No** Consultar en AEAT button |
| F11 | Query on a permanently-rejected row | Reports not found, plainly |

F2 is the highest-value item: it is the only remaining check touching a financial control.

---

## Findings from the 10.9 run

Three bugs found here that the automated suite could not reach, all fixed:

1. **`PendingSubmitsDialog` persisted a literal `'Error'`** instead of the canonical `ERROR`.
   `verifactuEstadoFromString()` compared exactly, so those rows decoded as *un-submitted* and
   silently lost their "Reintentar envío a AEAT" button. Fixed at the writer, normalised in
   `migrateDatabase`, and the reader is now case-insensitive as a net. SQL filters compare
   case-sensitively, so the stored value — not just the C++ read — had to be corrected.
2. **"Consultar en AEAT" was hidden on settled rows**, which blocked the only way to verify what
   AEAT holds for a registered invoice. Now offered on any paid row (the query is read-only); the
   adopt button stays disabled where there is nothing to adopt.
3. **The payment date field was inert.** Nothing ever wrote it back — its only writer was the dead
   `PAY_YES` path — so edits were silently discarded. Made display-only: `fecha_pago` is part of the
   AEAT invoice identity, so editing it after submission would break both retry and reconciliation.

Also confirmed by this run:

- The migration moved **zero money**. Income per quarter was identical to the cent against a genuine
  pre-migration snapshot, while `PENDIENTE 349 → 30` and `SIN COBRAR 0 → 319` (349 = 30 + 319, and
  nothing else moved). The 18 678 legacy NULL-estado rows were untouched, which is exactly why the
  backfill is scoped to the literal `'PENDIENTE'`.
- The `GetFilteredList` query endpoint works and **its `InvoiceID` filter is applied server-side**
  (`Count: 1` for a specific id). This contradicted an earlier design note claiming no lookup
  endpoint existed — see `docs/modules/verifactu/README.md`.
- Pre-2024 tickets sit permanently in `ERROR` ("fecha de años anteriores al 2024"). These are
  legitimate AEAT rejections, not lost replies, and can never be reconciled.
