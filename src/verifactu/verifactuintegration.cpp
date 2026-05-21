#include "verifactuintegration.h"
#include "appsettings.h"
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

    if (!loadEmitterConfiguration()) {
        m_lastError = "No se pudo cargar la configuración del emisor";
        qWarning() << "Verifactu emitter configuration failed to load";
        return false;
    }

    qDebug().noquote() << m_manager->getConfigurationInfo();
    return true;
}

VerifactuResult VerifactuIntegration::submitSimplifiedInvoice(
    const QString &invoiceNumber,
    const QDate &invoiceDate,
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
    invoice.setInvoiceType(VerifactuInvoice::SIMPLIFIED);
    invoice.setSellerNIF(m_manager->getConfig()->getEmitterNIF());
    invoice.setSellerName(m_manager->getConfig()->getEmitterName());

    invoice.setDescription(description.isEmpty() ? "Servicio de lavandería" : description);

    VerifactuTaxItem taxItem;
    taxItem.setTaxBase(taxBase);
    taxItem.setTaxRate(taxRate);
    taxItem.setTaxAmount(taxBase * taxRate / 100.0);

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

    invoice.setDescription(description.isEmpty() ? "Servicio de lavandería" : description);

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
    double taxBase,
    double taxRate,
    const QString &description)
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
    invoice.setInvoiceType(VerifactuInvoice::SIMPLIFIED);
    invoice.setSellerNIF(m_manager->getConfig()->getEmitterNIF());
    invoice.setSellerName(m_manager->getConfig()->getEmitterName());
    invoice.setDescription(description.isEmpty() ? "Servicio de lavandería" : description);

    VerifactuTaxItem taxItem;
    taxItem.setTaxBase(taxBase);
    taxItem.setTaxRate(taxRate);
    taxItem.setTaxAmount(taxBase * taxRate / 100.0);

    invoice.addTaxItem(taxItem);
    invoice.calculateTotals();

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
    if (!m_manager) return false;

    AppSettings *settings = AppSettings::instance();
    QString emitterNIF  = settings->verifactuNif();
    QString emitterName = settings->verifactuName();
    QString serviceKey  = settings->verifactuServiceKey();

    if (emitterNIF.isEmpty() || emitterName.isEmpty()) {
        qCritical() << "Emitter NIF or name not configured in AppSettings";
        return false;
    }

    m_manager->getConfig()->setEmitterData(emitterNIF, emitterName, emitterName);
    m_manager->getConfig()->setSystemData("LAIDEAL", QString(PROJECT_VERSION), "LAIDEAL");
    m_manager->getConfig()->setServiceKey(serviceKey);

    VerifactuConfig::Environment env = settings->verifactuProduction()
        ? VerifactuConfig::PRODUCTION
        : VerifactuConfig::TESTING;
    m_manager->getConfig()->setEnvironment(env);

    m_manager->getConfig()->save();

    if (!m_manager->getConfig()->isValid()) {
        qCritical() << "Verifactu configuration is invalid:" << m_manager->getConfig()->getValidationError();
        return false;
    }

    return true;
}
