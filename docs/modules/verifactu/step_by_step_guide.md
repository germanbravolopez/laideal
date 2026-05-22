# Step-by-Step Guide: Verifactu Integration in LAIDEAL

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Preparation](#preparation)
3. [Initial Setup](#initial-setup)
4. [Integration in Receipts and Invoices](#integration-in-receipts-and-invoices)
5. [Testing](#testing)
6. [Going to Production](#going-to-production)
7. [Troubleshooting](#troubleshooting)

---

## Prerequisites

```
- Qt 5.15+ or Qt 6.x
- Qt Network module available
- CMake 3.5+
- C++17-compatible compiler
- Internet access for the REST API
- Verifactu service key (optional for testing)
```

### Obtain a Service Key

1. Go to: https://facturae.irenesolutions.com/verifactu/go
2. Register or log in
3. Generate your **ServiceKey**
4. Keep it safe — you will need it during configuration

---

## Preparation

### Step 1: The module is already in `src/verifactu/`

The main files are ready:

```
src/verifactu/
├── verifactuconfig.h/cpp         configuration and persistence
├── verifactuinvoice.h/cpp        data models
├── verifactumanager.h/cpp        REST API manager
├── verifactuintegration.h/cpp    integration facade
├── CMakeLists.txt                build config
└── implementation_example.cpp    code examples
```

### Step 2: Verify the root CMakeLists.txt includes the module

In `CMakeLists.txt` (root):

```cmake
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql PrintSupport Network)
add_subdirectory(src/verifactu)
```

Already done.

### Step 3: Build the project

```bash
cd c:\Users\gebra\work\tintoreria\laideal
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

---

## Initial Setup

### Step 1: Create the VerifactuIntegration instance

In `src/app/mainwindow.h`, add:

```cpp
#include "../verifactu/verifactuintegration.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    // ... existing members ...
    VerifactuIntegration *m_verifactuIntegration;
};
```

### Step 2: Initialise in the constructor

In `src/app/mainwindow.cpp`:

```cpp
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // ... existing code ...

    // Initialise Verifactu
    m_verifactuIntegration = new VerifactuIntegration(this);

    if (!m_verifactuIntegration->initialize()) {
        QMessageBox::warning(this, tr("Advertencia"),
            QString(tr("No se pudo inicializar Verifactu:\n%1\n\n"
                       "Las facturas se guardarán localmente."))
            .arg(m_verifactuIntegration->getLastError()));
    } else {
        qDebug() << "Verifactu initialised";
        qDebug() << m_verifactuIntegration->getConfigInfo();
    }
}

MainWindow::~MainWindow()
{
    delete ui;
    // m_verifactuIntegration deleted automatically by Qt parent
}
```

---

## Integration in Receipts and Invoices

> **Note**: In LAIDEAL this is already implemented via `verifactuSubmitInvoice()` and `showQrToClient()` in `mainwindow.cpp`. The steps below describe the integration pattern for reference.

### Step 1: The VerifactuIntegration instance already exists

The `m_verifactuIntegration` member from the previous section is already available.

### Step 2: Call Verifactu inside `saveTicket()`

Pattern for integrating into the ticket save flow:

```cpp
void MainWindow::saveTicket()
{
    // validation already done by validateTicket() before this call

    // Submit to Verifactu if configured
    if (m_verifactuIntegration && m_verifactuIntegration->isConfigured()) {

        VerifactuResult result = m_verifactuIntegration->createAndSubmitInvoice(
            ticketNumber,
            ticketDate,
            buyerNIF,
            buyerName,
            taxBase,
            taxRate,
            "Servicio de lavandería"
        );

        if (result.isSuccess()) {
            showQrToClient(result);
        } else if (result.status == VerifactuResult::NETWORK_ERROR) {
            qWarning() << "Verifactu network error:" << result.errorDescription;
            // save ticket locally regardless
        } else {
            qWarning() << "Verifactu error:" << result.errorDescription;
            // save ticket locally regardless
        }
    }

    // Always write to ingresos table regardless of Verifactu result
    // ... DB insert code ...
}
```

### Step 3: Show QR to client after successful submission

Already implemented in `showQrToClient(const VerifactuResult &result)`. It shows a modal dialog with:
- The QR image
- The CSV security code
- A link to the AEAT validation URL

### Step 4: Cancel an invoice

```cpp
void MainWindow::cancelInvoice(const QString &invoiceNumber, QDate invoiceDate)
{
    if (!m_verifactuIntegration || !m_verifactuIntegration->isConfigured()) {
        QMessageBox::warning(this, tr("Error"),
            tr("Verifactu no está configurado"));
        return;
    }

    VerifactuResult result = m_verifactuIntegration->cancelInvoice(
        invoiceNumber,
        invoiceDate
    );

    if (result.isSuccess()) {
        qDebug() << "Invoice cancelled, CSV:" << result.csv;
    } else {
        QMessageBox::critical(this, tr("Error"),
            QString(tr("No se pudo anular la factura:\n%1"))
            .arg(result.errorDescription));
    }
}
```

---

## Testing

### Step 1: Use the TESTING environment

In `src/verifactu/verifactuintegration.cpp`, `loadEmitterConfiguration()`:

```cpp
bool VerifactuIntegration::loadEmitterConfiguration()
{
    // Test data — safe to use, no real AEAT submission
    m_manager->getConfig()->setEmitterData(
        "B72877814",         // test NIF
        "WEFINZ GANDIA SL",  // test company name
        "WEFINZ GANDIA SL"
    );

    m_manager->getConfig()->setServiceKey("YOUR_SERVICE_KEY_HERE");

    // Always use TESTING for development
    m_manager->getConfig()->setEnvironment(VerifactuConfig::TESTING);

    m_manager->getConfig()->save();

    return m_manager->getConfig()->isValid();
}
```

### Step 2: Build and run

```bash
cmake --build . --config Debug
./laideal.exe
```

### Step 3: Test invoice submission

1. Open the invoice or ticket form
2. Fill in all required fields
3. Save
4. Verify that the CSV appears in the success dialog and the QR is shown

---

## Going to Production

**IMPORTANT**: Only switch to PRODUCTION after full validation in TESTING.

### Step 1: Obtain real credentials

- Real company NIF
- Real company name
- Valid ServiceKey from https://facturae.irenesolutions.com/verifactu/go

### Step 2: Update configuration

```cpp
bool VerifactuIntegration::loadEmitterConfiguration()
{
    // Real production data
    m_manager->getConfig()->setEmitterData(
        "B12345678",        // real NIF
        "Mi Empresa Real",  // real company name
        "Mi Empresa Real"
    );

    m_manager->getConfig()->setServiceKey("MY_REAL_SERVICE_KEY");

    // Switch to PRODUCTION
    m_manager->getConfig()->setEnvironment(VerifactuConfig::PRODUCTION);

    m_manager->getConfig()->save();

    return m_manager->getConfig()->isValid();
}
```

### Step 3: Back up the database

```bash
cp laideal.db laideal_backup_$(date +%Y%m%d).db
```

### Step 4: Rebuild in Release mode

```bash
cmake --build . --config Release
```

### Step 5: Final validation

1. Submit one real invoice
2. Validate the CSV on the AEAT portal
3. Verify the QR resolves correctly

---

## Troubleshooting

### Error: "Service key not configured"

```cpp
m_manager->getConfig()->setServiceKey("your_ServiceKey_here");
```

Obtain your key at: https://facturae.irenesolutions.com/verifactu/go

### Error: "Emitter NIF is not valid"

Check the NIF format:
- No spaces
- Format: `B12345678` (company) or `12345678Z` (self-employed)

### Compilation error: header not found

Verify that `src/verifactu/` exists and that CMakeLists.txt includes it:

```cmake
add_subdirectory(src/verifactu)
```

### Error: "Buyer not found" / buyer required

- Use simplified invoice type F2 if the client has no NIF
- Or use reverse charge (operation type S2)

### No response from the API

Possible causes:
1. No internet connection
2. Invalid ServiceKey
3. Wrong environment configured (TESTING vs PRODUCTION)

Debug:
```cpp
// Check connectivity
if (m_verifactuIntegration->getManager()->testConnection())
    qDebug() << "Connected to Verifactu";
else
    qWarning() << "No connection to Verifactu";
```
