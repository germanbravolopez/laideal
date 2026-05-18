/**
 * @file EJEMPLO_IMPLEMENTACION.cpp
 *
 * Este archivo muestra ejemplos prácticos de cómo integrar Verifactu en tu aplicación LAIDEAL.
 * NO EJECUTES ESTE CÓDIGO DIRECTAMENTE - Úsalo como referencia.
 */

#include "verifactuintegration.h"
#include "verifactumanager.h"
#include <QDebug>
#include <QDate>
#include <QMessageBox>

// ============================================================================
// EJEMPLO 1: Configuración inicial en tu MainWindow
// ============================================================================

/*
// En mainwindow.h
class MainWindow : public QMainWindow {
    Q_OBJECT

private:
    VerifactuIntegration *m_verifactuIntegration;
};

// En mainwindow.cpp constructor
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Inicializar Verifactu
    m_verifactuIntegration = new VerifactuIntegration(this);

    if (!m_verifactuIntegration->initialize()) {
        QMessageBox::critical(this, "Error de Configuración",
            QString("No se pudo inicializar Verifactu:\n%1")
            .arg(m_verifactuIntegration->getLastError()));
        return;
    }

    // Mostrar información de configuración
    qDebug() << m_verifactuIntegration->getConfigInfo();
}
*/

// ============================================================================
// EJEMPLO 2: Enviar una factura sencilla cuando se recoge una prenda
// ============================================================================

/*
void Facturas::on_prendaRecogida_clicked()
{
    // Datos de la operación (obtenidos del formulario o BD)
    QString numeroFactura = "LAI-2026-001";
    QDate fechaFactura = QDate::currentDate();

    // Cliente que recoge la prenda
    QString nifCliente = "12345678Z";
    QString nombreCliente = "Juan García López";

    // Cálculo del servicio
    double costoServicio = 25.00;      // Euros
    double tipoIVA = 21.0;             // 21%
    double cuotaIVA = costoServicio * tipoIVA / 100.0;
    double importeTotal = costoServicio + cuotaIVA;

    // Enviar a Verifactu
    VerifactuResult result = m_verifactuIntegration->createAndSubmitInvoice(
        numeroFactura,
        fechaFactura,
        nifCliente,
        nombreCliente,
        costoServicio,           // Base imponible
        tipoIVA,                 // Tipo de IVA
        "Servicio de lavandería"
    );

    if (result.isSuccess()) {
        // Guardar en BD
        guardarFacturaEnBD(numeroFactura, result.csv, nifCliente);

        // Mostrar QR al cliente
        mostrarQRAlCliente(numeroFactura, fechaFactura, importeTotal);

        QMessageBox::information(this, "Éxito",
            QString("Prenda registrada.\nCSV Verifactu: %1")
            .arg(result.csv));
    } else {
        QMessageBox::warning(this, "Advertencia",
            QString("Prenda registrada localmente pero:\n"
                    "Error en Verifactu: %1\n\n"
                    "Se reintentará automáticamente.")
            .arg(result.errorDescription));
    }
}

void Facturas::guardarFacturaEnBD(const QString &numero, const QString &csv, const QString &cliente)
{
    QSqlQuery query;
    query.prepare("INSERT INTO facturas (numero, csv, cliente, fecha, estado) "
                  "VALUES (:numero, :csv, :cliente, :fecha, :estado)");
    query.addBindValue(numero);
    query.addBindValue(csv);
    query.addBindValue(cliente);
    query.addBindValue(QDate::currentDate());
    query.addBindValue("ENVIADA");

    if (!query.exec()) {
        qWarning() << "Error guardando factura:" << query.lastError();
    }
}

void Facturas::mostrarQRAlCliente(const QString &numero, const QDate &fecha, double importe)
{
    VerifactuResult result = m_verifactuIntegration->generateQR(numero, fecha, importe);

    if (!result.qrCode.isNull()) {
        // Mostrar QR en un diálogo
        QDialog *qrDialog = new QDialog(this);
        qrDialog->setWindowTitle("Código QR de Validación");

        QLabel *label = new QLabel();
        label->setPixmap(result.qrCode.scaledToWidth(400, Qt::SmoothTransformation));

        QLabel *urlLabel = new QLabel(
            QString("<a href='%1'>Validar en AEAT</a>").arg(result.validationUrl));
        urlLabel->setOpenExternalLinks(true);

        QVBoxLayout *layout = new QVBoxLayout();
        layout->addWidget(label);
        layout->addWidget(urlLabel);

        qrDialog->setLayout(layout);
        qrDialog->exec();
    }
}
*/

// ============================================================================
// EJEMPLO 3: Procesamiento de lote de facturas
// ============================================================================

/*
void Facturas::enviarLoteFacturasVerifactu()
{
    // Obtener todas las facturas no enviadas
    QSqlQuery query("SELECT numero, fecha, cliente_nif, cliente_nombre, importe_base "
                   "FROM facturas WHERE estado = 'NO_ENVIADA' AND fecha >= '2026-01-01'");

    QList<VerifactuInvoice> facturas;

    while (query.next()) {
        VerifactuInvoice invoice;
        invoice.setInvoiceNumber(query.value(0).toString());
        invoice.setInvoiceDate(query.value(1).toDate());
        invoice.setSellerNIF(m_verifactuIntegration->getManager()->getConfig()->getEmitterNIF());
        invoice.setSellerName(m_verifactuIntegration->getManager()->getConfig()->getEmitterName());
        invoice.setBuyerNIF(query.value(2).toString());
        invoice.setBuyerName(query.value(3).toString());

        VerifactuTaxItem taxItem;
        taxItem.setTaxBase(query.value(4).toDouble());
        taxItem.setTaxRate(21.0);
        taxItem.setTaxAmount(query.value(4).toDouble() * 21.0 / 100.0);
        taxItem.setOperationType(VerifactuTaxItem::S1);

        invoice.addTaxItem(taxItem);
        invoice.calculateTotals();
        invoice.setDescription("Servicio de lavandería");

        facturas.append(invoice);
    }

    if (facturas.isEmpty()) {
        QMessageBox::information(this, "Información",
            "No hay facturas pendientes de enviar");
        return;
    }

    // Enviar el lote
    VerifactuResult result = m_verifactuIntegration->getManager()->submitInvoices(facturas);

    if (result.isSuccess()) {
        QMessageBox::information(this, "Éxito",
            QString("Lote de %1 facturas enviado correctamente").arg(facturas.count()));

        // Actualizar estado en BD
        QSqlQuery updateQuery("UPDATE facturas SET estado = 'ENVIADA' "
                             "WHERE estado = 'NO_ENVIADA' AND fecha >= '2026-01-01'");
    } else {
        QMessageBox::critical(this, "Error",
            QString("Error al enviar lote:\n%1").arg(result.errorDescription));
    }
}
*/

// ============================================================================
// EJEMPLO 4: Anular una factura (si hay error)
// ============================================================================

/*
void Facturas::anularFacturaVerifactu(const QString &numeroFactura)
{
    // Obtener datos de BD
    QSqlQuery query;
    query.prepare("SELECT fecha FROM facturas WHERE numero = :numero");
    query.addBindValue(numeroFactura);
    query.exec();

    if (!query.next()) {
        QMessageBox::warning(this, "Error", "Factura no encontrada");
        return;
    }

    QDate fechaFactura = query.value(0).toDate();

    // Confirmar anulación
    if (QMessageBox::question(this, "Confirmar Anulación",
        QString("¿Está seguro de que desea anular la factura %1?\n"
                "Esta operación es irreversible.")
        .arg(numeroFactura)) != QMessageBox::Yes) {
        return;
    }

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
        updateQuery.exec();

        QMessageBox::information(this, "Éxito",
            QString("Factura anulada.\nCSV de anulación: %1").arg(result.csv));
    } else {
        QMessageBox::critical(this, "Error",
            QString("No se pudo anular la factura:\n%1")
            .arg(result.errorDescription));
    }
}
*/

// ============================================================================
// EJEMPLO 5: Factura simplificada (sin datos de cliente)
// ============================================================================

/*
void Facturas::emitirFacturaSimplificada()
{
    // Factura simplificada para clientes sin identificación
    VerifactuInvoice invoice;

    invoice.setInvoiceNumber("TICKET-001");
    invoice.setInvoiceDate(QDate::currentDate());
    invoice.setInvoiceType(VerifactuInvoice::SIMPLIFIED);

    invoice.setSellerNIF(m_verifactuIntegration->getManager()->getConfig()->getEmitterNIF());
    invoice.setSellerName(m_verifactuIntegration->getManager()->getConfig()->getEmitterName());

    // No es necesario establecer datos de comprador para F2

    invoice.setDescription("Servicio de lavandería");

    VerifactuTaxItem taxItem;
    taxItem.setTaxBase(50.0);
    taxItem.setTaxRate(21.0);
    taxItem.setTaxAmount(10.5);

    invoice.addTaxItem(taxItem);
    invoice.calculateTotals();

    // Enviar
    VerifactuResult result = m_verifactuIntegration->getManager()->submitInvoice(invoice);

    if (result.isSuccess()) {
        qDebug() << "Factura simplificada enviada. CSV:" << result.csv;
    }
}
*/

// ============================================================================
// EJEMPLO 6: Operación con Inversión del Sujeto Pasivo (ISP)
// ============================================================================

/*
void Facturas::emitirFacturaISP()
{
    // Operación sujeta a ISP (ej: venta a empresa)
    VerifactuInvoice invoice;

    invoice.setInvoiceNumber("ISP-001");
    invoice.setInvoiceDate(QDate::currentDate());
    invoice.setInvoiceType(VerifactuInvoice::NORMAL);

    invoice.setSellerNIF(m_verifactuIntegration->getManager()->getConfig()->getEmitterNIF());
    invoice.setSellerName(m_verifactuIntegration->getManager()->getConfig()->getEmitterName());

    invoice.setBuyerNIF("B87654321");
    invoice.setBuyerName("Empresa Compradora S.L.");

    VerifactuTaxItem taxItem;
    taxItem.setOperationType(VerifactuTaxItem::S2);  // ISP
    taxItem.setTaxBase(1000.0);
    taxItem.setTaxRate(21.0);
    taxItem.setTaxAmount(210.0);

    invoice.addTaxItem(taxItem);
    invoice.calculateTotals();
    invoice.setDescription("Servicios de consultoría");

    VerifactuResult result = m_verifactuIntegration->getManager()->submitInvoice(invoice);

    if (result.isSuccess()) {
        qDebug() << "Factura ISP enviada. CSV:" << result.csv;
    }
}
*/

// ============================================================================
// EJEMPLO 7: Factura rectificativa
// ============================================================================

/*
void Facturas::emitirFacturaRectificativa(const QString &numeroFacturaOriginal)
{
    // Rectificación por error en factura anterior
    VerifactuInvoice invoice;

    invoice.setInvoiceNumber("RECT-001");
    invoice.setInvoiceDate(QDate::currentDate());
    invoice.setInvoiceType(VerifactuInvoice::RECTIFICATION);

    invoice.setSellerNIF(m_verifactuIntegration->getManager()->getConfig()->getEmitterNIF());
    invoice.setSellerName(m_verifactuIntegration->getManager()->getConfig()->getEmitterName());

    invoice.setBuyerNIF("12345678Z");
    invoice.setBuyerName("Cliente Rectificado");

    // Base NEGATIVA para rectificación
    VerifactuTaxItem taxItem;
    taxItem.setTaxBase(-100.0);        // Reducción de 100€
    taxItem.setTaxRate(21.0);
    taxItem.setTaxAmount(-21.0);       // Reducción de 21€ de IVA

    invoice.addTaxItem(taxItem);
    invoice.calculateTotals();
    invoice.setDescription(QString("Rectificación factura %1 - Error en importe")
                                .arg(numeroFacturaOriginal));

    VerifactuResult result = m_verifactuIntegration->getManager()->submitInvoice(invoice);

    if (result.isSuccess()) {
        qDebug() << "Factura rectificativa enviada. CSV:" << result.csv;
    }
}
*/

// ============================================================================
// EJEMPLO 8: Manejo de errores
// ============================================================================

/*
void Facturas::procesarResultadoVerifactu(const VerifactuResult &result)
{
    switch (result.status) {
        case VerifactuResult::SUCCESS:
            qDebug() << "✓ Operación exitosa";
            qDebug() << "  CSV:" << result.csv;
            break;

        case VerifactuResult::ERROR:
            qWarning() << "✗ Error en AEAT";
            qWarning() << "  Código:" << result.errorCode;
            qWarning() << "  Descripción:" << result.errorDescription;

            // Guardar para reintento automático
            guardarFacturaParaReintento();
            break;

        case VerifactuResult::NETWORK_ERROR:
            qWarning() << "✗ Error de conectividad";
            qWarning() << "  Detalles:" << result.errorDescription;

            // Reintentar más tarde
            programarReintentoAutomatico();
            break;

        case VerifactuResult::INVALID_CONFIG:
            qCritical() << "✗ Configuración inválida";
            qCritical() << "  Error:" << result.errorDescription;

            // Mostrar cuadro de configuración
            mostrarDialogoConfiguracion();
            break;

        case VerifactuResult::PENDING:
            qDebug() << "⏳ Operación pendiente";
            break;
    }
}

void Facturas::guardarFacturaParaReintento()
{
    // Implementar lógica de reintento
    // Guardar en tabla "facturas_pendientes_verifactu"
}

void Facturas::programarReintentoAutomatico()
{
    // Usar QTimer para reintentar después de X segundos
    QTimer::singleShot(30000, this, [this]() {
        // Reintentar envíos pendientes
    });
}
*/

// ============================================================================
// EJEMPLO 9: Consultar información de configuración
// ============================================================================

/*
void MainWindow::mostrarInfoVerifactu()
{
    QString info = m_verifactuIntegration->getConfigInfo();

    QMessageBox::information(this, "Información de Verifactu", info);
}
*/

// ============================================================================
// EJEMPLO 10: Cambiar de entorno (pruebas a producción)
// ============================================================================

/*
void Facturas::cambiarEntorno()
{
    VerifactuManager *manager = m_verifactuIntegration->getManager();

    // Cambiar a producción (CUIDADO!)
    if (QMessageBox::question(this, "Confirmación",
        "¿Desea cambiar a PRODUCCIÓN?\n"
        "Las facturas se enviarán directamente a la AEAT.") == QMessageBox::Yes) {

        manager->getConfig()->setEnvironment(VerifactuConfig::PRODUCTION);
        manager->getConfig()->save();

        QMessageBox::information(this, "Entorno cambiado",
            "Entorno de producción activado.\n"
            "Las próximas facturas se enviarán a AEAT.");
    }
}
*/

// ============================================================================
// INFORMACIÓN ADICIONAL
// ============================================================================

/*
TIPOS DE OPERACIÓN SOPORTADOS:

1. F1 (Normal):
   - Operación estándar
   - Requiere datos de comprador y vendedor

2. F2 (Simplificada):
   - Sin datos de comprador
   - Para consumidores finales
   - Datos mínimos

3. F3 (Sustitución):
   - Sustituye una factura F2
   - Con datos de comprador

4. R1 (Rectificativa):
   - Corrige errores
   - Base imponible NEGATIVA
   - Referencia a factura anterior

OPERACIONES ESPECIALES:

- S1: Operación sujeta (estándar)
- S2: Inversión del sujeto pasivo (ISP)
- N1: No sujeta art. 7, 14 LIVA
- N2: No sujeta reglas de localización
- E1: Exenta

ERRORES COMUNES:

1. "NIF no válido"
   → Verificar formato del NIF
   → Incluir letra si aplica

2. "Comprador no encontrado"
   → Usar factura simplificada (F2)
   → O usar ISP (S2)

3. "Importe no coincide"
   → Verificar cálculo: Base * Tipo = Importe
   → Precisión a 2 decimales

4. "Entorno de producción"
   → En TESTING = datos ficticios OK
   → En PRODUCTION = datos reales, requiere certificado

FLUJO DE ENVÍO:

Factura creada
    ↓
Validación local
    ↓
Envío a API REST
    ↓
AEAT procesa
    ↓
Respuesta con CSV
    ↓
Guardar en BD
    ↓
Generar QR
    ↓
Cliente valida
*/
