# RESUMEN TÉCNICO Y ROADMAP DE VERIFACTU

> **Nota**: Para implementación paso a paso, lee **GUIA_PASO_A_PASO.md**

## 📦 Componentes del Módulo

### Arquitectura

```
VerifactuIntegration (Alto nivel)
    ↓
VerifactuManager (API REST)
    ↓
VerifactuInvoice + VerifactuTaxItem (Modelos)
    ↓
VerifactuConfig (Configuración)
```

### Estructura de Carpetas

```
src/verifactu/
├── verifactuconfig.h/cpp            # Configuración y persistencia
├── verifactuinvoice.h/cpp           # Modelos de datos
├── verifactumanager.h/cpp           # API REST
└── verifactuintegration.h/cpp       # Capa de integración
```

---

## 🔧 Detalles Técnicos

### VerifactuConfig
- **Propósito**: Gestión centralizada de configuración
- **Persistencia**: QSettings (archivo INI)
- **Miembros principales**:
  - `m_serviceKey`: Clave de API
  - `m_emitterNIF`: NIF de la empresa
  - `m_environment`: TESTING o PRODUCTION

### VerifactuInvoice & VerifactuTaxItem
- **Propósito**: Modelos de datos compatibles con API REST
- **Serialización**: JSON automático
- **Tipos soportados**: F1 (Normal), F2 (Simplificada), F3 (Sustitución), R1 (Rectificativa)
- **Operaciones**: S1, S2, N1, N2, Exempt

### VerifactuManager
- **Propósito**: Comunicación con API REST de Verifactu
- **Métodos principales**:
  - `submitInvoice()`: Envío individual
  - `submitInvoices()`: Lote de facturas
  - `cancelInvoice()`: Anulación
  - `generateQRCode()`: Código QR
- **Manejo de errores**: Status enum detallado

### VerifactuIntegration
- **Propósito**: Simplificar uso desde la aplicación
- **Métodos públicos**:
  - `createAndSubmitInvoice()`: Crear y enviar en una llamada
  - `cancelInvoice()`: Anular factura
  - `generateQR()`: Generar QR
  - `initialize()`: Inicialización con validación

---

## 📊 Esquema de Base de Datos

### Campos a Añadir a Tabla `facturas`

```sql
ALTER TABLE facturas ADD COLUMN (
    verifactu_csv VARCHAR(50),
    verifactu_timestamp DATETIME,
    verifactu_estado VARCHAR(20),      -- Ejemplo: ENVIADA, ERROR, ANULADA
    verifactu_error VARCHAR(255),
    verifactu_url_qr VARCHAR(500)
);
```

### Tablas Complementarias (Recomendadas)

#### Tabla de Reintentos
```sql
CREATE TABLE facturas_reintentos (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    numero_factura VARCHAR(50),
    fecha_intento DATETIME,
    numero_intentos INTEGER,
    ultimo_error VARCHAR(255),
    FOREIGN KEY (numero_factura) REFERENCES facturas(numero)
);
```

#### Tabla de Auditoría
```sql
CREATE TABLE verifactu_auditoria (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    accion VARCHAR(50),                -- Ejemplo: ENVIADA, ANULADA, ERROR
    numero_factura VARCHAR(50),
    fecha_accion DATETIME,
    resultado VARCHAR(20),             -- SUCCESS, ERROR, PENDING
    detalles TEXT,
    FOREIGN KEY (numero_factura) REFERENCES facturas(numero)
);
```

---

## 🔐 Consideraciones de Seguridad

### ⚠️ Puntos Críticos

1. **ServiceKey**
   - No incluir en código fuente
   - Almacenar en archivo INI protegido
   - Considerar encriptación QSettings
   - Renovar periódicamente

2. **Datos de Empresa**
   - NIF confidencial
   - Datos bancarios (futuros)
   - Certificado digital

3. **Entornos**
   - TESTING: Datos ficticios, sin impacto real
   - PRODUCTION: Datos reales, envío a AEAT
   - Nunca mezclar datos reales en TESTING

### Recomendaciones

- Usar TESTING para desarrollo y debugging
- Cambiar a PRODUCTION solo después de validación
- Mantener backup de configuración
- Auditar todos los envíos

---

## � Roadmap Futuro

### v1.1 (Próximas semanas)
- [ ] Panel de configuración gráfico en Qt
- [ ] Sistema de sincronización de facturas pendientes
- [ ] Exportación de informes Verifactu
- [ ] Validación de respuestas AEAT mejorada

### v1.2 (Próximos meses)
- [ ] Soporte para múltiples empresas
- [ ] Caché local de facturas
- [ ] Notificaciones en tiempo real
- [ ] Integración con repositorio de facturas

### v2.0 (Futuro)
- [ ] API REST propia para Verifactu
- [ ] Dashboard de análisis
- [ ] Integración avanzada con portales AEAT
- [ ] Machine learning para validación de datos

---

## 🔗 Recursos Técnicos

- **Documentación VeriFactu**: https://github.com/mdiago/VeriFactu/wiki
- **API REST Verifactu**: https://facturae.irenesolutions.com/
- **AEAT Documentation**: https://www.aeat.es/
- **Qt Network Module**: https://doc.qt.io/qt-6/qtnetwork-index.html
- **QJson Docs**: https://doc.qt.io/qt-6/qjson.html

---

## 💡 Patrones de Diseño Utilizados

### 1. **Facade Pattern**
VerifactuIntegration simplifica la complejidad de VerifactuManager

### 2. **Configuration Pattern**
VerifactuConfig centraliza y persiste la configuración

### 3. **Model Pattern**
VerifactuInvoice y VerifactuTaxItem representan datos de dominio

### 4. **Manager Pattern**
VerifactuManager coordina operaciones complejas

---

## 📝 Checklist de Implementación

### Fase 1: Configuración (1-2 horas)
- [ ] Leer GUIA_PASO_A_PASO.md completamente
- [ ] Obtener ServiceKey en portal
- [ ] Configurar NIF de empresa
- [ ] Compilar el proyecto exitosamente

### Fase 2: Integración (2-4 horas)
- [ ] Añadir VerifactuIntegration a MainWindow
- [ ] Integrar en módulo Facturas
- [ ] Implementar métodos en UI
- [ ] Actualizar BD con nuevos campos

### Fase 3: Testing (1-2 horas)
- [ ] Probar con datos de prueba en TESTING
- [ ] Validar QR generados
- [ ] Verificar almacenamiento de CSV

### Fase 4: Producción (30 minutos)
- [ ] Cambiar a PRODUCTION
- [ ] Usar datos reales
- [ ] Ejecutar prueba final
- [ ] Monitorear primeras facturas

---

## ❓ Preguntas Frecuentes

**P: ¿Qué pasa si no tengo ServiceKey?**
A: El módulo funcionará en TESTING sin ella, pero no podrás enviar a producción.

**P: ¿Puedo usar TESTING indefinidamente?**
A: Sí, TESTING es permanente. Es ideal para desarrollo.

**P: ¿Qué sucede con error de red?**
A: El resultado incluye NETWORK_ERROR. Implementa reintentos.

**P: ¿Cómo asegurar facturas no se pierdan?**
A: Almacena localmente primero, luego sincroniza con Verifactu.

---

## 📞 Soporte

### Para Problemas de Implementación
→ Consulta **GUIA_PASO_A_PASO.md** sección "Troubleshooting"

### Para Ejemplos de Código
→ Revisa **EJEMPLO_IMPLEMENTACION.cpp**

### Para Documentación Oficial
→ Visita https://github.com/mdiago/VeriFactu/wiki

---

**Estado**: ✅ Implementación Completa
**Versión**: 1.0
**Última actualización**: Mayo 2026
**Rama**: `feature/add_mdiago_verifactu`
