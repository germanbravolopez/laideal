# REST API Field Reference — Verifactu

## Overview

This document maps the official Verifactu REST API fields (Irene Solutions) to the LAIDEAL implementation.

---

## Invoice Object

The `Invoice` object is the main input parameter for all operations (submit, cancel, generate QR). The number of required fields varies by operation.

### Status
- **Description**: Indicates the document state
- **Values**:
  - Any value other than `'DRAFT'` → definitive submission to AEAT
  - `'DRAFT'` → draft with XML and QR, no AEAT validation
- **Type**: Alphanumeric (20)
- **Required**: No (defaults to definitive submission)
- **In LAIDEAL**: `'DRAFT'` used for testing, omitted for production

### InvoiceType (REQUIRED)
- **Description**: Invoice type key (list L2)
- **Values**:
  - **F1**: Invoice (art. 6, 7.2 and 7.3 RD 1619/2012) — most common
  - **F2**: Simplified invoice / without buyer identification (art. 6.1.d)
  - **F3**: Invoice substituting simplified invoices
  - **R1**: Corrective invoice (legal error, Art. 80.1.2.6 LIVA)
  - **R2**: Corrective (Art. 80.3) — buyer insolvency
  - **R3**: Corrective (Art. 80.4) — bad debts
  - **R4**: Corrective (other)
  - **R5**: Corrective on simplified invoices
- **Type**: Alphanumeric
- **Default**: F1
- **In LAIDEAL**: Set in `VerifactuInvoice::invoiceTypeToString()`

### RectificationType
- **Description**: Identifies the corrective invoice type (if applicable)
- **Values**:
  - **S**: By substitution (replaces the original invoice)
  - **I**: By differences (adjusts the difference)
- **Type**: Alphanumeric (1)
- **Default**: I
- **Required if**: InvoiceType is R1, R2, R3, R4, or R5

### IsInvoiceFix
- **Description**: Correction of a previously rejected invoice
- **Values**: `false` = normal submission, `true` = correction of a prior submission
- **Type**: Boolean
- **Default**: false
- **In LAIDEAL**: Not implemented (reserved for future)

### IsRejected
- **Description**: Correction with prior rejection
- **Values**: `false` = normal, `true` = correction with prior rejection
- **Type**: Boolean
- **Default**: false
- **In LAIDEAL**: Not implemented (reserved for future)

### InvoiceID (REQUIRED)
- **Description**: Unique invoice identifier (NumSerieFactura)
- **Composition**: Series number + invoice number
- **Example**: `"24417"` or `"SERIE-2026-001"`
- **Type**: Alphanumeric (60)
- **In LAIDEAL**: `m_invoiceNumber` of `VerifactuInvoice`
- **Note**: MUST be unique — do not repeat on the same day

### InvoiceDate (REQUIRED)
- **Description**: Invoice issue date
- **Format**: ISO 8601 (YYYY-MM-DD)
- **Example**: `"2026-05-17"`
- **Type**: Date
- **In LAIDEAL**: `m_invoiceDate` of `VerifactuInvoice`

### SellerID (REQUIRED)
- **Description**: Tax identification number of the issuer
- **Format**: 9-character NIF (FormatoNIF)
- **Example**: `"B12345678"`
- **Type**: Alphanumeric (9)
- **In LAIDEAL**: `m_sellerNIF` of `VerifactuInvoice`
- **Note**: Set in `VerifactuIntegration::loadEmitterConfiguration()`

### CompanyName (REQUIRED)
- **Description**: Seller company name
- **Type**: Alphanumeric (120)
- **Example**: `"LAIDEAL Laundry SL"`
- **In LAIDEAL**: `m_sellerName` of `VerifactuInvoice`
- **CRITICAL**: Omitting this field causes `"El campo CompanyName es obligatorio."`

### RelatedPartyID (CONDITIONAL)
- **Description**: Buyer identifier
- **Required if**: InvoiceType is F1
- **Optional if**: InvoiceType is F2 (end consumer)
- **Valid values**: NIF, VAT number, passport, official document
- **Type**: Alphanumeric (20)
- **Example**: `"12345678A"`
- **In LAIDEAL**: `m_buyerNIF` of `VerifactuInvoice`

### RelatedPartyName (CONDITIONAL)
- **Description**: Buyer company name
- **Required if**: RelatedPartyID is present
- **Type**: Alphanumeric (120)
- **Example**: `"Juan García López"` or `"Cliente SL"`
- **In LAIDEAL**: `m_buyerName` of `VerifactuInvoice`

### RelatedPartyIDType
- **Description**: Buyer identification type key (list L7)
- **Values**:
  - **02**: NIF-IVA (default)
  - **03**: Passport
  - **04**: Official identification document
  - **05**: Residence certificate
  - **06**: Other supporting document
  - **07**: Not registered
- **Type**: Alphanumeric (2)
- **Default**: 02
- **In LAIDEAL**: Not implemented (assumes 02)

### CountryID
- **Description**: Buyer country (ISO-3166)
- **Example**: `"ES"` (Spain), `"FR"` (France)
- **Type**: Alphanumeric (2)
- **In LAIDEAL**: Not implemented (reserved for future)

### Text (OPTIONAL)
- **Description**: Invoice description (DescripcionOperacion)
- **Type**: Alphanumeric (500)
- **Example**: `"Lavado y planchado de trajes"`
- **In LAIDEAL**: `m_description` of `VerifactuInvoice`

### TaxItems (REQUIRED)
- **Description**: Array of invoice tax detail lines
- **Type**: Array of TaxItem
- **Minimum**: At least 1 item
- **In LAIDEAL**: Array of `VerifactuTaxItem`
- **See**: TaxItem Object section below

### RectificationItems
- **Description**: Array of corrected invoices (corrective invoices only)
- **Type**: Array of InvoiceRectification
- **In LAIDEAL**: Not implemented (reserved for future)

### RectificationTaxBase
- **Description**: Corrected tax base for substitution correctives (`'S'`)
- **Type**: Decimal (3,2)
- **In LAIDEAL**: Not implemented

### RectificationTaxAmount
- **Description**: Corrected tax amount (substitution correctives `'S'`)
- **Type**: Decimal (3,2)
- **In LAIDEAL**: Not implemented

### RectificationTaxAmountSurcharge
- **Description**: Corrected equivalence surcharge amount (substitution correctives `'S'`)
- **Type**: Decimal (3,2)
- **In LAIDEAL**: Not implemented

---

## TaxItem Object

Element containing tax line information for an invoice.

### Tax
- **Description**: Tax type (list L1)
- **Values**:
  - **01**: VAT (IVA) — most common
  - **02**: IPSI (Ceuta and Melilla)
  - **03**: IGIC (Canary Islands)
  - **05**: Other
- **Type**: Alphanumeric (1)
- **Default**: 01 (VAT)
- **In LAIDEAL**: `m_taxType` of `VerifactuTaxItem` (currently VAT = 01)

### TaxScheme (REQUIRED)
- **Description**: Tax scheme / regime key (list L8A/L8B)
- **Main values**:
  - **01**: General regime (most common)
  - **02**: Export
  - **03**: Used goods, art, antiques
  - **04**: Investment gold
  - **05**: Travel agencies
  - **07**: Cash accounting
  - **17**: OSS/IOSS
  - **18**: Equivalence surcharge / small business
- **Type**: Alphanumeric (2)
- **Default**: 01
- **In LAIDEAL**: Not implemented (assumes 01 — general regime)

### TaxType (REQUIRED — CRITICAL)
- **Description**: Taxable/exempt operation key (list L9)
- **Values**:
  - **S1**: Taxable and non-exempt — without reverse charge (most common)
  - **S2**: Taxable and non-exempt — with reverse charge
  - **N1**: Not taxable (art. 7, 14, others)
  - **N2**: Not taxable (location rules)
- **Type**: Alphanumeric (2)
- **Default**: S1
- **In LAIDEAL**: `m_operationType` of `VerifactuTaxItem`. Implemented: S1, S2, N1, N2 via `VerifactuTaxItem::operationTypeToString()`

### TaxException
- **Description**: Exemption reason (list L10) — for exempt operations
- **Values**: E1–E6 (art. 20–25 LIVA)
- **Type**: Alphanumeric (2)
- **In LAIDEAL**: Not implemented (reserved for future)

### TaxBase (REQUIRED)
- **Description**: Tax base / non-taxable amount
- **Type**: Decimal (12,2)
- **Example**: `100.00`
- **In LAIDEAL**: `m_taxBase` of `VerifactuTaxItem`
- **Validation**: Must be >= 0

### TaxRate (REQUIRED)
- **Description**: Tax rate (percentage)
- **Type**: Decimal (3,2)
- **Example**: `21.00` (21%)
- **In LAIDEAL**: `m_taxRate` of `VerifactuTaxItem`
- **Validation**: Must be >= 0 (if not exempt)

### TaxAmount (REQUIRED)
- **Description**: Tax amount (CuotaRepercutida)
- **Calculation**: `TaxBase * TaxRate / 100.0`
- **Type**: Decimal (12,2)
- **Example**: `21.00`
- **In LAIDEAL**: `m_taxAmount` of `VerifactuTaxItem`
- **Validation**: `abs(TaxAmount - (TaxBase * TaxRate / 100)) < 0.01`

### TaxRateSurcharge
- **Description**: Equivalence surcharge rate (if applicable)
- **Type**: Decimal (3,2)
- **In LAIDEAL**: Not implemented

### TaxAmountSurcharge
- **Description**: Equivalence surcharge amount
- **Type**: Decimal (12,2)
- **In LAIDEAL**: Not implemented

---

## Result Object (Response)

Object returned by the server for any operation.

### ResultCode
- **Description**: Operation result code
- **Values**: `0` = success; other values = error code
- **Type**: Integer
- **In LAIDEAL**: `result.status` of `VerifactuResult`

### ResultMessage
- **Description**: Descriptive result message
- **Example**: `"Factura procesada correctamente"` or an error message
- **Type**: String
- **In LAIDEAL**: Not currently used (info comes from `Return.ErrorDescription`)

### Return
- **Description**: Object with detailed result properties (see Return Object below)
- **In LAIDEAL**: Processed in `VerifactuManager::processResponse()`

---

## Return Object (Response Detail)

Contains the same Invoice properties plus processing results.

### ExternKey
- **Description**: Blockchain identifier for the invoice
- **Type**: Alphanumeric (20)
- **In LAIDEAL**: Not captured (reserved for future)

### StatusResponse
- **Description**: Status assigned by AEAT
- **Values**: `"Correcto"` = success; others indicate an error
- **Type**: Alphanumeric (20)
- **In LAIDEAL**: Not captured (ResultCode is used instead)

### ErrorCode
- **Description**: AEAT error code (if any)
- **Values**:
  - `null`: No error
  - `9999`: Internal library error
  - Other: Specific AEAT error code
- **Type**: Alphanumeric (100)
- **In LAIDEAL**: `result.errorCode` of `VerifactuResult`

### ErrorDescription
- **Description**: AEAT error description
- **Example**: `"El campo CompanyName es obligatorio."`
- **Type**: Alphanumeric (1000)
- **In LAIDEAL**: `result.errorDescription` of `VerifactuResult`

### CSV (CRITICAL ON SUCCESS)
- **Description**: Security verification code (Código Seguro de Verificación) issued by AEAT
- **Importance**: MUST be captured and shown to the client
- **Type**: Alphanumeric (100)
- **Example**: `"ABCD1234EF"`
- **In LAIDEAL**: `result.csv` of `VerifactuResult`
- **Where to save**: DB column on the ticket/invoice row (pending — see progress tracker)

### QrCode
- **Description**: QR image in BMP format encoded as base64
- **Usage**: Show to client, print on receipt
- **Type**: Bytes (Base64)
- **In LAIDEAL**: `result.qrCode` of `VerifactuResult` (decoded to `QPixmap`)
- **Method**: `VerifactuManager::decodeImageFromBase64()`

### Xml
- **Description**: XML file sent to AEAT
- **Type**: String (XML)
- **In LAIDEAL**: `result.rawResponse` (JSON — Verifactu converts to XML internally)

### Response
- **Description**: XML response file from AEAT
- **Type**: String (XML)
- **In LAIDEAL**: Not captured

### QrCodeUrl
- **Description**: URL to the QR bitmap image
- **Type**: String (URL)
- **In LAIDEAL**: Not captured

### ValidationUrl
- **Description**: Validation URL embedded in the QR code
- **Usage**: Show to client to verify on the AEAT portal
- **Example**: `"https://www.agenciatributaria.es/verifactu?nif=B12345678&..."`
- **Type**: String (URL)
- **In LAIDEAL**: `result.validationUrl` of `VerifactuResult`. Calculated by `VerifactuManager::getValidationUrl()`

---

## FilterSet Object (For Queries)

Input parameter for the submitted-invoices query endpoint.

### Count
- **Description**: Maximum number of records to return
- **Type**: Integer
- **Default**: 5,000
- **In LAIDEAL**: Not implemented (reserved for future)

### Offset
- **Description**: Start position for pagination
- **Type**: Integer
- **Default**: 0
- **Example**: Offset=5000, Count=1000 → records 5001–6000
- **In LAIDEAL**: Not implemented

### OrderClause
- **Description**: SQL ordering clause
- **Example**: `"ORDER BY Created ASC"`
- **Type**: Alphanumeric (200)
- **In LAIDEAL**: Not implemented

### Filters
- **Description**: Array of filter conditions to apply
- **Type**: Array of Filter
- **In LAIDEAL**: Not implemented

---

## Filter Object (Query Conditions)

### FieldName
- **Description**: Name of the field to filter on
- **Example**: `"InvoiceID"`, `"InvoiceDate"`, `"StatusResponse"`
- **Type**: Alphanumeric (64)

### Operator
- **Description**: Comparison operator
- **Examples**: `=`, `LIKE`, `IN`, `>`, `<`, `>=`, `<=`
- **Type**: Alphanumeric (50)

### Value
- **Description**: Value to compare against
- **Type**: Any (String, Number, etc.)

---

## Required Fields by Operation

### Standard submission (submitInvoice)
```
REQUIRED:
  InvoiceType     (F1, F2, etc.)
  InvoiceID       (unique number)
  InvoiceDate     (date)
  SellerID        (emitter NIF)
  CompanyName     (emitter name)
  TaxItems        (array, at least 1 item)

CONDITIONAL (for F1):
  RelatedPartyID  (buyer NIF)
  RelatedPartyName (buyer name)

OPTIONAL:
  Status          (omit or any value other than DRAFT)
  Text            (description)
  CountryID       (buyer country)
  RelatedPartyIDType (default 02)
  TaxScheme       (default 01)
  Tax             (default 01)
```

### QR generation (generateQRCode)
```
REQUIRED: Same as standard submission
OPTIONAL:
  Status = "DRAFT"  (QR without AEAT validation)
```

### Cancellation (cancelInvoice)
```
REQUIRED:
  InvoiceID       (invoice to cancel)
  InvoiceDate     (date)
  SellerID        (NIF)
```

---

## Common Error Codes

| Code | Message | Solution |
|------|---------|----------|
| 8002 | CompanyName required | Check `setEmitterData()` |
| 8002 | SellerID required | Check NIF is configured |
| 8001 | ServiceKey invalid | Obtain a new key |
| 8003 | Duplicate invoice | Generate a unique InvoiceID |
| 9999 | Internal error | Retry; contact support |

---

## Implementation Status in LAIDEAL

| Field | Status | Notes |
|-------|--------|-------|
| Status | Partial | Used for DRAFT in testing |
| InvoiceType | Complete | F1, F2, F3, R1 supported |
| RectificationType | Pending | For future correctives |
| IsInvoiceFix | Pending | For corrections |
| IsRejected | Pending | For corrections with rejection |
| InvoiceID | Complete | |
| InvoiceDate | Complete | |
| SellerID | Complete | |
| CompanyName | Complete | |
| RelatedPartyID | Complete | |
| RelatedPartyName | Complete | |
| RelatedPartyIDType | Partial | Assumes 02 (default) |
| CountryID | Pending | Future |
| Text | Complete | |
| TaxItems | Complete | |
| TaxScheme | Partial | Assumes 01 (general regime) |
| TaxType | Complete | S1, S2, N1, N2 |
| TaxException | Pending | For exempt operations |
| TaxBase | Complete | |
| TaxRate | Complete | |
| TaxAmount | Complete | |
| CSV (Return) | Complete | Captured in `result.csv` |
| ErrorDescription | Complete | Shown to user |
| ValidationUrl | Complete | Calculated and shown |

---

## JSON Structure Sent to the API

### CompanyName (REQUIRED)
- **Source**: `m_sellerName` of `VerifactuInvoice`
- **Set in**: `VerifactuIntegration::loadEmitterConfiguration()`
- **Format**: String
- **Example**: `"Mi Empresa SL"`
- **Error if missing**: `"El campo CompanyName es obligatorio."`

### SellerID (REQUIRED)
- **Source**: `m_sellerNIF` of `VerifactuInvoice`
- **Set in**: `VerifactuIntegration::loadEmitterConfiguration()`
- **Format**: String (9-character NIF)
- **Example**: `"B12345678"`

### ServiceKey (REQUIRED — sent separately)
- **Source**: Loaded from external `.ini` file via `QSettings`
- **Location**: Added to the JSON in `VerifactuManager::submitInvoice()`

```cpp
QJsonObject invoiceJson = invoice.toJson();
invoiceJson["ServiceKey"] = m_config->getServiceKey();
```

---

## Data Flow

```
User creates ticket in MainWindow
    ↓
Extract data (number, date, client, amount, tax)
    ↓
verifactuSubmitInvoice() → VerifactuIntegration::createAndSubmitInvoice()
    ↓
Creates VerifactuInvoice and sets all fields
    ↓
Adds VerifactuTaxItem (base, rate, amount)
    ↓
Calculates totals (calculateTotals())
    ↓
Validates invoice (isValid())
    ↓
Serialises to JSON (toJson())
    ↓
VerifactuManager adds ServiceKey
    ↓
HTTP POST to Verifactu endpoint
    ↓
API responds with ResultCode (0 = success)
    ↓
showQrToClient() displays QR dialog
```

---

## Internal Validations

### VerifactuTaxItem::isValid()
```cpp
TaxBase >= 0
TaxRate >= 0 (if not exempt)
abs(TaxAmount - (TaxBase * TaxRate / 100)) < 0.01
```

### VerifactuInvoice::isValid()
```cpp
invoiceNumber not empty
invoiceDate valid
sellerNIF not empty
sellerName not empty
at least one valid TaxItem
if buyerNIF present → buyerName not empty
totalAmount > 0 (generally)
```

---

## Invoice Type Examples

### F1 — Full Invoice (most common)
```json
{
  "InvoiceType": "F1",
  "SellerID": "B12345678",
  "CompanyName": "Mi Empresa",
  "BuyerID": "12345678A",
  "BuyerName": "Cliente",
  "InvoiceID": "24417",
  "InvoiceDate": "2026-05-17",
  "TaxItems": [...]
}
```

### F2 — Simplified Invoice (no buyer identification)
```json
{
  "InvoiceType": "F2",
  "SellerID": "B12345678",
  "CompanyName": "Mi Empresa",
  "InvoiceID": "24417",
  "InvoiceDate": "2026-05-17",
  "TaxItems": [...]
}
```

---

## Common Errors and Solutions

### "El campo CompanyName es obligatorio"
Verify that `VerifactuIntegration::loadEmitterConfiguration()` calls:
```cpp
m_manager->getConfig()->setEmitterData("B12345678", "Mi Empresa SL", "Mi Empresa");
```

### "InvoiceID duplicado"
Generate a unique invoice ID for each submission. Do not re-submit the same number.

### "BuyerID es obligatorio para tipo F1"
Either provide BuyerID + BuyerName, or change to invoice type F2.

### "TaxBase o TaxAmount inválidos"
Verify: `TaxAmount = TaxBase * TaxRate / 100.0`

---

## Debugging: Log the full JSON

```cpp
// In verifactumanager.cpp, inside submitInvoice():
qDebug() << "JSON sent:" << doc.toJson();
```

---

## References

- **Official specification**: https://facturae.irenesolutions.com/
- **API documentation**: Provided with the ServiceKey
- **Regulation**: Real Decreto 1619/2012 (Verifactu)
