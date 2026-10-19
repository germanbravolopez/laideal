# Pre-12.0 review — 19-10-2026

A read-only deep review of every `src/` module before release 12.0: 18 module reviewers and one system-integration supervisor (Claude agents), each checking the code, whether the docs match it, unit coverage, and what the e2e bench, the UI smoke test and the self-check should add. Nothing was built or changed during the review.

- [`00_integration.md`](00_integration.md): the supervisor's report: release blockers (§2), architecture and cross-module findings (§3), global docs (§4), test strategy per layer (§5) and the **consolidated prioritised backlog** (§6, P0-P3).
- One report per module: [`add_garment`](add_garment.md), [`app`](app.md), [`appsettings`](appsettings.md), [`backup`](backup.md), [`contabilidad`](contabilidad.md), [`facturas`](facturas.md), [`imprimir`](imprimir.md), [`listado`](listado.md), [`logging`](logging.md), [`printing`](printing.md), [`recog_prendas`](recog_prendas.md), [`reporthtml`](reporthtml.md), [`sql_lite`](sql_lite.md), [`tableview`](tableview.md), [`uikit`](uikit.md), [`updater`](updater.md), [`verifactu-direct`](verifactu-direct.md), [`verifactu-gateway`](verifactu-gateway.md).

The P0 and P1 items are filed in [`docs/progress_tracker.md`](../../progress_tracker.md); P2 / P3 stay in §6 of the integration report until picked up. Line numbers refer to commit `6f9c1dd`.

## Resolution status of the blockers (P0)

Each fix is its own commit. It also marks the item ✅ in §2 / §6 of [`00_integration.md`](00_integration.md) and in the module report, and moves it from the tracker to [`completed_milestones.md`](../../completed_milestones.md).

| # | Blocker (§6 row) | Status |
|---|------------------|--------|
| 1 | Private data in the public repo (repo) | ✅ Fixed 19-10-2026. HEAD cleaned: NIF, full name and the request CSVs removed; the test uses AEAT's test NIF. History kept (owner's decision). The shop's public street address stays as the first-run default. |
| 2 | Shop connection for 12.0 (release) | ✅ Fixed 19-10-2026. The gateway on TEST in 12.0; the direct client for the later production switch. The tracker's Next line, the production row and the Verifactu README now say so. The gateway items (blocker 9) are the 12.0 blockers. |
| 3 | 12.0 release mechanics and gates (release) | ⏳ Deferred to `/release` (owner's decision 19-10-2026). ES / EN release-note drafts are in the tracker row; Req. 8 no longer claims COVERED before the signature. The gates run after the blocker fixes. |
| 4 | Anular / Rectificar act on the re-searched ticket; Rectificar success without a row (app A1-A3) | ✅ Fixed 19-10-2026. Both dialogs lock the search while a request is pending and apply the reply to a snapshot taken at click time. Rectificar sends nothing when its row cannot be stored, and an UPDATE that finds no row is a failure. 3 e2e tests. |
| 5 | Closing Cobrar / Anular / Rectificar while AEAT answers (recog_prendas #1, app A7) | ✅ Fixed 19-10-2026. `done()` is overridden in PayDialog, CancelInvoiceDialog and RectifyInvoiceDialog and refuses Esc / X / Cerrar while a request is pending, with a notice. Each dialog still closes itself on the reply (Cobrar also after its bounded wait). e2e `test_dialogsCannotCloseWhileAeatAnswers`. |
| 6 | No single-instance guard (app A4) | ✅ Fixed 19-10-2026. `InstanceLock` (`src/app/instancelock.*`): a `QLockFile` keyed on the database path in the user's temp folder, not next to the OneDrive-synced database. A second copy waits 3 s, then says the app is already open and exits; a crashed copy's lock is taken over; `--self-check` is exempt. Unit suite `test_instancelock`. |
| 7 | Failed DB read: zero report closes a quarter; numbering restarts at 1 (sql_lite F1, F10) | Open |
| 8 | Recogida reprint without a row; blank recibo (imprimir F1, F2) | Open |
| 9 | Gateway: no timeout; success without CSV; non-JSON as refusal (verifactu-gateway) | Open |
| 10 | Direct client: installation number / chain marker not saved; lost cancellation reply (appsettings #1, verifactu-direct) | Open |
