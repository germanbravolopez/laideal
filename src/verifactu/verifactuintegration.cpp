#include "verifactuintegration.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <cstdlib>

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

    if (!m_manager) return false;

    // ========================================
    // 1. CARGAR DATOS DEL EMISOR
    // ========================================
    // Estos datos se pueden obtener de:
    // 1. Tu base de datos SQLite
    // 2. Un archivo de configuración INI/JSON
    // 3. Las preferencias de la aplicación
    // 4. Variables de entorno
    //

    QString emitterNIF = "B12345678";           // TODO: NIF real de la empresa
    QString emitterName = "Mi Empresa SL";      // TODO: Nombre real de la empresa
    QString emitterCommercialName = "Mi Empresa";

    // Intentar cargar desde variable de entorno (mejor práctica para desarrollo)
    const char* envNIF = std::getenv("VERIFACTU_NIF");
    if (envNIF) {
        emitterNIF = QString::fromUtf8(envNIF);
        qDebug() << "NIF cargado desde variable de entorno: " << emitterNIF;
    }

    m_manager->getConfig()->setEmitterData(
        emitterNIF,
        emitterName,
        emitterCommercialName
    );

    m_manager->getConfig()->setSystemData(
        "LAIDEAL",
        "7.1",
        "LAIDEAL"
    );

    // ========================================
    // 2. CARGAR LA CLAVE DE SERVICIO
    // ========================================
    // CRÍTICO: La clave debe obtenerse de https://facturae.irenesolutions.com/verifactu/go
    //
    // Opciones de carga (en orden de preferencia):
    // 1. Variable de entorno VERIFACTU_SERVICEKEY (RECOMENDADO)
    // 2. Base de datos (tabla de configuración)
    // 3. Archivo de configuración seguro
    // 4. Archivo .env en el directorio home del usuario
    //

    QString serviceKey;

    // Opción 1: Variable de entorno (MEJOR OPCIÓN para desarrollo y producción)
    const char* envServiceKey = std::getenv("VERIFACTU_SERVICEKEY");
    if (envServiceKey) {
        serviceKey = QString::fromUtf8(envServiceKey);
        qDebug() << "ServiceKey cargada desde variable de entorno";
    }
    // Opción 2: Archivo en el directorio home del usuario (más seguro)
    else {
        QString homeDir = QDir::homePath();
        QString keyFilePath = homeDir + "/.verifactu_key";
        QFile keyFile(keyFilePath);

        if (keyFile.exists()) {
            if (keyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream stream(&keyFile);
                serviceKey = stream.readLine().trimmed();
                keyFile.close();
                qDebug() << "ServiceKey cargada desde archivo:" << keyFilePath;
            } else {
                qWarning() << "No se pudo abrir el archivo de clave:" << keyFilePath;
            }
        } else {
            qWarning() << "Archivo de clave no encontrado en:" << keyFilePath;
            qWarning() << "Instrucciones:";
            qWarning() << "  1. Obtén tu clave en: https://facturae.irenesolutions.com/verifactu/go";
            qWarning() << "  2. Crea un archivo en:" << keyFilePath;
            qWarning() << "  3. Pega tu clave en el archivo (solo la clave, sin espacios)";
            qWarning() << "  O establece la variable de entorno:";
            qWarning() << "  SET VERIFACTU_SERVICEKEY=tu_clave_aqui";
        }
    }

    m_manager->getConfig()->setServiceKey(serviceKey);

    // ========================================
    // 3. ESTABLECER ENTORNO
    // ========================================
    // TESTING para pruebas (sin validación real)
    // PRODUCTION para producción (validación real con AEAT)
    m_manager->getConfig()->setEnvironment(VerifactuConfig::TESTING);

    // Guardar configuración
    m_manager->getConfig()->save();

    // ========================================
    // 4. VALIDAR CONFIGURACIÓN
    // ========================================
    if (!m_manager->getConfig()->isValid()) {
        qCritical() << "Error: Configuración de Verifactu inválida";
        qCritical() << m_manager->getConfig()->getValidationError();
        return false;
    }

    return true;
}
