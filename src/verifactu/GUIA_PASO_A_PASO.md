# Guía Paso a Paso: Integración de Verifactu en LAIDEAL

## 📋 Tabla de Contenidos

1. [Requisitos Previos](#requisitos-previos)
2. [Preparación](#preparación)
3. [Configuración Inicial](#configuración-inicial)
4. [Integración en MainWindow](#integración-en-mainwindow)
5. [Integración en Facturas](#integración-en-facturas)
6. [Testing](#testing)
7. [Paso a Producción](#paso-a-producción)
8. [Troubleshooting](#troubleshooting)

---

## Requisitos Previos

✅ Qt 5.15+ o Qt 6.x instalado
✅ Módulo Network de Qt disponible
✅ CMake 3.5+
✅ Compilador C++17 compatible
✅ Acceso a Internet para API REST
✅ Clave de servicio Verifactu (opcional para testing)

### Obtener Clave de Servicio

1. Ve a: https://facturae.irenesolutions.com/verifactu/go
2. Regístrate o inicia sesión
3. Genera tu **ServiceKey**
4. Guárdala en un lugar seguro (la necesitarás en la configuración)

---

## Preparación

### Paso 1: El módulo ya está creado en `src/verifactu/`

Los archivos principales ya están listos:

```
src/verifactu/
├── verifactuconfig.h/cpp         ✓ Configuración
├── verifactuinvoice.h/cpp        ✓ Modelos
├── verifactumanager.h/cpp        ✓ Gestor principal
├── verifactuintegration.h/cpp    ✓ Integración
├── CMakeLists.txt                ✓ Build
├── README.md                     ✓ Documentación
└── EJEMPLO_IMPLEMENTACION.cpp    ✓ Ejemplos
```

### Paso 2: Verificar que el CMakeLists.txt principal incluya el módulo

En `CMakeLists.txt` (raíz):

```cmake
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql PrintSupport Network)
add_subdirectory(src/verifactu)
```

✅ Ya está hecho

### Paso 3: Compilar el proyecto

```bash
cd c:\Users\gebra\work\tintoreria\laideal
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

---

## Configuración Inicial

### Paso 1: Crear la instancia de VerifactuIntegration

En `src/app/mainwindow.h`, añade:

```cpp
#include "../verifactu/verifactuintegration.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Tus slots existentes...

private:
    // Tus miembros existentes...
    VerifactuIntegration *m_verifactuIntegration;
};
```

### Paso 2: Inicializar en el constructor

En `src/app/mainwindow.cpp`:

```cpp
#include "../verifactu/verifactuintegration.h"
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // ... tu código existente ...

    // Inicializar Verifactu
    m_verifactuIntegration = new VerifactuIntegration(this);

    if (!m_verifactuIntegration->initialize()) {
        QMessageBox::warning(this, "Advertencia",
            QString("No se pudo inicializar Verifactu:\n%1\n\n"
                    "Las facturas se guardarán localmente.\n"
                    "Configura Verifactu más tarde.")
            .arg(m_verifactuIntegration->getLastError()));
    } else {
        qDebug() << "✓ Verifactu inicializado correctamente";
        qDebug() << m_verifactuIntegration->getConfigInfo();
    }
}

MainWindow::~MainWindow()
{
    delete ui;
    // m_verifactuIntegration se eliminará automáticamente (parent)
}
```

---

## Integración en Facturas

### Paso 1: Actualizar `src/facturas/facturas.h`

```cpp
#include "../verifactu/verifactuintegration.h"
#include <QMainWindow>

class Facturas : public QMainWindow
{
    Q_OBJECT

public:
    explicit Facturas(QWidget *parent = nullptr);
    ~Facturas();

    // Inyectar la instancia de VerifactuIntegration desde MainWindow
    void setVerifactuIntegration(VerifactuIntegration *integration) {
        m_verifactuIntegration = integration;
    }

private slots:
    // Tus slots existentes...
    void on_buttonGuardarFactura_clicked();  // Modificar este
    void on_buttonAnularFactura_clicked();   // Nuevo
    void on_buttonGenerarQR_clicked();       // Nuevo

private:
    VerifactuIntegration *m_verifactuIntegration;

    // Métodos auxiliares
    void guardarFacturaEnBD(const QString &numero, const QString &csv);
    void mostrarQRAlCliente(const VerifactuResult &result);
};
```

### Paso 2: Implementar en `src/facturas/facturas.cpp`

```cpp
#include "facturas.h"
#include "ui_facturas.h"
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

Facturas::Facturas(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::Facturas)
    , m_verifactuIntegration(nullptr)
{
    ui->setupUi(this);
}

Facturas::~Facturas()
{
    delete ui;
}

// Modificar: Guardar factura CON Verifactu
void Facturas::on_buttonGuardarFactura_clicked()
{
    // Validar datos del formulario
    if (!validate_form()) {
        QMessageBox::warning(this, "Error de Validación",
            "Por favor completa todos los campos requeridos");
        return;
    }

    // Obtener datos del formulario
    QString numeroFactura = ui->lineEditNumeroFactura->text();
    QDate fechaFactura = ui->dateEditFecha->date();
    QString nifCliente = ui->lineEditNIFCliente->text();
    QString nombreCliente = ui->lineEditNombreCliente->text();
    double baseImponible = ui->spinBoxBase->value();
    double tipoIVA = 21.0;  // Obtener de combobox si es variable

    // Si Verifactu está disponible, enviar
    if (m_verifactuIntegration && m_verifactuIntegration->isConfigured()) {

        VerifactuResult result = m_verifactuIntegration->createAndSubmitInvoice(
            numeroFactura,
            fechaFactura,
            nifCliente,
            nombreCliente,
            baseImponible,
            tipoIVA,
            "Servicio de lavandería"
        );

        if (result.isSuccess()) {
            // Guardar en BD con CSV
            guardarFacturaEnBD(numeroFactura, result.csv);

            // Mostrar éxito
            QMessageBox::information(this, "Éxito",
                QString("Factura registrada y enviada a AEAT.\n"
                        "CSV: %1").arg(result.csv));

            // Mostrar QR
            mostrarQRAlCliente(result);

        } else if (result.status == VerifactuResult::NETWORK_ERROR) {
            // Si hay error de red, guardar localmente para reintento
            guardarFacturaEnBD(numeroFactura, "");

            QMessageBox::warning(this, "Advertencia de Conectividad",
                QString("Factura guardada localmente.\n"
                        "Error Verifactu: %1\n"
                        "Se reintentará automáticamente.")
                .arg(result.errorDescription));
        } else {
            // Error real en Verifactu
            int respuesta = QMessageBox::question(this, "Error en Verifactu",
                QString("Error al enviar a AEAT: %1\n\n"
                        "¿Deseas guardar la factura localmente?")
                .arg(result.errorDescription),
                QMessageBox::Yes | QMessageBox::No);

            if (respuesta == QMessageBox::Yes) {
                guardarFacturaEnBD(numeroFactura, "");
                QMessageBox::information(this, "Información",
                    "Factura guardada localmente. Puedes intentar enviarla más tarde.");
            }
        }
    } else {
        // Verifactu no disponible, guardar solo localmente
        guardarFacturaEnBD(numeroFactura, "");

        QMessageBox::information(this, "Información",
            "Factura guardada localmente.\n"
            "Verifactu no está disponible.");
    }

    // Limpiar formulario
    reset_all_contents();
}

// Nuevo: Anular factura
void Facturas::on_buttonAnularFactura_clicked()
{
    QString numeroFactura = ui->lineEditNumeroFactura->text();

    if (numeroFactura.isEmpty()) {
        QMessageBox::warning(this, "Error", "Selecciona una factura a anular");
        return;
    }

    if (QMessageBox::question(this, "Confirmar Anulación",
        QString("¿Anular la factura %1?\n"
                "Esta operación es irreversible.")
        .arg(numeroFactura)) != QMessageBox::Yes) {
        return;
    }

    if (!m_verifactuIntegration || !m_verifactuIntegration->isConfigured()) {
        QMessageBox::warning(this, "Error",
            "Verifactu no está configurado");
        return;
    }

    QSqlQuery query;
    query.prepare("SELECT fecha FROM facturas WHERE numero = :numero");
    query.addBindValue(numeroFactura);

    if (!query.exec() || !query.next()) {
        QMessageBox::warning(this, "Error", "Factura no encontrada");
        return;
    }

    QDate fechaFactura = query.value(0).toDate();

    // Anular en Verifactu
    VerifactuResult result = m_verifactuIntegration->cancelInvoice(
        numeroFactura,
        fechaFactura
    );

    if (result.isSuccess()) {
        // Actualizar BD
        QSqlQuery updateQuery;
        updateQuery.prepare("UPDATE facturas SET estado = 'ANULADA', csv_anulacion = :csv "
                           "WHERE numero = :numero");
        updateQuery.addBindValue(result.csv);
        updateQuery.addBindValue(numeroFactura);

        if (updateQuery.exec()) {
            QMessageBox::information(this, "Éxito",
                QString("Factura anulada.\nCSV de anulación: %1")
                .arg(result.csv));
        }
    } else {
        QMessageBox::critical(this, "Error",
            QString("No se pudo anular la factura:\n%1")
            .arg(result.errorDescription));
    }
}

// Nuevo: Generar QR
void Facturas::on_buttonGenerarQR_clicked()
{
    QString numeroFactura = ui->lineEditNumeroFactura->text();
    double importe = ui->spinBoxBase->value() * 1.21;  // Incluir IVA
    QDate fecha = ui->dateEditFecha->date();

    if (numeroFactura.isEmpty()) {
        QMessageBox::warning(this, "Error", "Selecciona una factura");
        return;
    }

    if (!m_verifactuIntegration || !m_verifactuIntegration->isConfigured()) {
        QMessageBox::warning(this, "Error",
            "Verifactu no está configurado");
        return;
    }

    VerifactuResult result = m_verifactuIntegration->generateQR(
        numeroFactura,
        fecha,
        importe
    );

    if (!result.qrCode.isNull()) {
        mostrarQRAlCliente(result);
    } else {
        QMessageBox::warning(this, "Error",
            "No se pudo generar el QR");
    }
}

// Métodos auxiliares
void Facturas::guardarFacturaEnBD(const QString &numero, const QString &csv)
{
    QSqlQuery query;
    query.prepare("INSERT INTO facturas "
                 "(numero, fecha, cliente_nif, cliente_nombre, "
                 "importe_base, csv, estado) "
                 "VALUES (:numero, :fecha, :nif, :nombre, :base, :csv, :estado)");

    query.addBindValue(numero);
    query.addBindValue(ui->dateEditFecha->date());
    query.addBindValue(ui->lineEditNIFCliente->text());
    query.addBindValue(ui->lineEditNombreCliente->text());
    query.addBindValue(ui->spinBoxBase->value());
    query.addBindValue(csv.isEmpty() ? "" : csv);
    query.addBindValue(csv.isEmpty() ? "NO_ENVIADA" : "ENVIADA");

    if (!query.exec()) {
        qWarning() << "Error guardando factura:" << query.lastError().text();
    }
}

void Facturas::mostrarQRAlCliente(const VerifactuResult &result)
{
    QDialog *qrDialog = new QDialog(this);
    qrDialog->setWindowTitle("Código QR de Validación");
    qrDialog->setModal(true);

    QVBoxLayout *layout = new QVBoxLayout();

    // Mostrar QR
    if (!result.qrCode.isNull()) {
        QLabel *labelQR = new QLabel();
        labelQR->setPixmap(result.qrCode.scaledToWidth(300, Qt::SmoothTransformation));
        layout->addWidget(labelQR);
    }

    // Mostrar URL
    if (!result.validationUrl.isEmpty()) {
        QLabel *labelUrl = new QLabel(
            QString("<a href='%1'>Validar en AEAT</a>")
            .arg(result.validationUrl));
        labelUrl->setOpenExternalLinks(true);
        layout->addWidget(labelUrl);
    }

    QPushButton *btnCerrar = new QPushButton("Cerrar");
    connect(btnCerrar, &QPushButton::clicked, qrDialog, &QDialog::accept);
    layout->addWidget(btnCerrar);

    qrDialog->setLayout(layout);
    qrDialog->exec();
    qrDialog->deleteLater();
}
```

### Paso 3: Pasar VerifactuIntegration a Facturas

En `src/app/mainwindow.cpp`, cuando abras Facturas:

```cpp
void MainWindow::abrirFacturas()
{
    Facturas *ventanaFacturas = new Facturas(this);

    // ✓ IMPORTANTE: Pasar la instancia de Verifactu
    ventanaFacturas->setVerifactuIntegration(m_verifactuIntegration);

    ventanaFacturas->show();
}
```

---

## Testing

### Paso 1: Entorno de Pruebas

En `src/verifactu/verifactuintegration.cpp`, método `loadEmitterConfiguration()`:

```cpp
bool VerifactuIntegration::loadEmitterConfiguration()
{
    // TESTING: Usar datos de prueba
    m_manager->getConfig()->setEmitterData(
        "B72877814",        // NIF de prueba
        "WEFINZ GANDIA SL", // Nombre de prueba
        "WEFINZ GANDIA SL"
    );

    m_manager->getConfig()->setServiceKey("TU_SERVICE_KEY_AQUI");

    // IMPORTANTE: Usar TESTING para pruebas
    m_manager->getConfig()->setEnvironment(VerifactuConfig::TESTING);

    m_manager->getConfig()->save();

    return m_manager->getConfig()->isValid();
}
```

### Paso 2: Compilar y Ejecutar

```bash
cmake --build . --config Debug
# Ejecutar la aplicación
./laideal  # Windows: laideal.exe
```

### Paso 3: Probar Emisión de Factura

1. Abre el módulo de Facturas
2. Completa los datos del formulario
3. Haz clic en "Guardar Factura"
4. Verifica que aparezca el CSV en el mensaje de éxito

---

## Paso a Producción

⚠️ **IMPORTANTE**: Solo cuando estés 100% seguro

### Paso 1: Obtener Datos Reales

- NIF real de tu empresa
- Nombre real de la empresa
- ServiceKey válida de https://facturae.irenesolutions.com/verifactu/go

### Paso 2: Actualizar Configuración

```cpp
bool VerifactuIntegration::loadEmitterConfiguration()
{
    // PRODUCCIÓN: Datos reales
    m_manager->getConfig()->setEmitterData(
        "B12345678",        // Tu NIF real
        "Mi Empresa Real",  // Tu nombre real
        "Mi Empresa Real"
    );

    m_manager->getConfig()->setServiceKey("MI_SERVICE_KEY_REAL");

    // CAMBIAR A PRODUCCIÓN
    m_manager->getConfig()->setEnvironment(VerifactuConfig::PRODUCTION);

    m_manager->getConfig()->save();

    return m_manager->getConfig()->isValid();
}
```

### Paso 3: Crear Backup

```bash
# Respalda tu BD antes de cambiar a producción
cp facturas.db facturas_backup_$(date +%Y%m%d).db
```

### Paso 4: Recompilación

```bash
cmake --build . --config Release
```

### Paso 5: Testing en Producción

1. Emite una factura de prueba
2. Valida el CSV en AEAT
3. Verifica que todo funcione

---

## Troubleshooting

### ❌ Error: "Clave de servicio no configurada"

**Solución**:
```cpp
m_manager->getConfig()->setServiceKey("Tu_ServiceKey_aqui");
```

Obtén tu clave en: https://facturae.irenesolutions.com/verifactu/go

### ❌ Error: "NIF del emisor no es válido"

**Solución**: Verifica que el NIF sea correcto
- Sin espacios
- Formato: B12345678 (empresa) o 12345678Z (autónomo)

### ❌ Error de Compilación: "verifactu.h no encontrado"

**Solución**: Verifica que `src/verifactu/` existe y que CMakeLists.txt está actualizado:
```cmake
add_subdirectory(src/verifactu)
```

### ❌ Error: "Comprador no encontrado"

**Solución**:
- Usa factura simplificada (F2) si el cliente no tiene NIF
- O usa Inversión del Sujeto Pasivo (S2)

### ❌ No recibo respuesta de la API

**Posibles causas**:
1. Sin conexión a Internet
2. ServiceKey inválida
3. Endpoint incorrecto (¿TESTING vs PRODUCTION?)

**Solución**:
```cpp
// Verifica conectividad
if (m_verifactuIntegration->getManager()->testConnection()) {
    qDebug() << "✓ Conectado a Verifactu";
} else {
    qWarning() << "✗ Sin conexión";
}
```

---

## 📚 Próximos Pasos

1. ✅ Prueba emisión de facturas
2. ✅ Implementa almacenamiento de CSV
3. ✅ Genera informes con QR
4. ✅ Crea sistema de reintento automático
5. ✅ Valida con la AEAT
6. ✅ Pasa a producción

---

**¿Preguntas?** Consulta el README.md o EJEMPLO_IMPLEMENTACION.cpp

Versión: 1.0 | Fecha: Abril 2026
