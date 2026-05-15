#include "verifactuintegration.h"
#include <QDebug>

VerifactuIntegration::VerifactuIntegration(QObject *parent)
    : QObject(parent), m_manager(nullptr)
{
}

VerifactuIntegration::~VerifactuIntegration()
{
}

bool VerifactuIntegration::initialize()
{
    m_manager = new VerifactuManager(QString(), this);

    // Cargar configuración del emisor
    if (!loadEmitterConfiguration()) {
        m_lastError = "No se pudo cargar la configuración del emisor";
        qWarning() << m_lastError;
        return false;
    }

    qDebug() << m_manager->getConfigurationInfo();
    return true;
}

VerifactuResult VerifactuIntegration::createAndSubmitInvoice(
    const QString &invoiceNumber,
    const QDate &invoiceDate,
    const QString &buyerNIF,
    const QString &buyerName,
    double taxBase,
    double taxRate,
    const QString &description)
{
    VerifactuResult result;

    if (!isConfigured()) {
        result.status = VerifactuResult::INVALID_CONFIG;
        result.errorDescription = "Verifactu no está configurado correctamente";
        m_lastError = result.errorDescription;
        return result;
    }

    // Crear la factura
    VerifactuInvoice invoice;
    invoice.setInvoiceNumber(invoiceNumber);
    invoice.setInvoiceDate(invoiceDate);
    invoice.setInvoiceType(VerifactuInvoice::NORMAL);
    invoice.setSellerNIF(m_manager->getConfig()->getEmitterNIF());
    invoice.setSellerName(m_manager->getConfig()->getEmitterName());
    invoice.setBuyerNIF(buyerNIF);
    invoice.setBuyerName(buyerName);

    if (!description.isEmpty()) {
        invoice.setDescription(description);
    } else {
        invoice.setDescription("Operación de venta");
    }

    // Añadir item de impuesto
    VerifactuTaxItem taxItem;
    taxItem.setTaxBase(taxBase);
    taxItem.setTaxRate(taxRate);
    taxItem.setTaxAmount(taxBase * taxRate / 100.0);
    taxItem.setTaxType(VerifactuTaxItem::VAT);
    taxItem.setOperationType(VerifactuTaxItem::S1);

    invoice.addTaxItem(taxItem);
    invoice.calculateTotals();

    // Enviar la factura
    result = m_manager->submitInvoice(invoice);

    if (result.isSuccess()) {
        qDebug() << "Factura enviada exitosamente. CSV:" << result.csv;
    } else {
        m_lastError = result.errorDescription;
        qWarning() << "Error al enviar factura:" << m_lastError;
    }

    return result;
}

VerifactuResult VerifactuIntegration::cancelInvoice(
    const QString &invoiceNumber,
    const QDate &invoiceDate)
{
    VerifactuResult result;

    if (!isConfigured()) {
        result.status = VerifactuResult::INVALID_CONFIG;
        result.errorDescription = "Verifactu no está configurado correctamente";
        return result;
    }

    result = m_manager->cancelInvoice(
        invoiceNumber,
        invoiceDate,
        m_manager->getConfig()->getEmitterNIF()
    );

    if (result.isSuccess()) {
        qDebug() << "Factura anulada exitosamente. CSV:" << result.csv;
    } else {
        m_lastError = result.errorDescription;
        qWarning() << "Error al anular factura:" << m_lastError;
    }

    return result;
}

VerifactuResult VerifactuIntegration::generateQR(
    const QString &invoiceNumber,
    const QDate &invoiceDate,
    double totalAmount)
{
    VerifactuResult result;

    if (!isConfigured()) {
        result.status = VerifactuResult::INVALID_CONFIG;
        result.errorDescription = "Verifactu no está configurado correctamente";
        return result;
    }

    // Crear la factura (mínima información para generar QR)
    VerifactuInvoice invoice;
    invoice.setInvoiceNumber(invoiceNumber);
    invoice.setInvoiceDate(invoiceDate);
    invoice.setSellerNIF(m_manager->getConfig()->getEmitterNIF());
    invoice.setSellerName(m_manager->getConfig()->getEmitterName());
    invoice.setTotalAmount(totalAmount);

    result = m_manager->generateQRCode(invoice);

    if (!result.qrCode.isNull()) {
        qDebug() << "QR generado exitosamente";
        qDebug() << "URL de validación:" << result.validationUrl;
    } else {
        m_lastError = result.errorDescription;
        qWarning() << "Error al generar QR:" << m_lastError;
    }

    return result;
}

QString VerifactuIntegration::getConfigInfo() const
{
    if (!m_manager) {
        return "Gestor de Verifactu no inicializado";
    }
    return m_manager->getConfigurationInfo();
}

bool VerifactuIntegration::isConfigured() const
{
    if (!m_manager) return false;
    return m_manager->getConfig()->isValid();
}

bool VerifactuIntegration::validateEmitterConfiguration()
{
    if (!m_manager) return false;

    return m_manager->getConfig()->getEmitterNIF().isEmpty() ? false : true;
}

bool VerifactuIntegration::loadEmitterConfiguration()
{
    // IMPORTANTE: Este método debe cargar la configuración del emisor desde tu base de datos
    // o desde los datos de configuración de la empresa.
    //
    // Ejemplo si tienes acceso a una clase Database o Settings:
    //
    // QString nif = Database::getInstance()->getCompanyNIF();
    // QString name = Database::getInstance()->getCompanyName();
    // QString serviceKey = Settings::getInstance()->getVerifactuServiceKey();
    //
    // Pero como no tenemos acceso a eso aquí, usamos valores de ejemplo.
    //
    // TODO: Implementar carga real de configuración desde tu BD

    if (!m_manager) return false;

    // Valores de ejemplo - DEBES REEMPLAZAR ESTOS CON DATOS REALES
    // Estos datos se pueden obtener de:
    // 1. Tu base de datos SQLite
    // 2. Un archivo de configuración
    // 3. Las preferencias de la aplicación

    m_manager->getConfig()->setEmitterData(
        "B12345678",        // TODO: NIF real de la empresa
        "Mi Empresa SL",    // TODO: Nombre real de la empresa
        "Mi Empresa"        // Nombre comercial
    );

    m_manager->getConfig()->setSystemData(
        "LAIDEAL",
        "7.1",
        "LAIDEAL"
    );

    // CRÍTICO: Establecer la clave de servicio obtenida en https://facturae.irenesolutions.com/verifactu/go
    m_manager->getConfig()->setServiceKey("");  // TODO: Obtener de configuración

    // Establecer entorno (TESTING para pruebas, PRODUCTION para producción)
    m_manager->getConfig()->setEnvironment(VerifactuConfig::TESTING);

    // Guardar configuración
    m_manager->getConfig()->save();

    return m_manager->getConfig()->isValid();
}
