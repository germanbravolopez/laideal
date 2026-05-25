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

    connect(m_manager, &VerifactuManager::requestFinished,
            this, &VerifactuIntegration::requestFinished);

    qDebug().noquote() << m_manager->getConfigurationInfo();
    return true;
}

QString VerifactuIntegration::submitSimplifiedInvoiceAsync(
    const QString &invoiceNumber,
    const QDate &invoiceDate,
    double taxBase,
    double taxRate,
    const QString &description)
{
    if (!isConfigured()) {
        m_lastError = "Verifactu no está configurado correctamente";
        qWarning() << m_lastError;
        return QString();
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

    return m_manager->submitInvoiceAsync(invoice);
}

QString VerifactuIntegration::cancelInvoiceAsync(
    const QString &invoiceNumber,
    const QDate &invoiceDate)
{
    if (!isConfigured()) {
        m_lastError = "Verifactu no está configurado correctamente";
        qWarning() << m_lastError;
        return QString();
    }
    return m_manager->cancelInvoiceAsync(invoiceNumber, invoiceDate);
}

QString VerifactuIntegration::generateQRAsync(
    const QString &invoiceNumber,
    const QDate &invoiceDate,
    double taxBase,
    double taxRate,
    const QString &description)
{
    if (!isConfigured()) {
        m_lastError = "Verifactu no está configurado correctamente";
        qWarning() << m_lastError;
        return QString();
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

    return m_manager->generateQRAsync(invoice);
}

bool VerifactuIntegration::isConfigured() const
{
    if (!m_manager) return false;
    return m_manager->getConfig()->isValid();
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
