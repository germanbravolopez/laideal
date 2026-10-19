# Pre-12.0 review — 19-10-2026

A read-only deep review of every `src/` module before release 12.0: 18 module reviewers and one system-integration supervisor (Claude agents), each checking the code, whether the docs match it, unit coverage, and what the e2e bench, the UI smoke test and the self-check should add. Nothing was built or changed during the review.

- [`00_integration.md`](00_integration.md): the supervisor's report: release blockers (§2), architecture and cross-module findings (§3), global docs (§4), test strategy per layer (§5) and the **consolidated prioritised backlog** (§6, P0-P3).
- One report per module: [`add_garment`](add_garment.md), [`app`](app.md), [`appsettings`](appsettings.md), [`backup`](backup.md), [`contabilidad`](contabilidad.md), [`facturas`](facturas.md), [`imprimir`](imprimir.md), [`listado`](listado.md), [`logging`](logging.md), [`printing`](printing.md), [`recog_prendas`](recog_prendas.md), [`reporthtml`](reporthtml.md), [`sql_lite`](sql_lite.md), [`tableview`](tableview.md), [`uikit`](uikit.md), [`updater`](updater.md), [`verifactu-direct`](verifactu-direct.md), [`verifactu-gateway`](verifactu-gateway.md).

The P0 and P1 items are filed in [`docs/progress_tracker.md`](../../progress_tracker.md); P2 / P3 stay in §6 of the integration report until picked up. Line numbers refer to commit `6f9c1dd`.
