# La Ideal
La Ideal laundry software app

## 📋 Características Principales

- ✅ Gestión de prendas y clientes
- ✅ Sistema de contabilidad
- ✅ **NUEVO: Integración Verifactu para emisión de facturas a la AEAT**
- ✅ Generación de reportes
- ✅ Base de datos SQLite local

## 🆕 Verifactu - Nuevo Sistema de Facturas Digitales

A partir de la versión 7.1, LAIDEAL incluye integración completa con **Verifactu**, el sistema de la AEAT para emitir facturas digitales.

### ¿Qué es Verifactu?

Verifactu es el nuevo sistema de la AEAT obligatorio para empresas que no utilizan el SII. Permite emitir facturas digitales en tiempo real directamente a la administración tributaria.

**Fechas de obligatoriedad**:
- 🔹 Empresas: 1 de enero de 2026
- 🔹 Autónomos: 1 de julio de 2026

### Características Implementadas

✅ Envío de facturas a la AEAT
✅ Generación automática de códigos QR
✅ Soporte para todos los tipos de factura (F1, F2, F3, R1)
✅ Entorno de pruebas y producción
✅ Gestión de anulaciones y rectificativas
✅ Manejo robusto de errores

### Cómo Usar Verifactu en LAIDEAL

```cpp
// Enviar factura a la AEAT
VerifactuResult result = verifactuManager->createAndSubmitInvoice(
    "F001",                  // Número de factura
    QDate::currentDate(),    // Fecha
    "B12345678",             // NIF cliente
    "Cliente S.A.",          // Nombre cliente
    100.0,                   // Base imponible
    21.0                     // Tipo IVA
);

if (result.isSuccess()) {
    qDebug() << "CSV:" << result.csv;  // Código de Seguridad
    qDebug() << "QR generado";
}
```

### Documentación Verifactu

- 📖 [README de Verifactu](./src/verifactu/README.md)
- 📚 [Guía Paso a Paso](./src/verifactu/GUIA_PASO_A_PASO.md)
- 💡 [Ejemplos de Implementación](./src/verifactu/EJEMPLO_IMPLEMENTACION.cpp)
- 📋 [Resumen de Integración](./src/verifactu/RESUMEN_IMPLEMENTACION.md)

### Requisitos para Verifactu

1. Obtener **ServiceKey** en: https://facturae.irenesolutions.com/verifactu/go
2. Conexión a Internet
3. Datos correctos de tu empresa (NIF, nombre)

## Deploy application
- [windeployqt](https://medium.com/swlh/how-to-deploy-your-qt-cross-platform-applications-to-windows-operating-system-by-using-windeployqt-a7cd5663d46e)
- [Tutorial to create installer](https://www.youtube.com/watch?v=Y9Ovo2XJHDs)
- Application to create installer: [Inno Setup](https://jrsoftware.org/isinfo.php)

## Release procedure:

1. Update release number in `laideal/CMakeLists.txt`
2. Update release notes
3. Run application in Qt with "*Release*" option
4. Close Qt
5. Open a Qt bash with admin rights
6. `cd C:\Users\gebra\work\tintoreria\laideal\releases`
7. `deploy_laideal_run_in_qt_cmd.bat`
8. Set number of release, for example: "r4.2"
10. Change icon after installed