# Smoke Test — manual pre-release checklist

Run this on the working branch **before** the version-bump commit of a release (step 1 of the
`/release` skill). It is short on purpose: everything that a test can check by itself is in
`ctest` (17 suites). The end-to-end bench drives PayDialog, MainWindow, Anular prendas, Anular
factura, Rectificar, the recovery dialog, the reprint QR rules and Contabilidad generate / lock /
revert against a fake AEAT server; see [e2e.md](e2e.md). The unit suites cover the migrations,
the accounting figures, the estado rules and the AEAT reply parsing.

This checklist covers only what `ctest` cannot see: the release build starting on a real database
schema, the real printer, PDF and screen rendering, menu and button wiring, the Listado editing
lock, the backup, the language switch and the installer.

---

## 0. Safety rules — read before touching anything

**The smoke test never contacts AEAT.** It runs with the Verifactu **service key removed** from the
settings, so the app treats Verifactu as not configured and every submit, cancel, rectify, QR and
query path is skipped locally. Never run it with a key: `TEST_ENDPOINT` and `PROD_ENDPOINT` in
`verifactuconfig.h` are the same IreneSolutions URL, so a smoke ticket sent with the shop's key
lands in the shop's own AEAT records. Its number would also collide with a real ticket the shop
issues later.

*Possible future change:* if IreneSolutions confirms that production uses a different URL from testing, a test key would only ever reach the AEAT test environment and the real-AEAT checks could come back. The conditions, including a fresh ticket-number base per run, are in the "Switch Verifactu to PRODUCTION" row of `docs/progress_tracker.md`. Until then, keep the key removed.

**Never test against the live database.** Work on a copy of a backup, with `ingresos` and `gastos`
emptied (step 1.3), so the only tickets are the seeded ones below.

**Restore your settings afterwards** (step 1.1 backs them up, the Teardown restores them).

---

## 1. Setup

1. **Back up the settings** (they hold the service key, encrypted):
   ```powershell
   Copy-Item "$env:USERPROFILE\.laideal_settings.json" "$env:USERPROFILE\.laideal_settings.pre-smoke.json"
   ```
2. **Copy a backup snapshot** and start a clean log:
   ```powershell
   Copy-Item "<db folder>/backups/laideal_<newest>.db" "$env:USERPROFILE\Desktop\laideal_SMOKE.db" -Force
   Move-Item "$env:USERPROFILE\.laideal.log" "$env:USERPROFILE\.laideal.log.old" -Force -ErrorAction SilentlyContinue
   ```
3. **Empty ingresos and gastos** in the copy ([DB Browser for SQLite](https://sqlitebrowser.org/)
   → Execute SQL → Write Changes). `clientes` and `prendas` stay, so prices and client search work.
   ```sql
   DELETE FROM ingresos;
   DELETE FROM gastos;
   ```
4. **Point the settings at the copy and remove the key.** With the app closed, edit
   `%USERPROFILE%\.laideal_settings.json` in a text editor:
   - `"database"` → `"path"`: the full path of `laideal_SMOKE.db` (forward slashes).
   - `"verifactu"` → `"service_key"`: `""` (empty string).
5. **Safety check: first launch.** Start the new build. It **must** show *"No se pudo inicializar
   Verifactu … Clave de servicio no configurada"*. **If that warning does not appear, close the app
   and stop**: the key is still set. Accept it; Configuración must show the smoke database
   path. This first launch also runs the migrations on the copy's schema (Block A). Close the app.
6. **Seed the smoke tickets** (after the first launch, so the schema already has every column).
   Ticket numbers are plain integers; since `ingresos` is empty, the app's next ticket is 6.
   The block starts by emptying both tables, so running it twice cannot duplicate a row (the
   tables have no key that would reject one). Afterwards `SELECT COUNT(*) FROM ingresos` is 6 and
   `SELECT COUNT(*) FROM gastos` is 1.
   ```sql
   DELETE FROM ingresos;
   DELETE FROM gastos;
   -- 1: unpaid, two garments -> partial payment through Recogida, Recogida total
   INSERT INTO ingresos (n_recibo,cliente,fecha_recepcion,fecha_pago,fecha_recogida,importe,pagado,estado,cantidad,prenda,size,servicio,observaciones,edit_lock,hash,verifactu_estado,verifactu_invoice_seq)
   VALUES ('1','SMOKE','15-09-2026','','','12.00','NO','En tienda','1','Camisa','','Limp.','','0','smokehash000001','SIN COBRAR',0);
   INSERT INTO ingresos (n_recibo,cliente,fecha_recepcion,fecha_pago,fecha_recogida,importe,pagado,estado,cantidad,prenda,size,servicio,observaciones,edit_lock,hash,verifactu_estado,verifactu_invoice_seq)
   VALUES ('1','SMOKE','15-09-2026','','','8.00','NO','En tienda','1','Pantalon','','Plan.','','0','smokehash000002','SIN COBRAR',0);
   -- 2: unpaid -> Anular prendas
   INSERT INTO ingresos (n_recibo,cliente,fecha_recepcion,fecha_pago,fecha_recogida,importe,pagado,estado,cantidad,prenda,size,servicio,observaciones,edit_lock,hash,verifactu_estado,verifactu_invoice_seq)
   VALUES ('2','SMOKE','15-09-2026','','','10.00','NO','En tienda','1','Falda','','Limp.','','0','smokehash000003','SIN COBRAR',0);
   -- 3: paid, left PENDIENTE -> recovery dialog
   INSERT INTO ingresos (n_recibo,cliente,fecha_recepcion,fecha_pago,fecha_recogida,importe,pagado,estado,cantidad,prenda,size,servicio,observaciones,edit_lock,hash,verifactu_estado,verifactu_invoice_seq)
   VALUES ('3','SMOKE','15-09-2026','15-09-2026','','30.00','SI','En tienda','1','Abrigo','','Limp.','','0','smokehash000004','PENDIENTE',0);
   -- 4: paid and sent (fake CSV) -> reprint layout, Q3 income
   INSERT INTO ingresos (n_recibo,cliente,fecha_recepcion,fecha_pago,fecha_recogida,importe,pagado,estado,cantidad,prenda,size,servicio,observaciones,edit_lock,hash,verifactu_csv,verifactu_estado,verifactu_invoice_seq,verifactu_invoice_id)
   VALUES ('4','SMOKE','16-09-2026','16-09-2026','','24.20','SI','En tienda','1','Jersey','','Limp.','','0','smokehash000005','SMOKE-CSV-4','ENVIADA',0,'4');
   -- 5: paid in Q3, cancelled in Q4 -> regularisation in the Q4 report
   INSERT INTO ingresos (n_recibo,cliente,fecha_recepcion,fecha_pago,fecha_recogida,importe,pagado,estado,cantidad,prenda,size,servicio,observaciones,edit_lock,hash,verifactu_csv,verifactu_estado,verifactu_invoice_seq,verifactu_invoice_id,fecha_anulacion)
   VALUES ('5','SMOKE','15-09-2026','15-09-2026','','12.10','SI','En tienda','1','Vestido','','Limp.','','0','smokehash000006','SMOKE-CSV-5','ANULADA',0,'5','01-10-2026');
   -- One gasto in Q3
   INSERT INTO gastos (id,n_factura,servicio,descripcion,empresa,fecha,iva,importe,edit_lock)
   VALUES (1,'SMOKE-G1','Suministros','Smoke','SMOKE','20-09-2026',21,'60.50',0);
   ```

Verification query used throughout:

```sql
SELECT n_recibo,prenda,pagado,estado,verifactu_estado,fecha_pago,fecha_anulacion
FROM ingresos ORDER BY CAST(n_recibo AS INTEGER), rowid;
```

**Teardown** (after the last block): close the app, then restore the settings and keep the log.
```powershell
Copy-Item "$env:USERPROFILE\.laideal_settings.pre-smoke.json" "$env:USERPROFILE\.laideal_settings.json" -Force
```

---

## Block A — Start-up on a real schema

| # | Step | Expected |
|---|------|----------|
| A1 | First launch (step 1.5) | Starts; the log has the `migrateDatabase` lines and no SQL error |
| A2 | Relaunch after seeding | Starts; no migration line repeats (idempotent); after ~4 s "Envíos Verifactu pendientes" lists ticket **3** only |
| A3 | Log | None of `Submitting invoice`, `Cancelling invoice`, `Querying AEAT`, `Generating QR`, `Server response` anywhere in the run. Paid tickets log `Verifactu not configured` instead |

A3 is the AEAT safety check: repeat it at the end.

## Block B — Ticket screens and wiring

| # | Step | Expected |
|---|------|----------|
| B1 | Recovery dialog: **Posponer**, restart | It appears again (nothing written) |
| B2 | Recovery dialog: **Marcar como error** | Ticket 3 reads `ERROR` (upper-case) in Recogida; its Verifactu dialog shows **Reintentar envío a AEAT** (do not press it) |
| B3 | Recogida → search ticket 1 by number | Importe total shows 20.00 (what is owed, not 0) |
| B4 | Herramientas → Añadir nuevas prendas on ticket 1 (still unpaid), add one garment **unpaid** | The added garment reads `SIN COBRAR`; ticket 1 now has three garments |
| B5 | Recogida → ticket 1 → **pay-all button** → untick everything but the Camisa → **Cobrar** | Only the Camisa is charged; the other two stay `NO` / `SIN COBRAR` |
| B6 | Herramientas → Añadir nuevas prendas on ticket 1 again | Refused: "El recibo Nº 1 ya tiene prendas pagadas (enviado a la AEAT)…" - a ticket with a paid garment has an invoice at AEAT and cannot grow |
| B6b | Herramientas → Anular prendas → ticket 2 → tick → confirm | In Recogida: `Anulado`, **NO in green**, Pago and Recogida empty, Anulación = today. A Pago-date search does not list it; an Anulación-date search does, with Importe total 0 |
| B7 | Columns | Recogida and Listado → ingresos show Recepción · Pago · Recogida · Anulación side by side |
| B8 | Listado / búsqueda | Accent-insensitive client search works; the estado column shows `SIN COBRAR`; PDF export works |
| B9 | Herramientas → Anular factura / Rectificar | Both show "Verifactu no configurado" (correct in smoke mode; their flows are in `test_e2e_app`) |

## Block C — Printing · *printer connected, printing on*

| # | Step | Expected |
|---|------|----------|
| C1 | Save a new **unpaid** ticket in MainWindow | Recibo prints (client copy + shop copy); next ticket number advances |
| C2 | Save a new **paid** ticket | A recibo with `IMPORTE PAGADO`, no QR (no AEAT in smoke mode) |
| C3 | Imprimir → Factura for ticket 4 | Factura layout correct (header, lines, IVA split, legal text); no QR in smoke mode |

The QR itself is checked by `test_e2e_app` (when it is requested) and `test_ticket_preview` (how it renders).

## Block D — Contabilidad and the quarter lock

| # | Step | Expected |
|---|------|----------|
| D1 | Contabilidad → Trimestral, Q3 2026 (no lock) | PDF opens: ingresos 66.30 € from 3 tickets (3, 4, 5), gastos 60.50 €; IVA and resumen blocks render; the annex tables at the end add up to the summary; the rounding note shows |
| D2 | Contabilidad → Trimestral, Q4 2026 | "Anulaciones / rectificaciones del periodo" shows ticket 5 at −12.10 € with its annex table; Q3 regenerated is unchanged. (Ticket 5 was paid in Q3, so Q3 counted it as income; Q4, where it was cancelled, takes it back. Paid and cancelled in the same quarter, the two lines net to 0.) |
| D3 | Contabilidad → Anual 2026 | PDF renders and ends with the tickets / gastos tables |
| D4 | Lock box | "Bloquear datos" greyed out in Mensual and Anual, enabled in Trimestral, disabled in Revertir contabilidad |
| D5 | Lock Q3 2026, then in Listado → ingresos edit ticket 4 (inside) and ticket 6 (outside) | Inside is blocked, outside is allowed |
| D6 | Revertir contabilidad Q3 2026 | Ticket 4 can be edited again |

D5 is the highest-value item: it is the only manual check left on a financial control.

## Block E — Settings, backup, language, installer

| # | Area | Check |
|---|------|-------|
| E1 | Backup | Herramientas → Hacer copia de seguridad ahora succeeds (the copy goes next to the smoke DB) |
| E2 | IVA | Configuración → General has no "Tipo de IVA" field; tickets and reports use 21 % |
| E3 | Language | Configuración → Idioma English → OK: a Sí/No dialog reads Yes/No and Ayuda → Notas de la versión opens the English notes, no restart. Back to Español: Spanish again |
| E4 | Installer | Run the setup in Spanish and in English: the information page shows the notes in that language with correct accents; on a machine without settings the app starts in the installer's language |

Then the Teardown, and A3 once more on the final log.

---

## The real AEAT round trip

Not checked by hand, since that would register smoke tickets in the shop's AEAT records. The
end-to-end bench covers it against the fake server: the accepted reply and QR request, the late
reply adopted in Recogida, the duplicate → "Consultar en AEAT" comparison dialog
(`test_e2e_verifactu`, `test_e2e_app`). The one real-AEAT check left is a single real ticket right
after the production switch (see that row in `docs/progress_tracker.md`).

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
