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

    if (!loadEmitterConfiguration()) {
        m_lastError = "No se pudo cargar la configuración del emisor";
        qWarning() << "Verifactu emitter configuration failed to load";
        return false;
    }

    qDebug().noquote() << m_manager->getConfigurationInfo();
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

    VerifactuInvoice invoice;
    invoice.setInvoiceNumber(invoiceNumber);
    invoice.setInvoiceDate(invoiceDate);
    invoice.setInvoiceType(VerifactuInvoice::NORMAL);
    invoice.setSellerNIF(m_manager->getConfig()->getEmitterNIF());
    invoice.setSellerName(m_manager->getConfig()->getEmitterName());
    invoice.setBuyerNIF(buyerNIF);
    invoice.setBuyerName(buyerName);

    invoice.setDescription(description.isEmpty() ? "Operación de venta" : description);

    VerifactuTaxItem taxItem;
    taxItem.setTaxBase(taxBase);
    taxItem.setTaxRate(taxRate);
    taxItem.setTaxAmount(taxBase * taxRate / 100.0);
    taxItem.setTaxType(VerifactuTaxItem::VAT);
    taxItem.setOperationType(VerifactuTaxItem::S1);

    invoice.addTaxItem(taxItem);
    invoice.calculateTotals();

    result = m_manager->submitInvoice(invoice);

    if (result.isSuccess()) {
        qDebug() << "Invoice submitted successfully. CSV:" << result.csv;
    } else {
        m_lastError = result.errorDescription;
        qWarning() << "Invoice submission failed:" << m_lastError;
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
        qDebug() << "Invoice cancelled successfully. CSV:" << result.csv;
    } else {
        m_lastError = result.errorDescription;
        qWarning() << "Invoice cancellation failed:" << m_lastError;
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

    VerifactuInvoice invoice;
    invoice.setInvoiceNumber(invoiceNumber);
    invoice.setInvoiceDate(invoiceDate);
    invoice.setSellerNIF(m_manager->getConfig()->getEmitterNIF());
    invoice.setSellerName(m_manager->getConfig()->getEmitterName());
    invoice.setTotalAmount(totalAmount);

    result = m_manager->generateQRCode(invoice);

    if (!result.qrCode.isNull()) {
        qDebug() << "QR code generated. Validation URL:" << result.validationUrl;
    } else {
        m_lastError = result.errorDescription;
        qWarning() << "QR generation failed:" << m_lastError;
    }

    return result;
}

QString VerifactuIntegration::getConfigInfo() const
{
    if (!m_manager)
        return "Verifactu manager not initialized";
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
    return !m_manager->getConfig()->getEmitterNIF().isEmpty();
}

bool VerifactuIntegration::loadEmitterConfiguration()
{
    // TODO: load emitter data from the app's SQLite config table or QSettings
    if (!m_manager) return false;

    QString emitterNIF = "B12345678";           // TODO: real company NIF
    QString emitterName = "Mi Empresa SL";      // TODO: real company name
    QString emitterCommercialName = "Mi Empresa";

    // Prefer environment variable so secrets stay out of source
    const char* envNIF = std::getenv("VERIFACTU_NIF");
    if (envNIF) {
        emitterNIF = QString::fromUtf8(envNIF);
        qDebug() << "NIF loaded from environment variable:" << emitterNIF;
    }

    m_manager->getConfig()->setEmitterData(emitterNIF, emitterName, emitterCommercialName);
    m_manager->getConfig()->setSystemData("LAIDEAL", QString(PROJECT_VERSION), "LAIDEAL");

    // Load service key — prefer env var, fall back to ~/.verifactu_key
    // Get key from https://facturae.irenesolutions.com/verifactu/go
    QString serviceKey;

    const char* envServiceKey = std::getenv("VERIFACTU_SERVICEKEY");
    if (envServiceKey) {
        serviceKey = QString::fromUtf8(envServiceKey);
        qDebug() << "ServiceKey loaded from environment variable";
    } else {
        QString keyFilePath = QDir::homePath() + "/.verifactu_key";
        QFile keyFile(keyFilePath);

        if (keyFile.exists()) {
            if (keyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream stream(&keyFile);
                serviceKey = stream.readLine().trimmed();
                keyFile.close();
                qDebug() << "ServiceKey loaded from file:" << keyFilePath;
            } else {
                qWarning() << "Could not open key file:" << keyFilePath;
            }
        } else {
            qWarning() << "Key file not found:" << keyFilePath;
            qWarning() << "Instructions:";
            qWarning() << "  1. Get your key at: https://facturae.irenesolutions.com/verifactu/go";
            qWarning() << "  2. Create a file at:" << keyFilePath;
            qWarning() << "  3. Paste your key (key only, no spaces)";
            qWarning() << "  Or set the environment variable: SET VERIFACTU_SERVICEKEY=your_key_here";
        }
    }

    m_manager->getConfig()->setServiceKey(serviceKey);

    // TESTING = no real AEAT validation; PRODUCTION = live validation
    m_manager->getConfig()->setEnvironment(VerifactuConfig::TESTING);

    m_manager->getConfig()->save();

    if (!m_manager->getConfig()->isValid()) {
        qCritical() << "Verifactu configuration is invalid:" << m_manager->getConfig()->getValidationError();
        return false;
    }

    return true;
}
