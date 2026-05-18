# Documentación - La Ideal

Índice central de toda la documentación del proyecto.

---

## Estructura

```
docs/
├── README.md                      (este archivo — índice general)
├── modules/
│   └── verifactu/                 (módulo de facturación electrónica AEAT)
│       ├── README.md
│       ├── INDEX.md
│       ├── GUIA_PASO_A_PASO.md
│       ├── RESUMEN_IMPLEMENTACION.md
│       └── VERIFACTU_REST_API.md
├── development/
│   └── planning_verifactu.md      (notas de planificación interna)
└── todo/
    └── hardcoded_paths.png        (captura de incidencia conocida)
```

---

## Módulos

### Verifactu — Facturación electrónica AEAT

Integración con el sistema Verifactu de la AEAT para la emisión de facturas digitales en tiempo real.

| Documento | Descripción | Tiempo estimado |
|-----------|-------------|-----------------|
| [README.md](./modules/verifactu/README.md) | Vista general, quick start, estado | 3 min |
| [INDEX.md](./modules/verifactu/INDEX.md) | Mapa de documentación y rutas de aprendizaje | 3 min |
| [GUIA_PASO_A_PASO.md](./modules/verifactu/GUIA_PASO_A_PASO.md) | Guía completa de implementación | 45 min |
| [RESUMEN_IMPLEMENTACION.md](./modules/verifactu/RESUMEN_IMPLEMENTACION.md) | Arquitectura, DB schema, roadmap | 20 min |
| [VERIFACTU_REST_API.md](./modules/verifactu/VERIFACTU_REST_API.md) | Referencia completa de la API REST | 15 min |
| [EJEMPLO_IMPLEMENTACION.cpp](../src/verifactu/EJEMPLO_IMPLEMENTACION.cpp) | 10+ ejemplos de código práctico | 5-10 min |

---

## Desarrollo

| Documento | Descripción |
|-----------|-------------|
| [planning_verifactu.md](./development/planning_verifactu.md) | Notas de planificación del módulo Verifactu |

---

## Para añadir documentación de un nuevo módulo

1. Crear carpeta `docs/modules/<nombre-modulo>/`
2. Añadir al menos un `README.md` con descripción, archivos y quick start
3. Registrar el módulo en la tabla de "Módulos" de este archivo
