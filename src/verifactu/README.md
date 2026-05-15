# Integración Verifactu en LAIDEAL

## Descripción General

Este módulo proporciona una integración completa del sistema Verifactu de la AEAT en tu aplicación LAIDEAL. Verifactu es el nuevo sistema para emitir facturas digitales en tiempo real a la administración tributaria española.

## Requisitos

- **Qt 5.15+** o **Qt 6.x** con módulo Network
- **Certificado digital** de la empresa (para producción)
- **Clave de servicio** obtenida en: https://facturae.irenesolutions.com/verifactu/go

## Archivos Incluidos

```
src/verifactu/
├── verifactuconfig.h/cpp          # Configuración de Verifactu
├── verifactuinvoice.h/cpp         # Modelos de factura
├── verifactumanager.h/cpp         # Gestor principal (API REST)
├── verifactuintegration.h/cpp     # Integración de alto nivel
└── CMakeLists.txt                 # Build configuration
```

## Estructura de Clases

### VerifactuConfig
Gestiona toda la configuración necesaria para conectarse con la API REST:
- NIF del emisor
- Clave de servicio
- Entorno (producción/pruebas)
- Datos del sistema informático

### VerifactuInvoice & VerifactuTaxItem
Representan una factura y sus ítems de impuesto:
- Datos de la factura (número, fecha, tipo)
- Información del comprador/vendedor
- Desglose de bases y cuotas impositivas
- Conversión a JSON para API REST

### VerifactuManager
Gestor principal que realiza las operaciones con la API REST:
- Enviar facturas individuales
- Enviar lotes de facturas
- Anular facturas
- Generar códigos QR
- Validar facturas

### VerifactuIntegration
Capa de integración de alto nivel que simplifica el uso desde tu aplicación:
- Métodos simples para crear y enviar facturas
- Gestión de configuración automática
- Manejo de errores

## Pasos de Implementación

### 1. Obtener la Clave de Servicio

1. Accede a: https://facturae.irenesolutions.com/verifactu/go
2. Sigue el proceso de registro
3. Genera tu clave de servicio (ServiceKey)
4. Guarda esta clave en tu configuración

### 2. Integrar en tu Aplicación

#### En tu mainwindow.h:

```cpp
#include "verifactuintegration.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

private:
    VerifactuIntegration *m_verifactuIntegration;
};
```

#### En tu mainwindow.cpp (constructor):

```cpp
#include "verifactuintegration.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Inicializar Verifactu
    m_verifactuIntegration = new VerifactuIntegration(this);

    if (!m_verifactuIntegration->initialize()) {
        qWarning() << "Error al inicializar Verifactu:"
                   << m_verifactuIntegration->getLastError();
    }
}
```

### 3. Usar en tu Módulo de Facturas

#### En src/facturas/facturas.cpp:

```cpp
#include "verifactuintegration.h"

void Facturas::emitirFacturaVerifactu()
{
    // Obtener datos de tu base de datos o formulario
    QString numeroFactura = "F001";
    QDate fechaFactura = QDate::currentDate();
    QString nifCliente = "B12345678";
    QString nombreCliente = "Cliente S.A.";
    double baseImponible = 1000.0;
    double tipoIVA = 21.0;

    // Crear y enviar la factura
    VerifactuResult result = m_verifactuIntegration->createAndSubmitInvoice(
        numeroFactura,
        fechaFactura,
        nifCliente,
        nombreCliente,
        baseImponible,
        tipoIVA,
        "Servicios de lavandería"
    );

    if (result.isSuccess()) {
        // Guardar el CSV en tu base de datos
        QMessageBox::information(this, "Éxito",
            QString("Factura enviada.\nCSV: %1").arg(result.csv));

        // TODO: Guardar result.csv en tu BD
        // guardarCSVEnBD(numeroFactura, result.csv);
    } else {
        QMessageBox::critical(this, "Error",
            QString("Error al enviar factura:\n%1")
            .arg(result.errorDescription));
    }
}

void Facturas::generarQRFactura()
{
    QString numeroFactura = "F001";
    QDate fechaFactura = QDate::currentDate();
    double importeTotal = 1210.0;

    VerifactuResult result = m_verifactuIntegration->generateQR(
        numeroFactura,
        fechaFactura,
        importeTotal
    );

    if (!result.qrCode.isNull()) {
        // Mostrar el QR
        QLabel *labelQR = new QLabel();
        labelQR->setPixmap(result.qrCode);

        // URL para que el cliente valide
        QString urlValidacion = result.validationUrl;
        qDebug() << "Validar en:" << urlValidacion;
    }
}

void Facturas::anuiarFacturaVerifactu()
{
    QString numeroFactura = "F001";
    QDate fechaFactura = QDate::currentDate();

    VerifactuResult result = m_verifactuIntegration->cancelInvoice(
        numeroFactura,
        fechaFactura
    );

    if (result.isSuccess()) {
        QMessageBox::information(this, "Éxito",
            QString("Factura anulada.\nCSV: %1").arg(result.csv));
    } else {
        QMessageBox::critical(this, "Error",
            result.errorDescription);
    }
}
```

### 4. Configurar Datos del Emisor

En `src/verifactu/verifactuintegration.cpp`, método `loadEmitterConfiguration()`:

```cpp
bool VerifactuIntegration::loadEmitterConfiguration()
{
    // Obtener datos de tu BD o configuración
    QString nifEmpresa = obtenerNIFEmpresa();      // Tu método
    QString nombreEmpresa = obtenerNombreEmpresa(); // Tu método
    QString serviceKey = obtenerServiceKey();       // Tu método

    m_manager->getConfig()->setEmitterData(
        nifEmpresa,
        nombreEmpresa,
        nombreEmpresa
    );

    m_manager->getConfig()->setServiceKey(serviceKey);
    m_manager->getConfig()->setEnvironment(VerifactuConfig::TESTING);

    return m_manager->getConfig()->isValid();
}
```

## Configuración de Campos de Factura Especiales

### Factura Simplificada (F2) - Sin datos de cliente
```cpp
VerifactuInvoice invoice;
invoice.setInvoiceType(VerifactuInvoice::SIMPLIFIED);
// No es necesario establecer BuyerNIF ni BuyerName
```

### Factura Rectificativa (R1) - Corrección de factura anterior
```cpp
VerifactuInvoice invoice;
invoice.setInvoiceType(VerifactuInvoice::RECTIFICATION);
// Para rectificación, la base imponible debe ser negativa
```

### Operación con Inversión del Sujeto Pasivo (ISP)
```cpp
VerifactuTaxItem taxItem;
taxItem.setOperationType(VerifactuTaxItem::S2); // Inversión del sujeto pasivo
```

### Operación No Sujeta a IVA
```cpp
VerifactuTaxItem taxItem;
taxItem.setOperationType(VerifactuTaxItem::N1); // Según art. 7, 14 LIVA
taxItem.setTaxRate(0);
```

## Tipos de Régimen Fiscal Soportados

El módulo soporta todos los tipos de régimen fiscal según normativa Verifactu:

- **F1**: Factura normal en régimen general
- **F2**: Factura simplificada
- **F3**: Sustitución de factura simplificada
- **R1**: Factura rectificativa

Y operaciones especiales como:
- Régimen especial agricultura (REAGYP)
- Operaciones intracomunitarias
- Exportaciones
- Entregas a depósitos aduaneros

## Base de Datos - Campos Recomendados

Añade estos campos a tu tabla de facturas para almacenar información de Verifactu:

```sql
ALTER TABLE facturas ADD COLUMN verifactu_csv VARCHAR(50);  -- Código de Seguridad
ALTER TABLE facturas ADD COLUMN verifactu_timestamp DATETIME; -- Timestamp AEAT
ALTER TABLE facturas ADD COLUMN verifactu_estado VARCHAR(20);  -- Estado (Correcto, Error, etc)
ALTER TABLE facturas ADD COLUMN verifactu_error VARCHAR(255); -- Descripción de error
```

## Flujo de Envío

```
Usuario emite factura
    ↓
Datos validados
    ↓
Se crea objeto VerifactuInvoice
    ↓
Se envía a API REST de Verifactu
    ↓
AEAT procesa y devuelve CSV
    ↓
Se guarda CSV en BD
    ↓
Se genera QR para cliente
    ↓
Cliente puede validar en AEAT
```

## Errores Comunes y Soluciones

### Error: "Clave de servicio no configurada"
**Solución**: Obtén la clave en https://facturae.irenesolutions.com/verifactu/go y configúrala

### Error: "NIF del emisor no es válido"
**Solución**: Verifica que el NIF sea correcto (sin espacios, formato válido)

### Error: "Comprador no encontrado en AEAT"
**Solución**: Para clientes particulares, usa "Inv. del sujeto pasivo" (S2) o factura simplificada

### Error: "Ambiente de pruebas vs producción"
**Solución**: En desarrollo usa `TESTING`, en producción usa `PRODUCTION`

## Testing

### Pasos para probar la integración:

1. **En TESTING** (Recomendado primero):
   - Todos los datos son ficticios
   - No se envía nada a la AEAT real
   - Perfecto para desarrollo

```cpp
m_manager->getConfig()->setEnvironment(VerifactuConfig::TESTING);
```

2. **En PRODUCCIÓN**:
   - Los datos se envían a la AEAT real
   - Requiere certificado digital válido
   - Datos verdaderos de la empresa

```cpp
m_manager->getConfig()->setEnvironment(VerifactuConfig::PRODUCTION);
```

## URLs Útiles

- **Portal Verifactu**: https://www.aeat.es/www2/Inicio/inicio.shtml
- **Servicio de Clave**: https://facturae.irenesolutions.com/verifactu/go
- **Documentación API**: https://github.com/mdiago/VeriFactu/wiki
- **Validación QR Producción**: https://www2.aeat.es/wlpl/TIKE-CONT/ValidarQR
- **Validación QR Pruebas**: https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR

## Próximos Pasos

1. ✅ Configurar NIF y nombre de empresa
2. ✅ Obtener clave de servicio
3. ✅ Probar en entorno TESTING
4. ✅ Validar facturas en AEAT
5. ✅ Pasar a producción
6. ✅ Implementar almacenamiento de CSV en BD
7. ✅ Generar informes con códigos QR

## Soporte y Recursos

- **Documentación VeriFactu**: https://github.com/mdiago/VeriFactu/wiki
- **AEAT**: https://www.aeat.es/
- **Mi Aplicación**: Rama `feature/add_mdiago_verifactu`

---

**Versión**: 1.0
**Última actualización**: Abril 2026
**Estado**: En Desarrollo
