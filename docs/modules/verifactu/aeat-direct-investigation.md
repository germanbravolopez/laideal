# Investigation: talking to AEAT directly instead of through IreneSolutions

Status: **research report and implementation, October 2026** (branch `feature/aeat-direct-investigation`, not merged). The direct client is built and tested against a fake AEAT; it has not talked to AEAT yet - see [Status on this branch](#status-on-this-branch-october-2026). Nothing reaches AEAT production.

## 1. Question

Today every Verifactu operation goes through the IreneSolutions REST gateway (`VerifactuConfig::PROD_ENDPOINT`, `https://facturae.irenesolutions.com:8050/Kivu/Taxes/Verifactu/Invoices` + `/Create`, `/Cancel`, `/GetFilteredList`, `/GetQrCode`, authenticated with a `ServiceKey`). Can the app instead build the Verifactu records itself and submit them straight to the AEAT web services, following the official specifications, and is it worth it?

**Short answer**: it is technically feasible with the Qt stack we already have, and the official specifications are public and complete. The cost is not the HTTP call but everything IreneSolutions does today on our behalf (XML records, the chained hash, the QR image, AEAT's flow control, error semantics) plus two non-technical items: the owner's **electronic certificate** has to live on the shop PC, and the project becomes the only party responsible for the compliance of the whole system. The legal deadline gives us time (section 7), so the recommendation is a phased approach that keeps IreneSolutions until a direct client has passed a full test run (section 9).

## 2. What IreneSolutions does for us today

The app sends a small JSON (`VerifactuInvoice::toJson`: `InvoiceID`, `InvoiceDate`, `InvoiceType`, `SellerID`, `CompanyName`, `TaxItems`, `TotalAmount`, `TotalTaxAmount`, rectification fields) and reads back `ResultCode`, `Return.CSV`, the record XML, the hash and a base64 BMP QR. Behind that, the gateway:

| Task | Done by IreneSolutions today | A direct client must |
|---|---|---|
| Build the `RegistroAlta` / `RegistroAnulacion` XML (SuministroLR.xsd) | yes | build and validate it ourselves |
| Compute the **huella** (SHA-256) and keep the **chain** (previous record per issuer) | yes, their state | compute it and own the chain state in our DB |
| Fill the `SistemaInformatico` block (producer, system name/id, version, installation number) | yes, with their data | fill it with ours |
| SOAP over mutual TLS with a qualified certificate | yes, with their credentials | present the owner's certificate |
| Respect AEAT flow control (wait time between submissions) | yes | queue records and honour `TiempoEsperaEnvio` |
| Render the QR image | yes (`/GetQrCode`) | encode the QR locally |
| Query records (`/GetFilteredList`) | yes | call `ConsultaFactuSistemaFacturacion` |
| Represent the owner before AEAT | yes: requires a type 017 social-collaboration agreement and a representation document signed by the owner | nothing: the owner submits as herself |

## 3. The AEAT side: services, environments, authentication

- **Protocol**: SOAP 1.1 web service described by `SistemaFacturacion.wsdl`, schemas `SuministroLR.xsd`, `SuministroInformacion.xsd`, `RespuestaSuministro.xsd`, `ConsultaLR.xsd`, `RespuestaConsultaLR.xsd` (published in the AEAT developers portal; also mirrored in public repositories).
- **Endpoints** (from the official WSDL):
  - Production: `https://www1.agenciatributaria.gob.es/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP` (and `www10.` for seal certificates).
  - Pre-production / tests: `https://prewww1.aeat.es/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP` (and `prewww10.`). Records sent there have **no tax effect**, and the same real certificate is used.
  - QR verification URL: `https://www2.agenciatributaria.gob.es/wlpl/TIKE-CONT/ValidarQR` (production), `https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR` (tests).
- **Authentication**: no user / key: **mutual TLS** with a qualified electronic certificate. The taxpayer may submit herself with her own certificate (persona física, FNMT or equivalent); a third party may submit for her only with a type 017 social-collaboration agreement plus a normalised representation document (AEAT FAQ "Colaboración social").
- **Record contents** (VERI*FACTU mode): `IDFactura` (issuer NIF, series-number, issue date), `TipoFactura` (`F2` simplified for our tickets, `R5` for rectificativas of simplified invoices), tax breakdown, `CuotaTotal`, `ImporteTotal`, `Encadenamiento` (`PrimerRegistro` or `RegistroAnterior` = issuer, number, date and huella of the previous record), `SistemaInformatico` (`NombreRazon`, `NIF`, `NombreSistemaInformatico`, `IdSistemaInformatico`, `Version`, `NumeroInstalacion`, `TipoUsoPosibleSoloVerifactu`, `TipoUsoPosibleMultiOT`, `IndicadorMultiplesOT`), `FechaHoraHusoGenRegistro`, `TipoHuella`, `Huella`. In VERI*FACTU mode records are **not** XAdES-signed and **no event log** is required (both apply only to non-VERI*FACTU systems).
- **Response** (`RespuestaBaseType`): `CSV`, `DatosPresentacion`, `TiempoEsperaEnvio`, `EstadoEnvio` (Correcto / ParcialmenteCorrecto / Incorrecto) and per record (`RespuestaExpedidaType`) `EstadoRegistro` (Correcto / AceptadoConErrores / Incorrecto), `CodigoErrorRegistro`, `DescripcionErrorRegistro`, `RegistroDuplicado`. A resubmission after a rejection is flagged with `Subsanacion` / `RechazoPrevio`.
- **Flow control**: each reply carries `TiempoEsperaEnvio` (seconds); the next submission waits that long unless the batch reaches the maximum (1000 records). Our volume (tens of tickets a day) fits one record per submission, but the wait must be honoured: records are generated at once and **queued**, not posted synchronously at save time. To be confirmed against the official "Descripción del servicio web" before building.

## 4. The huella (hash) and the chain

From the AEAT *Especificaciones técnicas para la generación de la huella o hash de los registros de facturación* (v0.1.2), confirmed by an open-source implementation tested against the three official vectors:

- **Alta**: `IDEmisorFactura=<nif>&NumSerieFactura=<num>&FechaExpedicionFactura=<dd-MM-yyyy>&TipoFactura=<F2>&CuotaTotal=<x.xx>&ImporteTotal=<x.xx>&Huella=<previous>&FechaHoraHusoGenRegistro=<yyyy-MM-ddTHH:mm:ss+hh:mm>`
- **Anulación**: `IDEmisorFacturaAnulada=…&NumSerieFacturaAnulada=…&FechaExpedicionFacturaAnulada=…&Huella=<previous>&FechaHoraHusoGenRegistro=…`
- UTF-8, SHA-256, **upper-case hex**. Values trimmed at both ends; an empty value is still written (`Huella=` on the first record). Amounts as text exactly as in the XML (two decimals, dot); dates `dd-MM-yyyy`; the timestamp carries its offset (`+01:00` / `+02:00`); no URL encoding (`/` stays `/`).
- Official vector: alta `89890001K`, `12345678/G33`, `01-01-2024`, `F1`, `12.35`, `123.45`, empty previous, `2024-01-01T19:20:30+01:00` → `3C464DAF61ACB827C65FDA19F352A4E3BDC2C640E9E9FC4CC058073F38F12F60`.
- **One chain per issuer, altas and anulaciones together**, in generation order. Two records must never read the same "previous": the app is single-user, but the async submit paths (MainWindow, Cobrar, Recogida, PendingSubmits) must still take the previous huella and write the new one in one DB transaction.

**Migration**: the first direct record must chain to the **last record AEAT holds**, which IreneSolutions generated. We store `verifactu_hash` and `verifactu_xml` (and `verifactu_cancel_xml`) per row, so the previous issuer/number/date/huella can be read locally, and `ConsultaFactuSistemaFacturacion` can confirm it before the switch. To verify in the proof of concept: that our stored `verifactu_hash` is AEAT's huella (not a gateway-internal value) and that IreneSolutions sent nothing for this NIF that we did not record.

## 5. What we would build (Qt / C++)

All with the existing stack (Qt 6 Network + Xml, SQLite); one new third-party file for the QR.

1. **`VerifactuBackend` interface** in `src/verifactu`, with the current gateway as one implementation and `AeatDirectBackend` as the other, so both coexist and the switch is a setting.
2. **Record builder**: `RegistroAlta` (F2 with the IVA breakdown, R5 substitution / differences for Rectificar factura) and `RegistroAnulacion`, `SistemaInformatico` from AppSettings; XSD validation in the test suite (a CI step with `xmllint` or Python `lxml`, since Qt has no XSD validator).
3. **Huella + chain**: pure functions unit-tested with the official vectors; chain state in a new table (last huella per issuer), updated in the same transaction as the record.
4. **SOAP client over mutual TLS**: `QNetworkAccessManager` with `QSslConfiguration::setLocalCertificate` / `setPrivateKey`, the certificate loaded from the owner's `.pfx` (`QSslCertificate::importPkcs12`). To verify early: that this works with the TLS backend our Windows build deploys (OpenSSL vs Schannel in Qt 6.4).
5. **Submission queue**: records are created at save / Cobrar / cancel time (estado `PENDIENTE`), a single sender posts them honouring `TiempoEsperaEnvio`, and maps `EstadoRegistro` onto the existing `VerifactuEstado` (Correcto and AceptadoConErrores → `ENVIADA` with CSV; Incorrecto → `ERROR` with code and text; duplicate → adopt). This replaces the current "wait 3 s for the reply, then print" logic.
6. **Local QR**: the QR only encodes `ValidarQR?nif=…&numserie=…&fecha=…&importe=…`, so it can be printed **at once**, before AEAT replies; encoded with a small MIT library such as Nayuki's QR Code generator (single C++ file), level M, 30-40 mm, "QR tributario:" above it.
7. **Query**: `ConsultaFactuSistemaFacturacion` for the existing comparison dialog and the startup reconciliation, replacing `GetFilteredList`.
8. **Tests**: extend `FakeVerifactuServer` to answer SOAP; the e2e bench keeps never reaching AEAT. A real run against `prewww1` becomes possible for the smoke test with the owner's certificate (no tax effect), which the gateway never allowed.

Rough size: the gateway client is ~1 200 lines today; a direct client is estimated at **2 500-3 500 lines plus tests** (record builder and chain the largest parts), i.e. several weeks of part-time work, then a test period in pre-production.

## 6. Legal and operational responsibilities

- **Declaración responsable**: required either way. AEAT's FAQ is explicit that a system used only in-house still needs its producer's certification, visible in the system, for every version, and that a system made of several components needs one per component that affects compliance. Today ours (Ayuda → Acerca de Verifactu) covers the app and IreneSolutions covers its gateway; going direct, **our declaration covers everything**, including the XML, the hash and the submission, so its text must be reviewed.
- **The certificate**: the owner's personal certificate is her identity for *every* tax procedure, not only Verifactu. On the shop PC it must be protected: the `.pfx` password stored with Windows DPAPI (never in the settings JSON or the repo), the file readable only by the shop account, renewal tracked (the app should warn weeks before it expires). This is the biggest practical risk of the change.
- **Representation**: if someone else (e.g. the technical contact) should submit with their own certificate instead, that is representation and needs the normalised model; to clarify with AEAT before relying on it.
- **Maintenance**: AEAT schema or validation changes become our job (today IreneSolutions absorbs them). The AEAT publishes versions in advance and keeps the pre-production environment for testing.

## 7. Timing

- RD-ley 15/2025 (BOE 3-12-2025) set the obligation at **1 July 2027** for self-employed people like the owner (1 January 2027 for companies).
- On **5 October 2026** Hacienda announced a further delay to **October 2028**, to coincide with mandatory B2B e-invoicing; **pending its publication in the BOE**.
- Producers of invoicing software had no delay (obliged since 29 July 2025), which concerns the declaración responsable, not the shop's submission date.

So there is no deadline pressure: the shop can stay on IreneSolutions (or not submit at all, voluntarily) for 1-2 years while a direct client is built and tested.

## 8. Comparison

| | IreneSolutions gateway (today) | Direct AEAT client |
|---|---|---|
| Fee | per their contract (to get from IreneSolutions) | none |
| Dependency | third-party service and its availability; same URL for test and production | only AEAT |
| Testing | no real test environment under our control | AEAT pre-production with the real certificate, no tax effect |
| Certificate on the shop PC | no | **yes** (owner's personal certificate) |
| Compliance scope we certify | the app | the whole system |
| Code to maintain | ~1 200 lines | ~2 500-3 500 lines + XSD updates |
| Printing | QR after AEAT replies (3 s wait, else receipt without QR) | QR printed at once (computed locally) |
| Chain / records | held by the gateway | held by us (fully inspectable, exportable) |

## 9. Recommendation

1. **Ask IreneSolutions** (already pending: the production-access email) for the fee and for confirmation that our stored `verifactu_hash` is AEAT's huella; their answer settles the cost side.
2. **Phase 1 - offline, no risk** (can start now on this branch): the `VerifactuBackend` interface, the record builder, the huella and chain functions with the official vectors, the local QR and XSD validation in CI. None of it needs a certificate or touches AEAT, and the local QR could be adopted even with the gateway.
3. **Phase 2 - proof of concept against pre-production**: with the owner's consent and certificate, one alta, one anulación and one query against `prewww1`, checking the mutual-TLS setup on the shop's Windows build and the response mapping.
4. **Phase 3 - decide**: with the fee, the PoC result and the final legal date, choose whether to switch. If yes, migrate the chain from the last AEAT record, run a smoke test in pre-production, and keep the gateway backend available for one release as a fallback.

## Status on this branch (October 2026)

Everything of phases 1-3 that can be done without the owner's certificate and without contacting AEAT is built and tested on `feature/aeat-direct-investigation`. The app still uses IreneSolutions unless Configuración → Verifactu → *Conexión con la AEAT* is set to *Directa con la AEAT (en pruebas)*.

| Phase | Status |
|---|---|
| 1. Ask IreneSolutions the fee and whether `verifactu_hash` is AEAT's huella | **Pending (owner / technical contact)** - an email draft is ready; nothing sent. The second question is now partly answered by the data: the gateway's stored `verifactu_xml` carries the AEAT record with its `Huella`. |
| 2. Offline parts | **Done** - see the steps below. |
| 3. Proof of concept against AEAT pre-production | **Built, not run** - *Prueba con la AEAT (entorno de pruebas)* in Configuración runs it; it needs the owner's certificate and her consent. |
| 4. Decide | Open: cost, the PoC result and the final legal date. |

### What was built

| Step | Files | What it does |
|---|---|---|
| 1 | `aeathash.*` | Huella of alta / anulación records, the amount (`123.40`) and timestamp (`+hh:mm`) formats, the QR verification URL. Checked against the three official AEAT hash vectors and the official QR URLs. |
| 2 | `aeatrecord.*` | `RegistroAlta` (F2, R5 by substitution / differences with `FacturasRectificadas` and `ImporteRectificacion`), `RegistroAnulacion`, `SistemaInformatico`, the submission and query SOAP envelopes (with paging). Every sample is validated against the **official AEAT XSDs** (`tests/fixtures/aeat-xsd`, downloaded from AEAT unchanged) by the `aeat_xsd` ctest entry (Python + lxml; CI installs lxml). |
| 3 | `aeatresponse.*` | AEAT's submission and query replies and SOAP faults, mapped onto the app's `VerifactuResult` / `VerifactuRemoteRecord` (Correcto and AceptadoConErrores accepted, a duplicate of an accepted record accepted, Incorrecto rejected, a cancelled registration never adopted as sent). Reply fixtures validated against the official response XSDs. |
| 4 | `aeatstore.*` | Two tables in the shop DB: `aeat_chain` (head of each issuer's chain) and `aeat_records` (every generated record: exact XML, hash, outcome, attempts). A record is appended and the head moved in one transaction; test and production apart. |
| 5 | `aeatqr.*`, `src/third_party/qrcodegen` | The QR drawn locally (Nayuki qrcodegen, MIT, level M). A sample was decoded back to the exact URL by an independent reader. |
| 6 | `verifactubackend.h`, `aeatdirectbackend.*`, `aeatcertificate.*`, `aeattransport.*` | The direct backend behind the same interface as the gateway: records stored and chained, sent together (up to 1000), never before `TiempoEsperaEnvio` has passed, a stored record resent unchanged after a lost reply (AEAT's duplicate answer adopted), a rejected one replaced by a new record flagged `Subsanacion` / `RechazoPrevio`, an accepted one answered from the store, unsent records sent at the next start. Tested end to end against a fake AEAT SOAP service (`tests/support/fakeaeatserver`). |
| 7 | `aeatdirectbackend.*` (`continueChainFromAeat`), `aeatresponse.*` (`chainTip`) | Hand-over from the gateway: the first direct record chains to the issuer's newest record - the one no other record chains to - among AEAT's registrations (queried month by month, up to 24 back) and the gateway's stored cancellations (the query does not return them). |
| 8 | `verifactuintegration.*`, `appsettings.*`, `settingsdialog.*`, `mainwindow.*`, `rectifyinvoicedialog.cpp`, `aeatselftest.*` | In the app: the connection and the certificate are chosen in Configuración (a Windows-store certificate, or a .pfx whose password is stored DPAPI-encrypted); `VerifactuIntegration` picks the backend; the chain continues from AEAT when the app starts; Rectificar factura sends the corrected invoice; the self-test is the proof of concept. |

Tests: 8 new ctest entries (`aeat_hash`, `aeat_record`, `aeat_response`, `aeat_store`, `aeat_qr`, `aeat_direct_backend`, `aeat_xsd`, `e2e_aeat_direct`) plus a settings-dialog case; 25 in all, green locally and in CI. The key rules were mutation-checked (removing each one fails its test).

### Changes after the compliance review

The `verifactu-compliance-auditor` reviewed the branch and found nine should-fix points, all fixed and each guarded by a test that fails without the fix: no record is generated before the chain is synced with AEAT the first time (requests wait); later starts re-sync offline, so records the gateway generated while switched away are followed; an unreadable chain is an error; records are generated even when the certificate is unusable (only the sending waits); the direct client uses AEAT pre-production unless its own explicit setting says otherwise (the gateway's PRODUCCIÓN box no longer decides); switching back to the gateway is refused while direct records are unsent; a duplicate of another system's record is accepted without claiming our record, a duplicate of a cancelled invoice is reported and never re-registered; failed sends are retried on their own and outcomes nobody waited for settle their rows; the first sync always queries this month and the previous one and fails when AEAT returns records without their hash; Rectificar factura takes the corrected invoice from its stored record and only allows R5 on the direct connection; "Consultar en AEAT" passes the invoice date; the QR is drawn at exactly level M.

### Findings while building

- **Qt's Schannel backend cannot read PKCS#12** (`The backend "schannel" cannot read PKCS12 format`). The release ships no OpenSSL, so the certificate is handled with the Windows crypto API (`PFXImportCertStore` in memory, or the personal store by thumbprint) and the requests go through **WinHTTP**, which presents it in the TLS handshake. A certificate already installed in Windows (as the FNMT one usually is, for the AEAT website) can be used without storing any password. *Not yet proven against AEAT*: the TLS handshake with a real certificate is the first thing the PoC checks.
- **The gateway's chain does not follow the local ticket order**: in the shop DB ticket 31277 chains to 31110, not to the previous local ticket 31273 - IreneSolutions' shared test environment holds other records (e.g. earlier test runs) in between. The hand-over therefore asks AEAT for the chain tip instead of trusting local data.
- **The official schemas differ from the public mirrors** (e.g. the query's `EstadoRegistro` is `Correcto / AceptadoConErrores / Anulado`, not `Correcta...`): the vendored XSDs are AEAT's own files.
- **Flow control**: the first submission goes at once; the next waits `TiempoEsperaEnvio` (60 s in production). A second paid ticket within a minute is therefore printed as a recibo without QR and patched when AEAT replies, exactly as with a slow gateway today. Printing the QR before the reply (it is computed locally) would remove the wait, but changes the rule "factura verificable only once ENVIADA" and needs a decision.

### To confirm in the proof of concept

0. **Whether a new system should continue IreneSolutions' chain at all.** The whole hand-over assumes one chain per issuer across systems. If AEAT expects each system / installation (a different `SistemaInformatico`) to start its own chain with `PrimerRegistro`, the sync must change. This is the first question for AEAT. Related: switching back to the gateway after direct records forks IreneSolutions' chain (it keeps its own state), so going back should be treated as exceptional.
1. The WinHTTP + certificate handshake with `prewww1.aeat.es`.
2. That `Subsanacion = S` + `RechazoPrevio = S` is the right flag pair to resend a corrected record after a rejection (the spec allows `S` and `X`).
3. The real `TiempoEsperaEnvio` and that a query is not subject to it.
4. That the chain hand-over from the gateway is accepted (the first direct record chains to a record IreneSolutions generated), and that the query returns `Huella`, `Encadenamiento` and `IdPeticion` for records IreneSolutions submitted.
6. Whether an unchanged record resent days later is accepted (any limit on the age of `FechaHoraHusoGenRegistro`) and whether `RemisionVoluntaria / Incidencia` is expected after an outage; what `RechazoPrevio = S` means on a `RegistroAnulacion`; AEAT's fault codes.
5. The text of the declaración responsable once our system builds the records itself.

### How to run the proof of concept

On the shop PC (or any PC with the owner's certificate), with her consent: Configuración → Verifactu → *Conexión con la AEAT*: *Directa con la AEAT (en pruebas)*, choose her certificate in the list (or a .pfx file and its password), then **Prueba con la AEAT (entorno de pruebas)**. It always uses AEAT pre-production (no tax effect, whatever the PRODUCCIÓN box says): it shows the certificate, continues the test chain, registers `PRUEBA-<date and time>` for 1,21 €, queries it back, waits the time AEAT asks and cancels it. Nothing is saved as configuration unless *Aceptar* is pressed; leave the connection on IreneSolutions afterwards until the decision.

## Sources

- AEAT, VERI*FACTU technical information: <https://sede.agenciatributaria.gob.es/Sede/iva/sistemas-informaticos-facturacion-verifactu/informacion-tecnica/esquemas.html>
- AEAT, huella specification v0.1.2: <https://www.agenciatributaria.es/static_files/AEAT_Desarrolladores/EEDD/IVA/VERI-FACTU/Veri-Factu_especificaciones_huella_hash_registros.pdf>
- AEAT FAQ, declaración responsable: <https://sede.agenciatributaria.gob.es/Sede/iva/sistemas-informaticos-facturacion-verifactu/preguntas-frecuentes/certificacion-sistemas-informaticos-declaracion-responsable.html>
- AEAT FAQ, colaboración social: <https://sede.agenciatributaria.gob.es/Sede/iva/sistemas-informaticos-facturacion-verifactu/preguntas-frecuentes/colaboracion-social.html>
- Orden HAC/1177/2024: <https://www.boe.es/diario_boe/txt.php?id=BOE-A-2024-22138>
- WSDL / XSD mirror: <https://github.com/hectorsipe/aeat-verifactu>; hash and QR reference implementation tested with the official vectors: <https://github.com/albertquerol12345/verifactu-huella-qr>
- Common hash pitfalls: <https://dev.to/a_molina_b9d7a17c33863229/implementando-la-huella-sha-256-de-verifactu-6-detalles-que-rompen-el-hash-y-uno-que-rompe-la-1d82>
- Deadlines: RD-ley 15/2025 (AEAT note <https://sede.agenciatributaria.gob.es/Sede/iva/sistemas-informaticos-facturacion-verifactu/nota-informativa-ampliacion-plazo-adaptacion-facturacion.html>); October 2028 announcement: <https://www.elespanol.com/invertia/economia/20261005/hacienda-vuelve-retrasar-verifactu-pospone-implantacion-empresas-autonomos/1003744409037_0.html>
