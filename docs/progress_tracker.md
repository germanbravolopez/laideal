# Progress Tracker — La Ideal

**Rule**: Track only what is actionable now. The active sections are:
- **Blocking Issues** — must-fix items before merging the current branch and cutting a release. Items move out of here once resolved (into [`completed_milestones.md`](completed_milestones.md)).
- **Open Non-Blocking Issues** — known backlog that does not gate the release but is still worth doing.
- **Backlog (deferred — low value / not currently planned)** — items consciously parked because the effort outweighs the value (or they depend on an external trigger). Not "open work": revisit only if the trade-off changes. Keeps the Open list a real to-do list.

Finished work (and its Archive) lives in [`completed_milestones.md`](completed_milestones.md), not here.

Add new entries at the **top** of the relevant section. Do not keep an "In Progress" list — work in progress lives in the branch and in Blocking Issues if it gates the release.

---

## Current Status — October 2026

**Active branch**: `develop`

**Latest release**: [10.13](https://github.com/germanbravolopez/laideal/releases/tag/10.13) — Verifactu cancellation and export fixes, plus the application-level test bench. **(1)** Anular factura cancels each invoice under the date AEAT registered it with (its payment date, shown per row), retrying an old seq-0 invoice with its reception date if AEAT rejects it, and stores AEAT's cancellation record. **(2)** Exportar registros AEAT writes one record per invoice AEAT holds (literal InvoiceID, payment date, event total; CSV-only invoices included; cancellations as their own record in the period they happened) and reports failures instead of writing an empty file. **(3)** A garment added as paid with Añadir nuevas prendas is submitted to AEAT at once. **(4)** The Contabilidad ticket count nets a cancellation only against a payment of the same period. Also in this cycle: the end-to-end test bench (fake AEAT server; PayDialog, MainWindow, the app dialogs, Recogida, Imprimir, Contabilidad), the AEAT-free smoke test, a dead-code cleanup, and the testing docs / guidelines audit reorganised.


<!-- Keep only the latest release here. Earlier releases are recorded in `completed_milestones.md`; do not accumulate a "Previous release" list under Current Status. -->

**Next**: switch Verifactu to PRODUCTION once IreneSolutions answers the production-access request (emailed 2026-10-09, awaiting reply), and optionally polish the thermal ticket layout (see Open Non-Blocking Issues).

---

## Blocking Issues (must fix before the next release)

> Verifactu legal-compliance gaps below come from the audit in [`docs/modules/verifactu/verifactu-requirements.md`](modules/verifactu/verifactu-requirements.md) (RD 1007/2023 + Orden HAC/1177/2024 + AEAT guidance). Each one corresponds to a numbered requirement in that file.

_(No blocking issues for the next release. The four items reported on 2026-10-07 (Recogida `Importe total`, Contabilidad detail tables, the service-key toggle, and adopting more AI agents/skills) are resolved; see [`completed_milestones.md`](completed_milestones.md).)_

---

## Open Non-Blocking Issues

| Issue | File | Notes |
|-------|------|-------|
| UI style standardisation: MainWindow and Recogida de prendas | `src/app/mainwindow.ui/.cpp`, `src/recog_prendas/recog_prendas.ui/.cpp` (+ its inline Verifactu / AEAT comparison dialogs and the Separar prendas input box) | The rest of the application uses the shared UiKit style (`docs/modules/uikit.md`). These two are the main daily screens and are customised (~35 and ~21 message boxes, `.ui` forms), so they were deferred on purpose. When taken on: one at a time, keep their keyboard flow, move messages to a result panel / status bar, update their e2e scenarios and the smoke test. |
| Switch Verifactu to PRODUCTION environment | `~/.laideal_settings.json`, `SettingsDialog`, possibly `src/verifactu/verifactuconfig.h` | v8.0 ships with TESTING environment. After obtaining the production ServiceKey from IreneSolutions: update `verifactu.environment` and credentials in settings via `SettingsDialog`. **Status (2026-10-09): waiting on IreneSolutions.** The production-access request was emailed to info@irenesolutions.com, with the titular Rocío López Domínguez (autónoma) as the taxpayer and Germán Bravo López as the technical contact. It asks: the steps to enable production, whether the ServiceKey changes, whether the production URL differs, any conditions or cost, and the timeline. **Before switching, check their answer on the URL**: `VerifactuConfig::TEST_ENDPOINT` and `PROD_ENDPOINT` are currently the same hard-coded IreneSolutions URL, so if production uses a different endpoint this needs a code change, not only settings. Then check one real ticket end to end against production (accepted, CSV stored, QR on the printed factura); the smoke test itself never contacts AEAT. **Also confirm with the first real cancellation**: (a) whether IreneSolutions matches a cancellation by InvoiceID only or by InvoiceID + date (the app now sends each invoice's own payment date, and retries an old seq-0 invoice with its reception date on a rejection); (b) whether any old invoices are registered under their reception date (pre-10.9 resubmissions used it) - the shop's pre-10.9 logs of Recogida retries / startup recovery show how many. **If their answer gives production a different URL** (or a separate account / key), then a TESTING key would reach only the AEAT test environment (`prewww2`), never the shop's real records. In that case consider re-enabling real AEAT communication in `docs/testing/smoke_test.md` with a dedicated test key (the Recogida late-reply and comparison-dialog screens are already covered against the fake server by `test_e2e_app`). Before doing so, solve **ticket-number reuse**: the test environment keeps every invoice from earlier smoke runs, so a run that seeds the same numbers again (1, 2, 3...) gets "duplicate" rejections and adopts old records, which looks like a bug. Seed each run from a fresh integer base, e.g. the run date (`2027031501`, `2027031502`...), so the app's next number continues from there and never repeats a previous run. |
---

## Backlog (deferred — low value / not currently planned)

Parked items where the effort outweighs the value as the code stands. Not on the active to-do list — revisit only if the trade-off changes (e.g. the surrounding code gets refactored for another reason, making the seam cheap).

| Issue | File | Why parked |
|-------|------|------------|
| Full app translation (English) | `src/**`, `src/appsettings/applanguage.cpp`, `resources/i18n/`, CMake | Parked 2026-10-09: the shop staff work in Spanish, and installer, release notes and Qt's standard dialogs are already bilingual. Today `en` gives English standard dialogs and notes with Spanish app text. Estimated 4-6 days. **Size**: ~248 `tr()` sites, ~110 raw Spanish literals still unwrapped (55 message boxes, 55 widget texts), ~180 strings in the 5 `.ui` forms; reports and tickets add a few hundred more. **Steps**: (1) `qt_add_translations` + `lupdate` → `laideal_en.ts` → embedded `laideal_en.qm`, loaded next to `qtbase_es.qm` in `AppLanguage` (½ day); (2) wrap the raw literals (1 day); (3) **separate display text from stored values** (1-2 days, the real risk): `pb_payment`'s "SI"/"NO" text is saved into `pagado`, the service combo's "Limp."/"Plan." into `servicio` (and drives the price lookup), and "En tienda" / "Anulado" / the estado values are compared in SQL, so translating them as-is would write English values into the DB and break totals, prices and accounting; each needs a stored value + translated label; (4) translate ~550 strings (1 day); (5) live switch for app text: each open window would need to rebuild its labels on a language change, or accept that only newly opened windows switch (½-1 day); (6) a test that an English session still stores the Spanish values, plus a smoke pass (½ day). **Stay Spanish regardless**: the text sent to AEAT ("Servicios de lavanderia"), the printed factura / recibo legal texts and the Contabilidad reports (Spanish tax documents; translating them is a separate decision, +1-2 days). **Cheaper first step if revisited** (1-1½ days): steps 1 + 3 only, which removes the on-screen-text-as-data fragility (worth it on its own) and leaves translation purely mechanical. Revive only if an English user / deployment is actually needed. |

---
