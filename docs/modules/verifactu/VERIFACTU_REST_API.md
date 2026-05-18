# Mapeo de Campos - API Verifactu

## Resumen
Este documento contiene la documentación oficial de la API REST de Verifactu de Irene Solutions, con mapeo a la implementación en LAIDEAL.

## Objeto Invoice

El objeto `Invoice` es el parámetro de entrada principal para todas las operaciones (alta, anulación, generación de QR). El número de parámetros obligatorios varía según la operación.

### Propiedad: Status
- **Descripción**: Indica el estado del documento
- **Valores**:
  - Cualquier valor distinto de 'DRAFT' → Envío definitivo a AEAT
  - 'DRAFT' → Borrador con XML y QR sin validación
- **Tipo**: Alfanumérico (20)
- **Obligatorio**: No (por defecto envío definitivo)
- **En LAIDEAL**: Se usa 'DRAFT' para pruebas, omitido para producción

### Propiedad: InvoiceType (OBLIGATORIO)
- **Descripción**: Clave del tipo de factura (lista L2)
- **Valores admitidos**:
  - **F1**: Factura (art. 6, 7.2 y 7.3 del RD 1619/2012) - MÁS COMÚN
  - **F2**: Factura Simplificada / sin identificación del destinatario (art. 6.1.d)
  - **F3**: Factura en sustitución de facturas simplificadas
  - **R1**: Factura Rectificativa (Error fundado en derecho, Art. 80.1.2.6 LIVA)
  - **R2**: Rectificativa (Art. 80.3) - Insolvencia del destinatario
  - **R3**: Rectificativa (Art. 80.4) - Créditos incobrables
  - **R4**: Rectificativa (Resto)
  - **R5**: Rectificativa en facturas simplificadas
- **Tipo**: Alfanumérico
- **Por defecto**: F1
- **En LAIDEAL**: Configurado en `VerifactuInvoice::invoiceTypeToString()`

### Propiedad: RectificationType
- **Descripción**: Identifica tipo de factura rectificativa (si aplica)
- **Valores**:
  - **S**: Por sustitución (sustituye la factura original)
  - **I**: Por diferencias (ajusta diferencias)
- **Tipo**: Alfanumérico (1)
- **Por defecto**: I
- **Obligatorio si**: InvoiceType es R1, R2, R3, R4 o R5

### Propiedad: IsInvoiceFix
- **Descripción**: Subsanación de factura previamente rechazada
- **Valores**:
  - **false**: Alta normal de factura
  - **true**: Subsanación de factura enviada anteriormente
- **Tipo**: Boolean
- **Por defecto**: false
- **En LAIDEAL**: No implementado (reserved for future)

### Propiedad: IsRejected
- **Descripción**: Subsanación con rechazo previo
- **Valores**:
  - **false**: Alta normal
  - **true**: Subsanación con rechazo previo
- **Tipo**: Boolean
- **Por defecto**: false
- **En LAIDEAL**: No implementado (reserved for future)

### Propiedad: InvoiceID (OBLIGATORIO)
- **Descripción**: Identificador único de la factura (NumSerieFactura)
- **Composición**: Nº Serie + Nº Factura
- **Ejemplo**: "24417" o "SERIE-2026-001"
- **Tipo**: Alfanumérico (60)
- **En LAIDEAL**: `m_invoiceNumber` de `VerifactuInvoice`
- **Nota**: DEBE SER ÚNICO - No repetir en mismo día

### Propiedad: InvoiceDate (OBLIGATORIO)
- **Descripción**: Fecha de expedición de la factura
- **Formato**: ISO 8601 (YYYY-MM-DD)
- **Ejemplo**: "2026-05-17"
- **Tipo**: Date
- **En LAIDEAL**: `m_invoiceDate` de `VerifactuInvoice`

### Propiedad: SellerID (OBLIGATORIO)
- **Descripción**: Número de identificación fiscal del obligado a expedir
- **Formato**: NIF de 9 caracteres (FormatoNIF)
- **Ejemplo**: "B12345678"
- **Tipo**: Alfanumérico (9)
- **En LAIDEAL**: `m_sellerNIF` de `VerifactuInvoice`
- **Nota**: Se obtiene de `VerifactuIntegration::loadEmitterConfiguration()`

### Propiedad: CompanyName (OBLIGATORIO)
- **Descripción**: Nombre-razón social del vendedor
- **Tipo**: Alfanumérico (120)
- **Ejemplo**: "LAIDEAL Laundry SL"
- **En LAIDEAL**: `m_sellerName` de `VerifactuInvoice`
- **CRÍTICO**: Este campo causó error "CompanyName es obligatorio" si faltaba

### Propiedad: RelatedPartyID (CONDICIONAL)
- **Descripción**: Identificador del comprador
- **Obligatorio si**: InvoiceType es F1
- **Opcionales si**: InvoiceType es F2 (consumidor final)
- **Valores válidos**: NIF, VAT Number, pasaporte, documento oficial
- **Tipo**: Alfanumérico (20)
- **Ejemplo**: "12345678A"
- **En LAIDEAL**: `m_buyerNIF` de `VerifactuInvoice`

### Propiedad: RelatedPartyName (CONDICIONAL)
- **Descripción**: Nombre-razón social del comprador
- **Obligatorio si**: RelatedPartyID presente
- **Tipo**: Alfanumérico (120)
- **Ejemplo**: "Juan García López" o "Cliente SL"
- **En LAIDEAL**: `m_buyerName` de `VerifactuInvoice`

### Propiedad: RelatedPartyIDType
- **Descripción**: Clave del tipo de identificación del comprador (lista L7)
- **Valores**:
  - **02**: NIF-IVA (por defecto)
  - **03**: PASAPORTE
  - **04**: DOCUMENTO OFICIAL DE IDENTIFICACIÓN
  - **05**: CERTIFICADO DE RESIDENCIA
  - **06**: OTRO DOCUMENTO PROBATORIO
  - **07**: NO CENSADO
- **Tipo**: Alfanumérico (2)
- **Por defecto**: 02
- **En LAIDEAL**: No implementado (asume 02)

### Propiedad: CountryID
- **Descripción**: País del comprador (ISO-3166)
- **Ejemplo**: "ES" (España), "FR" (Francia)
- **Tipo**: Alfanumérico (2)
- **En LAIDEAL**: No implementado (reserved for future)

### Propiedad: Text (OPCIONAL)
- **Descripción**: Descripción del objeto de la factura (DescripcionOperacion)
- **Tipo**: Alfanumérico (500)
- **Ejemplo**: "Lavado y planchado de trajes"
- **En LAIDEAL**: `m_description` de `VerifactuInvoice`

### Propiedad: TaxItems (OBLIGATORIO)
- **Descripción**: Colección de detalles de impuestos de la factura
- **Tipo**: Array de TaxItem
- **Mínimo**: Al menos 1 item
- **En LAIDEAL**: Array de `VerifactuTaxItem`
- **Ver**: Sección "Objeto TaxItem" abajo

### Propiedad: RectificationItems
- **Descripción**: Colección de facturas rectificadas (solo para rectificativas)
- **Tipo**: Array de InvoiceRectification
- **En LAIDEAL**: No implementado (reserved for future)

### Propiedad: RectificationTaxBase
- **Descripción**: Base rectificada para rectificativas por sustitución 'S'
- **Tipo**: Decimal (3,2)
- **En LAIDEAL**: No implementado

### Propiedad: RectificationTaxAmount
- **Descripción**: Cuota rectificada (rectificativas 'S')
- **Tipo**: Decimal (3,2)
- **En LAIDEAL**: No implementado

### Propiedad: RectificationTaxAmountSurcharge
- **Descripción**: Cuota recargo rectificado (rectificativas 'S')
- **Tipo**: Decimal (3,2)
- **En LAIDEAL**: No implementado

---

## Objeto TaxItem

Elemento que recopila información de líneas de impuesto de la factura.

### Propiedad: Tax
- **Descripción**: Tipo de impuesto (lista L1)
- **Valores**:
  - **01**: Impuesto sobre el Valor Añadido (IVA) - MÁS COMÚN
  - **02**: IPSI de Ceuta y Melilla
  - **03**: IGIC (Canarias)
  - **05**: Otros
- **Tipo**: Alfanumérico (1)
- **Por defecto**: 01 (IVA)
- **En LAIDEAL**: `m_taxType` de `VerifactuTaxItem` (actualmente VAT=01)

### Propiedad: TaxScheme (OBLIGATORIO)
- **Descripción**: Esquema impositivo / Clave de régimen (lista L8A/L8B)
- **Valores principales**:
  - **01**: Régimen general (MÁS COMÚN)
  - **02**: Exportación
  - **03**: Bienes usados, arte, antigüedades
  - **04**: Oro de inversión
  - **05**: Agencias de viajes
  - **07**: Criterio de caja
  - **17**: OSS/IOSS (Nivel avanzado)
  - **18**: Recargo de equivalencia / Pequeño empresario
- **Tipo**: Alfanumérico (2)
- **Por defecto**: 01
- **En LAIDEAL**: No implementado (asume 01 - régimen general)

### Propiedad: TaxType (OBLIGATORIO - CRÍTICO)
- **Descripción**: Clave de operación sujeta/no exenta (lista L9)
- **Valores**:
  - **S1**: Operación Sujeta y No exenta - SIN inversión del sujeto pasivo (MÁS COMÚN)
  - **S2**: Operación Sujeta y No exenta - CON inversión del sujeto pasivo
  - **N1**: Operación No Sujeta (art. 7, 14, otros)
  - **N2**: Operación No Sujeta (reglas de localización)
- **Tipo**: Alfanumérico (2)
- **Por defecto**: S1
- **En LAIDEAL**: `m_operationType` de `VerifactuTaxItem`
  - Implementado: S1, S2, N1, N2
  - Método: `VerifactuTaxItem::operationTypeToString()`

### Propiedad: TaxException
- **Descripción**: Causa de exención (lista L10) - Si la operación es exenta
- **Valores**:
  - **E1**: Exenta por art. 20
  - **E2**: Exenta por art. 21
  - **E3**: Exenta por art. 22
  - **E4**: Exenta por arts. 23 y 24
  - **E5**: Exenta por art. 25
  - **E6**: Exenta por otros
- **Tipo**: Alfanumérico (2)
- **En LAIDEAL**: No implementado (reserved for future)

### Propiedad: TaxBase (OBLIGATORIO)
- **Descripción**: Base imponible / Importe no sujeto
- **Cálculo**: Magnitud dineraria sobre la que se aplica el tipo impositivo
- **Tipo**: Decimal (12,2)
- **Ejemplo**: 100.00
- **En LAIDEAL**: `m_taxBase` de `VerifactuTaxItem`
- **Validación**: Debe ser >= 0

### Propiedad: TaxRate (OBLIGATORIO)
- **Descripción**: Tipo impositivo (porcentaje)
- **Cálculo**: Porcentaje sobre base para calcular cuota
- **Tipo**: Decimal (3,2)
- **Ejemplo**: 21.00 (21%)
- **En LAIDEAL**: `m_taxRate` de `VerifactuTaxItem`
- **Validación**: Debe ser >= 0 (si no es exenta)

### Propiedad: TaxAmount (OBLIGATORIO)
- **Descripción**: Importe cuota impuesto (CuotaRepercutida)
- **Cálculo**: TaxBase * TaxRate / 100.0
- **Tipo**: Decimal (12,2)
- **Ejemplo**: 21.00
- **En LAIDEAL**: `m_taxAmount` de `VerifactuTaxItem`
- **Validación**: abs(TaxAmount - (TaxBase * TaxRate / 100)) < 0.01

### Propiedad: TaxRateSurcharge
- **Descripción**: Tipo de recargo de equivalencia (si aplica)
- **Tipo**: Decimal (3,2)
- **En LAIDEAL**: No implementado

### Propiedad: TaxAmountSurcharge
- **Descripción**: Importe cuota recargo de equivalencia
- **Tipo**: Decimal (12,2)
- **En LAIDEAL**: No implementado

---

## Objeto Result (Respuesta)

Objeto devuelto por el servidor en la respuesta a cualquier operación.

### Propiedad: ResultCode
- **Descripción**: Código de resultado de la operación
- **Valores**:
  - **0**: Éxito - Factura procesada correctamente
  - Otros: Código de error
- **Tipo**: Entero
- **En LAIDEAL**: `result.status` de `VerifactuResult`

### Propiedad: ResultMessage
- **Descripción**: Mensaje descriptivo del resultado
- **Ejemplo**: "Factura procesada correctamente" o mensaje de error
- **Tipo**: String
- **En LAIDEAL**: No usado actualmente (info en Return.ErrorDescription)

### Propiedad: Return
- **Descripción**: Objeto con detalles del resultado
- **Tipo**: Object (contiene propiedades adicionales listadas abajo)
- **En LAIDEAL**: Procesado en `VerifactuManager::processResponse()`

---

## Objeto Return (Detalles de Respuesta)

Contiene las mismas propiedades del Invoice más resultados del procesamiento.

### Propiedad: ExternKey
- **Descripción**: Identificador en blockchain de la factura
- **Tipo**: Alfanumérico (20)
- **En LAIDEAL**: No capturado (reserved for future)

### Propiedad: StatusResponse
- **Descripción**: Estado asignado por AEAT
- **Valores**:
  - **"Correcto"**: Éxito
  - Otros: Indicativo de error
- **Tipo**: Alfanumérico (20)
- **En LAIDEAL**: No capturado (se usa ResultCode)

### Propiedad: ErrorCode
- **Descripción**: Código de error de AEAT (si hay error)
- **Valores especiales**:
  - **null**: Sin errores
  - **9999**: Error interno de librería Verifactu
  - Otros: Código de error específico de AEAT
- **Tipo**: Alfanumérico (100)
- **En LAIDEAL**: `result.errorCode` de `VerifactuResult`

### Propiedad: ErrorDescription
- **Descripción**: Descripción del error de AEAT
- **Ejemplo**: "El campo CompanyName es obligatorio."
- **Tipo**: Alfanumérico (1000)
- **En LAIDEAL**: `result.errorDescription` de `VerifactuResult`

### Propiedad: CSV (CRÍTICO EN ÉXITO)
- **Descripción**: Código Seguro de Verificación generado por AEAT
- **Importancia**: DEBE capturarse y mostrarse al cliente
- **Tipo**: Alfanumérico (100)
- **Ejemplo**: "ABCD1234EF"
- **En LAIDEAL**: `result.csv` de `VerifactuResult`
- **Dónde guardarlo**: Base de datos en tabla de tickets/facturas

### Propiedad: QrCode
- **Descripción**: Imagen QR en formato BMP codificada en base64
- **Uso**: Mostrar al cliente, imprimir en ticket
- **Tipo**: Bytes (Base64)
- **En LAIDEAL**: `result.qrCode` de `VerifactuResult` (QPixmap)
- **Método**: `VerifactuManager::decodeImageFromBase64()`

### Propiedad: Xml
- **Descripción**: Archivo XML enviado a AEAT
- **Tipo**: String (XML)
- **En LAIDEAL**: `result.rawResponse` (JSON, no XML)
- **Nota**: Verifactu convierte JSON a XML internamente

### Propiedad: Response
- **Descripción**: Archivo XML de respuesta de AEAT
- **Tipo**: String (XML)
- **En LAIDEAL**: No capturado

### Propiedad: QrCodeUrl
- **Descripción**: URL del bitmap con código QR
- **Tipo**: String (URL)
- **En LAIDEAL**: No capturado

### Propiedad: ValidationUrl
- **Descripción**: URL de validación contenida en el código QR
- **Uso**: Mostrar al cliente para verificar en portal AEAT
- **Ejemplo**: "https://www.agenciatributaria.es/verifactu?nif=B12345678&..."
- **Tipo**: String (URL)
- **En LAIDEAL**: `result.validationUrl` de `VerifactuResult`
- **Método**: `VerifactuManager::getValidationUrl()`

---

## Objeto FilterSet (Para Consultas)

Parámetro de entrada para endpoint de consulta de envíos enviados.

### Propiedad: Count
- **Descripción**: Número máximo de registros a devolver
- **Tipo**: Entero
- **Por defecto**: 5.000
- **En LAIDEAL**: No implementado (reserved for future)

### Propiedad: Offset
- **Descripción**: Posición de inicio en la lista (paginación)
- **Tipo**: Entero
- **Por defecto**: 0
- **Ejemplo**: Offset=5000, Count=1000 → registros 5001-6000
- **En LAIDEAL**: No implementado

### Propiedad: OrderClause
- **Descripción**: Cláusula de ordenación SQL
- **Ejemplo**: "ORDER BY Created ASC"
- **Tipo**: Alfanumérico (200)
- **En LAIDEAL**: No implementado

### Propiedad: Filters
- **Descripción**: Array de filtros a aplicar
- **Tipo**: Array de Filter
- **En LAIDEAL**: No implementado

---

## Objeto Filter (Condiciones)

Elemento de filtro para consultas.

### Propiedad: FieldName
- **Descripción**: Nombre del campo a filtrar
- **Ejemplo**: "InvoiceID", "InvoiceDate", "StatusResponse"
- **Tipo**: Alfanumérico (64)
- **En LAIDEAL**: No implementado

### Propiedad: Operator
- **Descripción**: Operador de comparación
- **Ejemplos**: =, LIKE, IN, >, <, >=, <=
- **Tipo**: Alfanumérico (50)
- **En LAIDEAL**: No implementado

### Propiedad: Value
- **Descripción**: Valor a comparar
- **Tipo**: Any (String, Number, etc.)
- **En LAIDEAL**: No implementado

---

## Resumen: Campos Requeridos por Operación

### Alta Normal (submitInvoice)
```
REQUERIDOS:
  ✓ InvoiceType (F1, F2, etc.)
  ✓ InvoiceID (número único)
  ✓ InvoiceDate (fecha)
  ✓ SellerID (NIF emisor)
  ✓ CompanyName (nombre emisor)
  ✓ TaxItems (array con al menos 1 item)

CONDICIONAL (si F1):
  ✓ RelatedPartyID (NIF comprador)
  ✓ RelatedPartyName (nombre comprador)

OPCIONAL:
  - Status (omitir o distinto de DRAFT)
  - Text (descripción)
  - CountryID (país comprador)
  - RelatedPartyIDType (defecto 02)
  - TaxScheme (defecto 01)
  - Tax (defecto 01)
```

### Generación de QR (generateQRCode)
```
REQUERIDOS: Mismos que Alta Normal
OPCIONAL:
  - Status = "DRAFT" (para QR sin validación AEAT)
```

### Anulación (cancelInvoice)
```
REQUERIDOS:
  ✓ InvoiceID (factura a anular)
  ✓ InvoiceDate (fecha)
  ✓ SellerID (NIF)
```

---

## Códigos de Error Comunes

| Código | Mensaje | Solución |
|--------|---------|----------|
| 8002 | CompanyName obligatorio | Verificar setEmitterData() |
| 8002 | SellerID obligatorio | Verificar NIF configurado |
| 8001 | ServiceKey no válida | Obtener nueva clave |
| 8003 | Factura duplicada | Generar InvoiceID único |
| 9999 | Error interno | Reintentar, contactar soporte |

---

## Estado de Implementación en LAIDEAL

| Campo | Estado | Notas |
|-------|--------|-------|
| Status | Parcial | Se usa para DRAFT en testing |
| InvoiceType | Completo | F1, F2, F3, R1 soportados |
| RectificationType | Pendiente | Para futuras rectificativas |
| IsInvoiceFix | Pendiente | Para subsanaciones |
| IsRejected | Pendiente | Para subsanaciones con rechazo |
| InvoiceID | Completo | ✓ |
| InvoiceDate | Completo | ✓ |
| SellerID | Completo | ✓ |
| CompanyName | Completo | ✓ FIJO |
| RelatedPartyID | Completo | ✓ |
| RelatedPartyName | Completo | ✓ |
| RelatedPartyIDType | Parcial | Por defecto 02 |
| CountryID | Pendiente | Para futuro |
| Text | Completo | ✓ |
| TaxItems | Completo | ✓ |
| TaxScheme | Parcial | Por defecto 01 |
| TaxType | Completo | S1, S2, N1, N2 ✓ |
| TaxException | Pendiente | Para exenciones |
| TaxBase | Completo | ✓ |
| TaxRate | Completo | ✓ |
| TaxAmount | Completo | ✓ |
| CSV (Return) | Completo | ✓ Se captura y guarda |
| ErrorDescription | Completo | ✓ Se muestra |
| ValidationUrl | Completo | ✓ Se calcula |

---



## Estructura JSON Enviada al API

### Campo: CompanyName (OBLIGATORIO)
- **Fuente**: `m_sellerName` de `VerifactuInvoice`
- **Origen en LAIDEAL**: Se establece en `VerifactuIntegration::loadEmitterConfiguration()`
- **Formato**: String
- **Ejemplo**: "Mi Empresa SL"
- **Error si falta**: `"El campo CompanyName es obligatorio."`

### Campo: SellerID (OBLIGATORIO)
- **Fuente**: `m_sellerNIF` de `VerifactuInvoice`
- **Origen en LAIDEAL**: Se establece en `VerifactuIntegration::loadEmitterConfiguration()`
- **Formato**: String (NIF de 9 caracteres)
- **Ejemplo**: "B12345678"

### Campo: SellerName (COMPATIBILIDAD)
- **Fuente**: `m_sellerName` de `VerifactuInvoice`
- **Nota**: Se incluye además de `CompanyName` para compatibilidad
- **Formato**: String

### Campo: InvoiceID (OBLIGATORIO)
- **Fuente**: `m_invoiceNumber` de `VerifactuInvoice`
- **Origen en LAIDEAL**: Número de ticket/factura del formulario
- **Formato**: String
- **Ejemplo**: "24417"

### Campo: InvoiceDate (OBLIGATORIO)
- **Fuente**: `m_invoiceDate` de `VerifactuInvoice`
- **Origen en LAIDEAL**: Fecha actual o fecha del ticket
- **Formato**: ISO 8601 (YYYY-MM-DD)
- **Ejemplo**: "2026-05-17"

### Campo: InvoiceType (OBLIGATORIO)
- **Fuente**: `m_invoiceType` de `VerifactuInvoice`
- **Valores válidos**:
  - `F1` - Factura completa
  - `F2` - Factura simplificada (sin datos del comprador)
  - `F3` - Factura emitida por tercero
  - `R1` - Factura rectificativa
- **Ejemplo**: "F1"

### Campo: BuyerID (CONDICIONAL)
- **Fuente**: `m_buyerNIF` de `VerifactuInvoice`
- **Obligatorio si**: Tipo de factura es F1
- **Obligatorio si**: Comprador es empresa
- **Formato**: String (NIF o NIE)
- **Ejemplo**: "12345678A"

### Campo: BuyerName (CONDICIONAL)
- **Fuente**: `m_buyerName` de `VerifactuInvoice`
- **Obligatorio si**: `BuyerID` está presente
- **Formato**: String
- **Ejemplo**: "Cliente SL"

### Campo: Text (OPCIONAL)
- **Fuente**: `m_description` de `VerifactuInvoice`
- **Formato**: String
- **Ejemplo**: "Lavado de trajes"
- **Límite**: 2000 caracteres

### Campo: TaxItems (OBLIGATORIO)
- **Fuente**: Array de `VerifactuTaxItem`
- **Estructura**: Array de objetos con:
  - `TaxRate`: Tipo impositivo (%)
  - `TaxBase`: Base imponible
  - `TaxAmount`: Cuota de impuesto
  - `TaxType`: Tipo (VAT, IRPF, etc.)
  - `Description`: Descripción del item (opcional)

### Campo: TotalAmount (OBLIGATORIO)
- **Fuente**: `m_totalAmount` de `VerifactuInvoice`
- **Cálculo**: Suma de bases + total de impuestos
- **Formato**: Double
- **Ejemplo**: 121.00

### Campo: TotalTaxAmount (OBLIGATORIO)
- **Fuente**: `m_totalTaxAmount` de `VerifactuInvoice`
- **Cálculo**: Suma de `TaxAmount` de todos los items
- **Formato**: Double
- **Ejemplo**: 21.00

### Campo: ServiceKey (OBLIGATORIO - ENVIADO SEPARADAMENTE)
- **Fuente**: Cargado desde archivo `~/.verifactu_key` o variable de entorno
- **Ubicación**: Se agrega al JSON en `VerifactuManager::submitInvoice()`
- **Código**:
  ```cpp
  QJsonObject invoiceJson = invoice.toJson();
  invoiceJson["ServiceKey"] = m_config->getServiceKey();
  ```

## Flujo de Datos

```
Usuario crea ticket en mainwindow.cpp
    ↓
Extrae datos (número, fecha, cliente, monto, impuesto)
    ↓
Llama a VerifactuIntegration::createAndSubmitInvoice()
    ↓
Crea VerifactuInvoice y establece todos los campos
    ↓
Agrega VerifactuTaxItem con base, tasa, cuota
    ↓
Calcula totales (calculateTotals())
    ↓
Valida la factura (isValid())
    ↓
Convierte a JSON (toJson())
    ↓
VerifactuManager agrega ServiceKey
    ↓
Envía al endpoint de Verifactu
    ↓
API responde con ResultCode (0=éxito)
```

## Validaciones Internas

### VerifactuTaxItem::isValid()
```cpp
✓ TaxBase >= 0
✓ TaxRate >= 0 (si no es exenta)
✓ abs(TaxAmount - (TaxBase * TaxRate / 100)) < 0.01
```

### VerifactuInvoice::isValid()
```cpp
✓ InvoiceNumber no vacío
✓ InvoiceDate válida
✓ SellerNIF no vacío
✓ SellerName no vacío
✓ Si hay TaxItems, al menos uno válido
✓ Si BuyerID presente, BuyerName no vacío
✓ TotalAmount > 0 (generalmente)
```

## Campos por Tipo de Factura

### F1 - Factura Completa (Más Común)
```json
{
  "InvoiceType": "F1",
  "SellerID": "B12345678",      // ✓ REQUERIDO
  "CompanyName": "Mi Empresa",  // ✓ REQUERIDO
  "BuyerID": "12345678A",       // ✓ REQUERIDO
  "BuyerName": "Cliente",       // ✓ REQUERIDO
  "InvoiceID": "24417",         // ✓ REQUERIDO
  "InvoiceDate": "2026-05-17",  // ✓ REQUERIDO
  "TaxItems": [...]             // ✓ REQUERIDO
}
```

### F2 - Factura Simplificada
```json
{
  "InvoiceType": "F2",
  "SellerID": "B12345678",      // ✓ REQUERIDO
  "CompanyName": "Mi Empresa",  // ✓ REQUERIDO
  "BuyerID": "OMITIDO",         // ✗ OPCIONAL (consumidor)
  "BuyerName": "OMITIDO",       // ✗ OPCIONAL
  "InvoiceID": "24417",         // ✓ REQUERIDO
  "InvoiceDate": "2026-05-17",  // ✓ REQUERIDO
  "TaxItems": [...]             // ✓ REQUERIDO
}
```

### F3 - Factura por Tercero
```json
{
  "InvoiceType": "F3",
  "SellerID": "B12345678",      // Emisor (tercero)
  "CompanyName": "Tercero SL",  // Nombre del tercero
  "BuyerID": "12345678A",       // Quien emite realmente
  // ... otros campos
}
```

### R1 - Rectificativa (Corrección)
```json
{
  "InvoiceType": "R1",
  "SellerID": "B12345678",
  "CompanyName": "Mi Empresa",
  "BuyerID": "12345678A",
  // ... otros campos
  // Nota: Suma o resta cantidad según si es aumento o disminución
}
```

## Errores Comunes y Soluciones

### Error: "El campo CompanyName es obligatorio"
- **Causa**: No se estableció `m_sellerName`
- **Solución**: Verificar que `VerifactuIntegration::loadEmitterConfiguration()` establezca:
  ```cpp
  m_manager->getConfig()->setEmitterData(
      "B12345678",        // NIF
      "Mi Empresa SL",    // ← CompanyName
      "Mi Empresa"
  );
  ```

### Error: "El campo SellerID es obligatorio"
- **Causa**: `m_sellerNIF` vacío
- **Solución**: Mismo como arriba, verificar el NIF

### Error: "InvoiceID duplicado"
- **Causa**: Se intenta enviar la misma factura dos veces
- **Solución**: Generar números únicos para cada factura

### Error: "BuyerID es obligatorio para tipo F1"
- **Causa**: Factura sin datos del comprador
- **Solución**: O proporcionar BuyerID/BuyerName o cambiar a tipo F2

### Error: "TaxBase o TaxAmount inválidos"
- **Causa**: Cálculos de impuestos incorrectos
- **Solución**: Verificar que `TaxAmount = TaxBase * TaxRate / 100.0`

---

## Debugging: Ver JSON Completo

Para ver el JSON exacto que se envía al API:

```cpp
// En verifactumanager.cpp, en submitInvoice():
qDebug() << "JSON enviado:" << doc.toJson();
```

La salida mostrará:
```json
{"BuyerID":"12345678A","BuyerName":"Cliente",...,"CompanyName":"Mi Empresa SL",...}
```

## Referencias

- **Especificación oficial**: https://facturae.irenesolutions.com/
- **Documentación API**: Incluida con la ServiceKey
- **Contacto**: soporte@irenesolutions.com
- **Regulación**: Real Decreto 1619/2012 (Verifactu)

## Control de Versiones

| Versión | Fecha | Cambios |
|---------|-------|---------|
| 1.0 | 2026-05-17 | Creación inicial + CompanyName fix |
| 2.0 | 2026-05-17 | Documentación oficial completa agregada |
