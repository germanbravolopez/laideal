# Integración Verifactu en LAIDEAL

## 📋 ¿Qué es este módulo?

Este módulo integra **Verifactu**, el nuevo sistema de la AEAT para emitir facturas digitales en tiempo real. A partir de 2026, es obligatorio para todas las empresas españolas.

**Está completamente implementado y listo para usar.**

---

## 🚀 Quick Start

1. **Compilar**: El módulo ya está integrado en CMakeLists.txt
2. **Leer**: [GUIA_PASO_A_PASO.md](./GUIA_PASO_A_PASO.md) para implementación completa
3. **Consultar**: [EJEMPLO_IMPLEMENTACION.cpp](./EJEMPLO_IMPLEMENTACION.cpp) para código de ejemplo

---

## 📦 Archivos del Módulo

```
src/verifactu/
├── verifactuconfig.h/cpp         ✓ Configuración
├── verifactuinvoice.h/cpp        ✓ Modelos de factura
├── verifactumanager.h/cpp        ✓ Gestor API REST
├── verifactuintegration.h/cpp    ✓ Integración simplificada
├── CMakeLists.txt                ✓ Configuración de build
├── INDEX.md                      ✓ Mapa de documentación
├── README.md                     ✓ Este archivo (overview)
├── GUIA_PASO_A_PASO.md           ✓ Guía completa de implementación
├── RESUMEN_IMPLEMENTACION.md     ✓ Detalles técnicos y roadmap
└── EJEMPLO_IMPLEMENTACION.cpp    ✓ Ejemplos de código
```

---

## 🎯 Funcionalidades
```
✅ Envío de facturas a la AEAT
✅ Generación de códigos QR
✅ Anulación de facturas
✅ Todos los tipos de factura (F1, F2, F3, R1)
✅ Ambientes TESTING y PRODUCTION
✅ Manejo robusto de errores
✅ Persistencia de configuración
```
---

## 👉 ¿Por dónde empiezo?

### **Start Here**: [INDEX.md](./INDEX.md) - Mapa de documentación

Abre **INDEX.md** para:
- Elegir tu ruta de aprendizaje
- Ver matriz de consulta rápida
- Encontrar respuestas rápidamente

---

## 📚 Documentación

| Documento | Propósito | Tiempo |
|-----------|-----------|--------|
| **INDEX.md** ⭐ | Guía de navegación (LEE ESTO PRIMERO) | 3 min |
| **GUIA_PASO_A_PASO.md** | Implementación completa paso a paso | 45 min |
| **RESUMEN_IMPLEMENTACION.md** | Detalles técnicos, DB schema, roadmap | 20 min |
| **EJEMPLO_IMPLEMENTACION.cpp** | 10+ ejemplos de código práctico | 5-10 min |

---

## 🔗 Enlaces Útiles

- **Obtener ServiceKey**: https://facturae.irenesolutions.com/verifactu/go
- **Documentación oficial**: https://github.com/mdiago/VeriFactu/wiki
- **Portal AEAT**: https://www.aeat.es/
- **Validar QR (Testing)**: https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR
- **Validar QR (Producción)**: https://www2.aeat.es/wlpl/TIKE-CONT/ValidarQR

---

## ✨ Estado

- **Compilación**: ✅ Lista
- **Documentación**: ✅ Completa y sin duplicaciones
- **Testing**: ✅ Ambiente TESTING disponible
- **Producción**: ⏳ Requiere datos reales y ServiceKey

---

## 📖 Estructura de Documentación

Hemos organizado la documentación en 4 documentos complementarios **sin duplicaciones**:

1. **INDEX.md** - Brújula para navegar todos los documentos
2. **GUIA_PASO_A_PASO.md** - Todo lo necesario para implementar (DOCUMENTO PRINCIPAL)
3. **RESUMEN_IMPLEMENTACION.md** - Arquitectura, diseño y roadmap (NO repite GUIA)
4. **EJEMPLO_IMPLEMENTACION.cpp** - Código funcional (NO documentación textual)

---

**¿Listo?** → [Abre INDEX.md](./INDEX.md) 🚀
