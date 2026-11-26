# IreneSolutions Gateway — REST API as the App Uses It

The gateway is the app's default Verifactu connection (`VerifactuManager`; how it fits in: [README.md](./README.md)). This page covers only what the app sends and reads. The vendor's own reference (https://facturae.irenesolutions.com/, provided with the ServiceKey) is the contract; the code (`verifactumanager.cpp`, `verifacturesponse.cpp`, `verifactuinvoice.cpp`, `verifactutaxitem.cpp`) is authoritative for what is actually sent and parsed. The direct AEAT client does not use this API.

## Endpoints

Base URL (both environments; the ServiceKey decides where invoices go): `https://facturae.irenesolutions.com:8050/Kivu/Taxes/Verifactu/Invoices` (`VerifactuConfig`).

| Call | Method | Used for |
|------|--------|----------|
| `/Create` | POST | Submit one invoice (F2 or R1-R5) |
| `/Cancel` | POST | Cancel an invoice |
| `/GetQrCode` | POST | QR of an already submitted invoice (a gateway reprint with no stored URL only) |
| `/GetFilteredList` | POST | Which invoices AEAT holds (Consultar en AEAT, [README §Reconciling](./README.md#reconciling-with-aeat-consultar-en-aeat)) |

- JSON body; the `ServiceKey` goes in the body of every call and is logged only as `[redacted, N chars]`.
- Transfer timeout 30 s; the connection test (`/Create` with a stub payload, since a GET never answers) 10 s. It reports a refused key (HTTP 401 / 403, or error `8001` in the body) apart from a network failure.
- Validation URL (QR host): TESTING `https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR`, PRODUCTION `https://www2.aeat.es/wlpl/TIKE-CONT/ValidarQR` (`VerifactuConfig::getValidationUrl()`); the box `verifactu.environment` changes only this URL.

## Invoice object (`/Create`, `/GetQrCode`)

Built by `VerifactuInvoice::toJson()`:

| Field | Sent value |
|-------|-----------|
| `InvoiceID` | NumSerieFactura: `n_recibo`, or `<n_recibo>-<seq>` for a later payment event, `R-<n>` for a rectificativa. Unique per issuer (not per day); AEAT answers a repeat as a duplicate |
| `InvoiceDate` | ISO `YYYY-MM-DD` |
| `InvoiceType` | `F2` (simplified, no buyer) or `R1`-`R5`. F1 / F3 do not exist in the app (rejected locally) |
| `SellerID` | Issuer NIF (`verifactu.nif`) |
| `CompanyName` | Issuer name (`verifactu.company_name`). **Required**: omitting it gives `8002` "El campo CompanyName es obligatorio." |
| `Text` | Description (optional) |
| `TaxItems` | Array of tax lines (below), at least one |
| `TotalAmount`, `TotalTaxAmount` | Computed from the tax items |
| `RectificationType` | `S` (substitution) or `I` (differences); only on R1-R5 |
| `RectificationTaxBase`, `RectificationTaxAmount` | Only for `S`: the original base and amount being replaced; the tax item then carries the corrected total |

Not sent: `Status` (so every call is a definitive submission, never `DRAFT`), `IsInvoiceFix`, `IsRejected`, `RelatedPartyID` / `Name` / `IDType` and `CountryID` (the app issues only F2 and R-types, which carry no buyer), `RectificationItems` (the local link is `ingresos.verifactu_rectifies_n_recibo`; because of this the facade refuses a gateway rectificativa in PRODUCTION), `RectificationTaxAmountSurcharge`.

### Tax item (`VerifactuTaxItem::toJson()`)

| Field | Sent value |
|-------|-----------|
| `TaxType` | Always `S1` (taxable, not exempt, no reverse charge) |
| `TaxBase` | Base; a negative one is allowed (a differences rectificativa going down) |
| `TaxRate` | Percentage, must be >= 0 |
| `TaxAmount` | Cuota; `splitGross` makes base + cuota the amount charged. A difference from `TaxBase * TaxRate / 100` of 0.01 or more only logs a warning |

`Tax`, `TaxScheme`, `TaxException`, `TaxRateSurcharge` and `TaxAmountSurcharge` are not sent: the gateway assumes IVA (`01`) and the general regime (`01`).

### Example (F2)

```json
{
  "InvoiceType": "F2", "InvoiceID": "24417", "InvoiceDate": "2026-05-17",
  "SellerID": "B12345678", "CompanyName": "Mi Empresa",
  "TaxItems": [{ "TaxType": "S1", "TaxBase": 100.00, "TaxRate": 21.0, "TaxAmount": 21.00 }],
  "TotalAmount": 121.00, "TotalTaxAmount": 21.00, "ServiceKey": "…"
}
```

## `/Cancel`

Body: `InvoiceID`, `InvoiceDate`, `SellerID`, `CompanyName` (always sent), `ServiceKey`. The date is the one the invoice was registered with.

## `/GetFilteredList`

Request (`VerifactuManager::queryInvoiceAsync`, vendor `FilterSet`): one filter on the ticket number.

```json
{"ServiceKey":"…","Filters":[{"FieldName":"InvoiceID","Operator":"=","Value":"30877"}],"Count":10,"Offset":-1}
```

Reply: the schema is not published (the vendor's client parses it untyped). `Items` is confirmed against real replies (`test_parseQuery_realPopulatedReply`) with the fields `InvoiceID`, `InvoiceDate`, `TotalAmount`, `CSV`, `StatusResponse`, `ErrorCode`, `IsRejected`, `ValidationUrl`. `parseVerifactuQueryResponse()` also probes the envelopes `Return` / `Records` / `List` / `Result` / `Invoices` and a bare array, normalises dates to `dd-MM-yyyy`, keeps the whole payload in `raw` and always logs it.

- **The service stores every submission attempt**, not one record per invoice. A ticket sent once and retried four times comes back as five records under one `InvoiceID`: four duplicate rejections (`ErrorCode 9999`, `CSV: null`) and the acceptance, which is not necessarily first. The parser picks the accepted record (a CSV, no `ErrorCode`, not `IsRejected`), else the first, and reports `recordCount`.
- A non-zero `ResultCode`, a body that is not JSON, or an envelope it does not understand leaves `parsed = false` (a failed query, never "AEAT does not have it"). Only a well-formed empty result means absent.

## Reply handling (`parseVerifactuResponse`, told whether it answers a submit, cancel or QR)

| Reply | `VerifactuResult` |
|-------|-------------------|
| Not JSON, or no numeric `ResultCode` | `NETWORK_ERROR`: unknown outcome, never a refusal |
| Transport error / timeout | `NETWORK_ERROR` (with the gateway's own message when the HTTP error body carries one) |
| `ResultCode != 0` | `ERROR`: `errorCode` = the code, `errorDescription` = `Return.ErrorDescription`, else `ResultMessage` |
| `ResultCode == 0`, `Return.ErrorCode` set | `ERROR` with that code and `Return.ErrorDescription` |
| `ResultCode == 0`, submit, no CSV | `PENDING` (never `ENVIADA`): checked later |
| `ResultCode == 0`, otherwise | `SUCCESS` |
| `/GetQrCode` with `Return` = base64 image | `SUCCESS` with `qrCode`; an unreadable image is `ERROR` |

What is read from `Return` on success, and where it goes:

| Field | Captured as | Stored in |
|-------|-------------|-----------|
| `CSV` | `csv` | `verifactu_csv` |
| `ValidationUrl` | `validationUrl` (a QR reply gets a locally assembled URL) | `verifactu_url_qr` |
| `Xml` (the record AEAT registered) | `rawXml` | `verifactu_xml` |
| `<Huella>` inside `Xml` (any namespace prefix, upper-case hex; not `HuellaPrevia`) | `rawHash` | `verifactu_hash` |
| `QrCode` (base64 BMP) | `qrCode` (`QPixmap`, `decodeVerifactuImageBase64`) | not stored: a reprint draws the QR locally from the stored URL |
| `ErrorCode`, `ErrorDescription` | `errorCode`, `errorDescription` | `verifactu_error` (only when `ERROR`) |

Not captured: `ExternKey`, `StatusResponse` (the `ResultCode` is used), `Response`, `QrCodeUrl`.

## Error codes

| Code | Meaning | What to do |
|------|---------|-----------|
| `8001` | ServiceKey invalid | Obtain a new key |
| `8002` | A required field is missing (`CompanyName`, `SellerID`) | Configure the issuer's name and NIF |
| `8003` | Duplicate `InvoiceID` | Never re-submit a number; reconcile with Consultar en AEAT |
| `9999` | Internal library error (also the code of a duplicate-rejection record in the list) | Retry; contact support |
| `3002` on cancel | No record to cancel: TESTING purges its records periodically, so submit a fresh invoice first. In production AEAT holds no invoice with that id and date | [README §Common error scenarios](./README.md#common-error-scenarios) |

## Local validation before sending

- `VerifactuTaxItem::isValid()`: `TaxRate >= 0`.
- `VerifactuInvoice::isValid()`: number, date, seller NIF and name not empty; at least one valid tax item. There is no total check.
- Not configured: `INVALID_CONFIG` ("Clave de servicio no configurada. Obténla en https://facturae.irenesolutions.com/verifactu/go" when the key is missing).

## Data flow

```
paid ticket saved (MainWindow)
  -> VerifactuIntegration::submitSimplifiedInvoiceAsync()   (F2)
  -> VerifactuInvoice with the seller from AppSettings + one VerifactuTaxItem; calculateTotals(), isValid()
  -> VerifactuManager::submitInvoiceAsync(): adds ServiceKey, POST /Create
  -> parseVerifactuResponse(): CSV, QrCode, ValidationUrl, Xml, Huella
  -> requestFinished(reqId, VerifactuResult): the caller writes the verifactu_* columns
  -> Imprimir::buildTicket(): QR rastered into the ESC/POS receipt
```

Wait times, adoption of a late reply and the QR on reprint: [README §Waits and in-flight requests](./README.md#waits-and-in-flight-requests).
