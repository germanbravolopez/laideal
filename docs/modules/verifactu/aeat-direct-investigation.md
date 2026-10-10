# Investigation: talking to AEAT directly instead of through IreneSolutions

Status: **research report, October 2026** (branch `feature/aeat-direct-investigation`). Nothing here is implemented; nothing reaches AEAT production.

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

## Sources

- AEAT, VERI*FACTU technical information: <https://sede.agenciatributaria.gob.es/Sede/iva/sistemas-informaticos-facturacion-verifactu/informacion-tecnica/esquemas.html>
- AEAT, huella specification v0.1.2: <https://www.agenciatributaria.es/static_files/AEAT_Desarrolladores/EEDD/IVA/VERI-FACTU/Veri-Factu_especificaciones_huella_hash_registros.pdf>
- AEAT FAQ, declaración responsable: <https://sede.agenciatributaria.gob.es/Sede/iva/sistemas-informaticos-facturacion-verifactu/preguntas-frecuentes/certificacion-sistemas-informaticos-declaracion-responsable.html>
- AEAT FAQ, colaboración social: <https://sede.agenciatributaria.gob.es/Sede/iva/sistemas-informaticos-facturacion-verifactu/preguntas-frecuentes/colaboracion-social.html>
- Orden HAC/1177/2024: <https://www.boe.es/diario_boe/txt.php?id=BOE-A-2024-22138>
- WSDL / XSD mirror: <https://github.com/hectorsipe/aeat-verifactu>; hash and QR reference implementation tested with the official vectors: <https://github.com/albertquerol12345/verifactu-huella-qr>
- Common hash pitfalls: <https://dev.to/a_molina_b9d7a17c33863229/implementando-la-huella-sha-256-de-verifactu-6-detalles-que-rompen-el-hash-y-uno-que-rompe-la-1d82>
- Deadlines: RD-ley 15/2025 (AEAT note <https://sede.agenciatributaria.gob.es/Sede/iva/sistemas-informaticos-facturacion-verifactu/nota-informativa-ampliacion-plazo-adaptacion-facturacion.html>); October 2028 announcement: <https://www.elespanol.com/invertia/economia/20261005/hacienda-vuelve-retrasar-verifactu-pospone-implantacion-empresas-autonomos/1003744409037_0.html>
