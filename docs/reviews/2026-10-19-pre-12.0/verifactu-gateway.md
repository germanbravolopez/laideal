# Review: verifactu-gateway (src/verifactu gateway client + integration layer)

## Summary

Overall the module is in fair shape. The facade and estado mapping (`verifactuEstadoForResult`) are sound, the ServiceKey is redacted in logs, and both the unknown-outcome rule and the reconcile safeguards are well tested. The weak spots are robustness gaps in the gateway client:
- no transfer timeout on any async call, although the docs claim 10 s;
- a `ResultCode 0` reply with no CSV is mapped to SUCCESS, so the row becomes ENVIADA without a CSV;
- a non-JSON body counts as a definitive AEAT rejection.

Docs drift is heavy (`rest_api.md` describes a much older API surface, and `verifactumanager.h` / `processResponse()` / QSettings INI are cited in five docs). Some dead code remains in the tax-item / invoice-type enums. No Critical findings.

## Findings

| Severity | Category | file:line | Finding | Proposal | Effort |
|---|---|---|---|---|---|
| High ✅ FIXED 19-10-2026 | bug | src/verifactu/verifactumanager.cpp:322-328 (createNetworkRequest), :64-97, :99-154, :156-187, :189-234 | No `setTransferTimeout` on submit / cancel / query / QR. Qt 6.4's default is 0 (no timeout), so a stalled TCP connection never emits `finished`. Effects: the `m_pendingSubmits` entries in MainWindow and RecogPrendas are never resolved, and in RecogPrendas::queryAeatAndOfferReconcile (recog_prendas.cpp:1085-1094) the heap `QMetaObject::Connection` stays alive forever. The comments in pay_dialog.cpp:309 and mainwindow.cpp:1172 and README:113 all rely on a "10 s transport timeout" that only `testConnection` (:269) sets | Set `request.setTransferTimeout(30000)` (or 10000 to match the docs) in `createNetworkRequest`. The timeout then surfaces as NETWORK_ERROR, so the row stays PENDIENTE, which is already the correct behaviour | S |
| High ✅ FIXED 19-10-2026 | compliance / bug | src/verifactu/verifacturesponse.cpp:110-157; src/sql_lite/sql_lite.cpp:1843-1851 | On a /Create reply, `ResultCode 0` with no `Return`, with `Return` null or not an object, or with a `Return` that has an empty CSV all become SUCCESS with `csv=""`. `updateTicketVerifactuFields` then writes ENVIADA with no CSV, QR or Huella: a settled row that is never retried or queried, and the export marks it `sinPayload`. `test_successWithoutReturn` (tests/test_verifactu_response.cpp:70) pins this behaviour | Add a parameter (`expectCsv` / an enum `ReplyKind {Submit, Cancel, Qr}`). For a submit, SUCCESS without a CSV becomes PENDING or NETWORK_ERROR (unknown outcome, so PENDIENTE, and recovery or "Consultar en AEAT" settles it). Keep the current behaviour for Cancel. Update the test | S |
| Medium ✅ FIXED 19-10-2026 | bug / compliance | src/verifactu/verifacturesponse.cpp:116-120, :124 | A body that is not JSON (proxy or captive-portal HTML, a truncated body), or JSON with no `ResultCode` (`toInt(-1)` gives errorCode "-1"), is treated as ERROR, which is "definitive AEAT rejection". The row becomes ERROR, the InvoiceID is cleared (sql_lite.cpp:1860) and the red "rechazada por la AEAT" count includes it, although AEAT never judged it | Map an unparseable body, or one without `ResultCode`, to NETWORK_ERROR (unknown). Keep ERROR for `ResultCode != 0` and `Return.ErrorCode` | S |
| Medium | design | src/verifactu/verifactuintegration.cpp:16-24 | `initialize()` uses raw `delete` on the old backend. An in-flight gateway reply (AddGarment submit, a RecogPrendas retry or query, adopted PayDialog requests) is aborted silently, and its requestId stays in the callers' pending maps forever. The row stays PENDIENTE (safe), but the operator gets no late success | Keep the old manager alive (disconnect it from the forwarding signals only after its replies drain, or `deleteLater` after `QNetworkAccessManager` finishes), or document it as a known limit. Add an e2e scenario | M |
| Medium | bug (UX) | src/verifactu/verifactuintegration.cpp:50-55, :67-70, :254-282 | Gateway path: `loadEmitterConfiguration()` already runs `isValid()`, so a missing ServiceKey returns "No se pudo cargar la configuración del emisor" (shown in MainWindow's panel, mainwindow.cpp:503) instead of "Clave de servicio no configurada". The `isConfigured()` check at :67 is unreachable for the gateway, and the NIF/name check is duplicated (:26-31 and :263-266) | Have `loadEmitterConfiguration` only push values, and let the common `isConfigured()` branch set `m_lastError = m_manager->getConfig()->getValidationError()` | S |
| Medium | design | src/app/mainwindow.cpp:885, src/recog_prendas/pay_dialog.cpp:263, src/recog_prendas/recog_prendas.cpp:1054, src/verifactu/verifactuintegration.cpp:196 | The gross-to-base split is duplicated in 3 callers (one through `Facturas::taxBaseFromGross`, two inline), and the amounts reach the gateway unrounded (`TaxBase` 12.396694..., `TaxAmount` from base*rate/100, `TotalAmount` 15.000000000000002). The gateway's rounding is unknown. `verifactuRemoteMatches` compares against AEAT's rounded total | Add `VerifactuIntegration::submitSimplifiedInvoiceFromGross(id, date, gross, rate)` that rounds the base to cents and sets tax = gross - base, and use it from all 3 callers plus `generateQRAsync` / `qrBeforeReply`. Coordinate with the direct backend's amount formatting | M |
| Medium | compliance | src/verifactu/verifactuintegration.cpp:144-146, verifactuinvoice.cpp:52-61, rest_api.md:139-142 | Gateway rectificativas never send `RectificationItems` (FacturasRectificadas). Art. 15 RD 1619/2012 expects the rectifying invoice to identify the original, and the direct client sends it | Serialise `m_rectifiedInvoices` as `RectificationItems` [{InvoiceID, InvoiceDate}] in `toJson()` (confirm the vendor's field names first), or record that this is an accepted legacy-path limit in verifactu-requirements.md. Ask verifactu-compliance-auditor | S-M |
| Medium | docs / design | src/verifactu/verifactuconfig.h:55-56, :95-98 | `TEST_ENDPOINT == PROD_ENDPOINT`. The gateway's environment is decided by the ServiceKey; `verifactu.production` only changes the (unused) validation URL. README:190 and :294 ("validate in TESTING before switching verifactu.production") suggest the box routes invoices | Document that the key decides the environment and the box is cosmetic, or drop the box for the gateway. Mention it in the tracker's production-switch item | S |
| Low | compliance | src/verifactu/verifactuconfig.h:57; aeathash.cpp:12 | Gateway production QR validation URL is `www2.aeat.es`, while the direct client and the AEAT spec use `www2.agenciatributaria.gob.es`. Its only consumer is `VerifactuManager::getValidationUrl` on the QR path, and no caller reads that value | Align it with `AeatHash`'s constant, or remove it together with the dead `getValidationUrl` (see the dead-code row) | S |
| Low ✅ FIXED 20-10-2026 | security | src/verifactu/verifactumanager.cpp:250-304 | `testConnection` reports SUCCESS for any HTTP status (401 / 500 included), and the comment at :260 claims it "proves connectivity + auth". A wrong key looks "Conexión correcta" | Parse the probe body with `parseVerifactuResponse`: an invalid-key code (8001) becomes a "clave de servicio no válida" warning. Fix the comment | S |
| Low | security | docs/modules/verifactu/rest_api.md:584-588 | The "Debugging" section tells you to `qDebug() << doc.toJson()` inside submit. That logs the ServiceKey in clear to ~/.laideal.log, undoing the redaction at verifactumanager.cpp:88-91 | Delete the section, or point to the existing redacted "Payload:" log line | S |
| Low | bug | src/verifactu/verifactumanager.cpp:104-106 | `validateConfiguration()` is called twice, and the diagnostic is ASCII "InvoiceID vacio" | Store the result in a local variable | S |
| Low | bug | src/verifactu/verifacturesponse.cpp:69-70 | `firstString` turns a numeric JSON value into `'f',2`. If the gateway ever returns `InvoiceID` as a number, "31121" becomes "31121.00" and never matches (fails safe) | Use `'f',2` only for amount keys; IDs use `QString::number(v.toVariant().toLongLong())` | S |
| Low | bug | src/verifactu/verifacturesponse.cpp:245 | `verifactuRemoteMatches` refuses totals <= 0, so a downward by-differences rectificativa can never be reconciled | Accept a non-zero total with matching sign, or document the limit | S |
| Low ✅ FIXED 20-10-2026 | dead-code | src/verifactu/verifactutaxitem.h:10-16, :38; .cpp:7, :44-53 | `OperationType` S2/N1/N2/EXEMPT can't be reached (no setter, always S1). EXEMPT serialises "E1" as `TaxType`, which is not a valid L9 value (E1-E6 are L10 exemption causes). `isValid` branches on EXEMPT that never happens | Remove the enum and the member, and emit "S1" as a constant; or add a setter and map EXEMPT to `TaxException`. Update `test_taxItemOperationTypeStrings` | S |
| Low ✅ FIXED 20-10-2026 | dead-code | src/verifactu/verifactuinvoice.h:17-19, verifactuinvoice.cpp:7, :116-125 | NORMAL (the default type) and SUBSTITUTE are always rejected by `isValid`. A default-constructed invoice is invalid ("NIF del comprador") | Default to SIMPLIFIED; consider removing F1/F3 | S |
| Low | dead-code | src/verifactu/verifactumanager.cpp:216-218, :236-248, verifactumanager.h:47-48; verifactuconfig `getSystemName/Version/setSystemData` | `getValidationUrl` (QR path `validationUrl`) is not read by Imprimir or any caller. System name/version are never sent, only logged. `getLastError()` duplicates `m_lastError` | Remove them, or use them | S |
| Low | design | src/verifactu/verifactuconfig.cpp:7-22 | A value-class constructor deletes a legacy INI file in AppData. It runs on every `VerifactuConfig()` (each init, each test-connection, each unit test) | Move it to a one-time startup migration | S |
| Low | guidelines | src/verifactu/verifactuintegration.cpp:133-135, :190; mainwindow.cpp:889; pay_dialog.cpp:268; recog_prendas.cpp:1056 | Four different `DescripcionOperacion` texts reach AEAT ("Servicio de lavandería", "Servicios de lavanderia", tr("Servicios de lavandería"), "Rectificativa de servicios de lavanderia"). The `tr()` one would change with a translation | Use one constant in the facade, not passed by callers and not through `tr()` | S |
| Low | guidelines | src/verifactu/verifactumanager.cpp:15-25 | `m_config` is a raw `new` without an owner type, deleted by hand | Use a plain `VerifactuConfig m_config` member | S |
| Low | guidelines | src/verifactu/verifactutypes.h:28-33, :42-46, :87-91; verifacturesponse.cpp:69-91 | Several comment blocks of 4-6 lines (estado case rule, the ticket 31121 story) go beyond the "1-2 lines, docs hold the long form" rule. The content is already in README:134 | Trim each to 1 line with a doc pointer | S |

## Documentation drift

| Doc (file:line) | Code (file:line) | Drift |
|---|---|---|
| docs/modules/verifactu/README.md:16, :98; docs/architecture.md:216, :311; docs/INDEX.md:164, :192 | src/verifactu/verifactutypes.h:7-46 | `VerifactuEstado` and its helpers live in `verifactutypes.h`, not `verifactumanager.h`. INDEX:164 also lists only 4 of the 6 estados (it lacks SIN COBRAR and RECTIFICADA) |
| README.md:13-19 (source table) | src/verifactu/ | Missing `verifactubackend.h`, `verifactutypes.h` and `verifactutaxitem.h/cpp` (the table claims `verifactuinvoice` holds TaxItem) |
| README.md:113 | verifactumanager.cpp:322-328 (only `testConnection` :269 sets a timeout) | Claims "VerifactuManager sets a 10 s transfer timeout". Async calls have none (also stated in pay_dialog.cpp:309) |
| README.md:3 vs :156, :191 | verifactuintegration.cpp:34 | "via the gateway (until 11.x)" vs "the gateway (the default)" / default `irenesolutions`: inconsistent status |
| README.md:132 | verifacturesponse.cpp:60-73, :115-131 | Lists the envelopes as guesses and says "if you have one, pin the parser"; the code says `Items` is confirmed and the fields are pinned. The code also probes `Result` / `Invoices` |
| README.md:198 | mainwindow.cpp invoicingBlockedReason; verifactuintegration.cpp:254-266 | "tickets save without an AEAT submission, with PENDIENTE": since 12.0 paid tickets are refused when not configured (README:103 itself says so) |
| README.md:279 | same | "INVALID_CONFIG at save: Silent (logged only)": now refused with a message |
| README.md:82 | verifactuintegration.cpp:121-125 | An empty return also happens for a non-rectificativa type in `submitRectificationAsync`, not only "not configured" |
| README.md:190, :294 | verifactuconfig.h:55-56 | `verifactu.production` does not change the gateway endpoint (same URL); the key decides |
| docs/architecture.md:295, :307; rest_api.md:478 | verifactuconfig.cpp:7-22; appsettings | "VerifactuConfig loads from an external .ini via QSettings": removed in 8.5; it is AppSettings with DPAPI |
| docs/architecture.md:304 | aeathash.cpp:12 | Production QR URL `www2.aeat.es` (gateway constant) vs AEAT's `www2.agenciatributaria.gob.es` |
| verifactu-requirements.md:45, :84; INDEX.md:191; rest_api.md:260, :504 | verifacturesponse.cpp:110 | `VerifactuManager::processResponse()` no longer exists; it is `parseVerifactuResponse()` |
| verifactu-requirements.md:105 | verifactumanager.cpp:216-218 | "URL per environment, see `VerifactuConfig::getValidationUrl()`": the gateway's printed QR comes from the gateway's image; the local URL is unused |
| rest_api.md:21, :449, :468 | verifactuinvoice.cpp:28-62 | `Status`=`DRAFT` "used for testing": never sent |
| rest_api.md:174 | verifactutaxitem.cpp:10-16 | "`m_taxType` (VAT = 01)": no such member; `Tax` is never sent |
| rest_api.md:197, :454 | verifactutaxitem.h, .cpp:12 | "Implemented: S1, S2, N1, N2": only S1 can be reached; EXEMPT gives "E1" |
| rest_api.md:210, :518-527 | verifactutaxitem.cpp:21-34 | "TaxBase >= 0" and "abs(...) < 0.01" listed as validations: negatives are allowed, and the mismatch only produces a `qWarning` |
| rest_api.md:530-538 | verifactuinvoice.cpp:64-106 | `isValid` list ("buyerNIF present", "totalAmount > 0") does not match the code (F2/R* only, no total check) |
| rest_api.md:256 | verifacturesponse.cpp:164 | `ResultMessage` "not used": it is the fallback description |
| rest_api.md:300 | verifacturesponse.cpp:101 | `VerifactuManager::decodeImageFromBase64()`: it is now `decodeVerifactuImageBase64()` |
| rest_api.md:322 | verifacturesponse.cpp:140 | Submit `ValidationUrl` is "calculated by getValidationUrl()": it is read from `Return.ValidationUrl` |
| rest_api.md:337-358 | verifactumanager.cpp:116-125 | FilterSet Count / Filters "Not implemented", Offset default 0: implemented, Count=10, Offset=-1 |
| rest_api.md:420-425 | verifactumanager.cpp:169-174; README:250 | Cancel's required fields omit `CompanyName`, which the code sends and README calls mandatory |
| rest_api.md:446-447, :438 | verifactuinvoice.cpp:89-93 | RelatedPartyID/Name "Complete" and F1/F3 "supported": never sent, rejected locally |
| rest_api.md:491-512, :572 | verifactuintegration.h | Data flow names `submitSimplifiedInvoice()`, `submitInvoice()`, `saveTicket(VerifactuResult)`, `createAndSubmitInvoice()`, `generateQR()`, and a 3-argument `setEmitterData`: none exist |
| rest_api.md:584-588 | verifactumanager.cpp:88-91 | Debug advice that logs the ServiceKey (see Findings) |
| rest_api.md:599 | - | "Real Decreto 1619/2012 (Verifactu)": Verifactu is RD 1007/2023 + Orden HAC/1177/2024; 1619/2012 is the invoicing regulation |
| rest_api.md:71; README.md:282 | - | "InvoiceID unique, do not repeat on the same day": NumSerieFactura is unique per issuer, not per day |

## Test gaps

Current coverage: test_verifactu_models covers the config, tax-item, invoice and estado helpers. test_verifactu_response covers the reply, query, duplicate and match parsers. test_e2e_verifactu and test_e2e_app cover submit, partial payments, timeout adoption, rejection, cancel (including the date fallback), QR reprint, query reconcile, gateway rectify closed-before-answer and re-init with one backend.

### Unit (test_verifactu_response / test_verifactu_models)
- `test_submitSuccessWithoutCsvIsUnknown`: `{"ResultCode":0}` and `{"ResultCode":0,"Return":{}}` parsed as a submit are not SUCCESS (PENDING or NETWORK_ERROR); as a cancel they stay SUCCESS. Replaces `test_successWithoutReturn`.
- `test_invalidJsonIsUnknownNotRejection` / `test_missingResultCodeIsUnknown`: status is NETWORK_ERROR, so `verifactuEstadoForResult` gives NotSubmitted.
- `test_qrRequestUndecodableImage`: QR reply with garbage base64 gives a null pixmap; decide whether that is SUCCESS or ERROR and pin it.
- `test_huellaMissingInXml`: SUCCESS with Xml but no Huella gives empty `rawHash`, CSV kept.
- `test_parseQuery_numericInvoiceId`: `InvoiceID: 31121` (number) still matches "31121".
- `test_remoteMatches_negativeRectification`: pin the chosen behaviour for totals <= 0.
- `test_errorIsDuplicate_nonDuplicateWording`: AEAT messages containing similar stems ("NIF no identificado", 4102) are not duplicates.
- `test_invoiceDefaultTypeIsSimplified` (after the fix) and `test_rectificationJsonCarriesRectifiedInvoices` (if RectificationItems is added).
- `test_simplifiedInvoiceAmountsRoundedToCents` (after the facade fix): gross 15.00 at 21 % gives base 12.40, tax 2.60, total 15.00 exactly in the JSON.
- `test_integration_rectifyWithNonRectifyingTypeReturnsEmpty`: `submitRectificationAsync(SIMPLIFIED, ...)` returns "" with `getLastError` "Tipo de factura no rectificativo" and no request.
- `test_integration_missingServiceKeyReportsKeyError` (gateway): `initialize()` is false and `getLastError()` names the "Clave de servicio"; `invoicingBlockedReason()` contains it.
- `test_manager_invalidInvoiceEmitsQueuedError`: `submitInvoiceAsync` with an empty number returns an id, and the signal arrives queued (not inside the call) with ERROR.
- `test_manager_cancelWithoutDateEmitsError`: "Datos de anulación incompletos", no request.
- `test_manager_queryWithoutIdIsUnparsed`: parsed=false, `raw` "InvoiceID vacio", via `queryFinished` only.
- `test_manager_logRedactsServiceKey`: install a message handler, submit to the fake server; no log line contains the key, the payload line shows "[redacted, N chars]", and the QR base64 is truncated in "Server response".
- `test_manager_requestIdsUniqueAcrossInstances`: two managers never return the same id.
- `test_manager_testConnection` (fake server): reachable gives SUCCESS "HTTP 200"; stopped server gives NETWORK_ERROR within about 10 s; (after the fix) an invalid-key body gives a warning.

### E2E (test_e2e_verifactu / test_e2e_app, FakeVerifactuServer)
- `test_gateway_droppedConnection_staysPendiente`: `Reply.drop = true` on Create. PayDialog leaves the row PENDIENTE with the InvoiceID kept, not ERROR. The `drop` option exists but no scenario uses it.
- `test_gateway_stalledReply_timesOut` (after the timeout fix): delay 40 s with a lowered timeout seam; MainWindow `m_pendingSubmits` is drained and the row is PENDIENTE.
- `test_gateway_htmlBody_notCountedAsRefused`: Create answers 200 with HTML; the row stays PENDIENTE and the refused count does not grow.
- `test_gateway_successWithoutCsv_notEnviada`: Create answers `{"ResultCode":0}`; the row is not ENVIADA.
- `test_gateway_reinitialiseWhileSubmitInFlight`: AddGarment paid submit delayed, then Configuración accepted (`initializeVerifactu`). The row stays PENDIENTE and is listed by the startup recovery, with no crash.
- `test_gateway_rectifySubstitutionPayload`: Create body carries `InvoiceType` R1-R5, `RectificationType` S, `RectificationTaxBase/Amount` = original, `TaxItems` = new totals (and `RectificationItems` if added). By differences gives a signed delta and no `RectificationTax*`.
- `test_gateway_cancelPayloadShape`: Cancel body has InvoiceID, ISO date, SellerID, CompanyName and ServiceKey.
- `test_gateway_queryNeverAnswered_noLeak`: GetFilteredList is dropped; the Recogida result panel reports a failed query (`parsed=false`).

### UI smoke (tests/ui)
- A gateway-mode block, if the UI harness can point at a fake gateway: Configuración → Probar conexión against the fake (success and unreachable messages); one paid ticket gives ENVIADA with a CSV and the printed QR. Today the harness appears to run on the direct client only (A3 checks that no gateway URL appears).

### Post-update self-check (src/app/selfcheck*)
- Add an offline M-check that reads the live settings read-only. If connection = irenesolutions, the DPAPI-decrypted ServiceKey must be non-empty and NIF/name set: a DPAPI failure after a Windows profile move silently blocks all invoicing (README:292). If connection = aeat, the certificate thumbprint or file must resolve. No network. Report "Verifactu: configuración completa / falta X" without printing the key. Effort S.

## Questions / cross-module notes

- **verifactu-direct**: please confirm that AeatRecord rounds amounts to cents per field, so that base + cuota = total for the unrounded inputs from `simplifiedInvoice`, and whether the direct transport has its own bound (WinHTTP timeouts). The facade-level rounding proposal affects both backends. `VerifactuInvoice::getRectifiedInvoices()` is used only by the direct client. (Could not reach that reviewer: the agent name did not resolve.)
- **PayDialog (recog_prendas)**: pay_dialog.cpp:270-280 still has a fallback that stores a payment "localmente sin envío" with blank `verifactu_*` when the submit returns empty. Since 12.0 `invoicingBlockedReason` should make this unreachable. Either remove it or turn it into a refusal, so a paid row can never exist without a Verifactu record. The comment at :309 cites a 10 s transport timeout that does not exist.
- **MainWindow**: mainwindow.cpp:503 shows `getLastError()` after a failed init (misleading text for a missing key, see Findings). The test-connection lambda (:446-475) builds a full `VerifactuManager` and runs a nested event loop inside the modal dialog; this is acceptable, but nothing tests it.
- **RecogPrendas**: recog_prendas.cpp:1085 is a one-shot `queryFinished` connection with no timeout. If the reply never arrives (see the High finding), the heap `Connection` leaks and no message appears.
- **Supervisor**: progress_tracker.md:23 ("switch to PRODUCTION once IreneSolutions answers") conflicts with :42 (production goes through the direct client; the IreneSolutions request becomes moot). Decide the gateway's 12.0 status ("legacy, selectable" or "default") and make README:3/:156/:191 agree.
- The compliance auditor should judge the missing `RectificationItems` on gateway rectificativas, and whether a SUCCESS-without-CSV row already exists in the shop data. A read-only query for paid ENVIADA rows with an empty `verifactu_csv` is enough; this reviewer did not open the shop DB.
