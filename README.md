# La Ideal
La Ideal laundry management software

## Main Features

- Garment and client management
- Accounting system
- **Verifactu integration for AEAT digital invoicing** (v8.0+)
- Report generation
- Local SQLite database

## Verifactu — Digital Invoice System (v8.0+)

From version 8.0, LAIDEAL includes full integration with **Verifactu**, the mandatory AEAT system for real-time digital invoice submission.

### What is Verifactu?

Verifactu is the AEAT system for businesses not using SII. It allows submitting digital invoices in real time to the tax authority.

**Mandatory from:**
- Businesses: 1 January 2026
- Self-employed: 1 July 2026

### Implemented features

- Submit invoices to AEAT in real time
- Automatic QR code generation
- All invoice types (F1, F2, F3, R1)
- TESTING and PRODUCTION environments
- Invoice cancellation and correction
- Robust error handling

### Using Verifactu in LAIDEAL

```cpp
// Submit an invoice to AEAT
VerifactuResult result = m_verifactuIntegration->createAndSubmitInvoice(
    "F001",                 // invoice number
    QDate::currentDate(),   // date
    "B12345678",            // buyer NIF
    "Cliente SA",           // buyer name
    100.0,                  // tax base
    21.0                    // VAT rate
);

if (result.isSuccess()) {
    qDebug() << "CSV:" << result.csv;  // security code
    // QR shown to client via showQrToClient(result)
}
```

### Verifactu documentation

- [Verifactu module README](./docs/modules/verifactu/README.md)
- [Step-by-step guide](./docs/modules/verifactu/step_by_step_guide.md)
- [REST API reference](./docs/modules/verifactu/rest_api.md)
- [Implementation summary](./docs/modules/verifactu/implementation_summary.md)
- [Code examples](./src/verifactu/implementation_example.cpp)

### Requirements for Verifactu

1. Obtain a **ServiceKey** at: https://facturae.irenesolutions.com/verifactu/go
2. Internet connection
3. Correct company data (NIF, name)

## Documentation

All technical documentation is in the [`docs/`](./docs/README.md) folder.

---

## Deploy application
- [windeployqt](https://medium.com/swlh/how-to-deploy-your-qt-cross-platform-applications-to-windows-operating-system-by-using-windeployqt-a7cd5663d46e)
- [Tutorial to create installer](https://www.youtube.com/watch?v=Y9Ovo2XJHDs)
- Application to create installer: [Inno Setup](https://jrsoftware.org/isinfo.php)

## Release procedure

1. Update release number in `laideal/CMakeLists.txt`
2. Update release notes
3. Run application in Qt with "Release" option
4. Close Qt
5. Open a Qt bash with admin rights
6. `cd C:\Users\gebra\work\tintoreria\laideal\releases`
7. `deploy_laideal_run_in_qt_cmd.bat`
8. Set number of release, for example: "r4.2"
9. Change icon after installed
