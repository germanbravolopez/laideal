#include "verifactuintegration.h"
#include "aeatdirectbackend.h"
#include "appsettings.h"
#include <QDebug>
#include <QSqlDatabase>

VerifactuIntegration::VerifactuIntegration(QObject *parent)
    : QObject(parent)
{
}

VerifactuIntegration::~VerifactuIntegration()
{
}

bool VerifactuIntegration::initialize(const QSqlDatabase &db)
{
    AppSettings *settings = AppSettings::instance();
    m_emitterNif  = settings->verifactuNif();
    m_emitterName = settings->verifactuName();
    if (m_emitterNif.isEmpty() || m_emitterName.isEmpty()) {
        m_lastError = "No se pudo cargar la configuración del emisor";
        qCritical() << "Emitter NIF or name not configured in AppSettings";
        return false;
    }

    if (settings->verifactuDirectAeat()) {
        AeatDirectBackend::Config c;
        // Pre-production unless explicitly enabled on its own (never the gateway's PRODUCCIÓN box).
        c.testEnvironment = !settings->aeatDirectProduction();
        c.issuerNif  = m_emitterNif;
        c.issuerName = m_emitterName;
        // The producer is the one named in the declaración responsable (Acerca de Verifactu).
        c.system = { m_emitterName, m_emitterNif, QStringLiteral("LAIDEAL"), QStringLiteral("LI"),
                     QString(PROJECT_VERSION), settings->aeatInstallationNumber() };
        c.certificateThumbprint = settings->aeatCertificateThumbprint();
        c.certificatePath       = settings->aeatCertificateFile();
        c.certificatePassword   = settings->aeatCertificatePassword();
        m_direct = new AeatDirectBackend(c, db.isValid() ? db : QSqlDatabase::database(QSqlDatabase::defaultConnection, false), this);
        m_backend = m_direct;
    } else {
        m_manager = new VerifactuManager(this);
        if (!loadEmitterConfiguration()) {
            m_lastError = "No se pudo cargar la configuración del emisor";
            qWarning() << "Verifactu emitter configuration failed to load";
            return false;
        }
        m_backend = m_manager;
    }

    connect(m_backend, &VerifactuBackend::requestFinished,
            this, &VerifactuIntegration::requestFinished);
    connect(m_backend, &VerifactuBackend::queryFinished,
            this, &VerifactuIntegration::queryFinished);
    connect(m_backend, &VerifactuBackend::recordSettled,
            this, &VerifactuIntegration::recordSettled);

    qDebug().noquote() << m_backend->configurationInfo();
    if (!m_backend->isConfigured()) {
        m_lastError = m_direct ? m_direct->configurationError() : QStringLiteral("Verifactu no está configurado correctamente");
        return false;
    }
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
    invoice.setSellerNIF(m_emitterNif);
    invoice.setSellerName(m_emitterName);
    invoice.setDescription(description.isEmpty() ? "Servicio de lavandería" : description);

    VerifactuTaxItem taxItem;
    taxItem.setTaxBase(taxBase);
    taxItem.setTaxRate(taxRate);
    taxItem.setTaxAmount(taxBase * taxRate / 100.0);

    invoice.addTaxItem(taxItem);
    invoice.calculateTotals();

    return m_backend->submitInvoiceAsync(invoice);
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
    return m_backend->cancelInvoiceAsync(invoiceNumber, invoiceDate);
}

QString VerifactuIntegration::submitRectificationAsync(
    const QString &newInvoiceNumber,
    const QDate &invoiceDate,
    VerifactuInvoice::InvoiceType invoiceType,
    VerifactuInvoice::RectificationType rectificationType,
    double correctedTaxBase,
    double correctedTaxAmount,
    double originalTaxBase,
    double originalTaxAmount,
    double taxRate,
    const QString &description,
    const QString &rectifiedInvoiceNumber,
    const QDate &rectifiedInvoiceDate)
{
    if (!isConfigured()) {
        m_lastError = "Verifactu no está configurado correctamente";
        qWarning() << m_lastError;
        return QString();
    }
    if (!VerifactuInvoice::isRectificationInvoiceType(invoiceType)) {
        m_lastError = "Tipo de factura no rectificativo";
        qWarning() << m_lastError;
        return QString();
    }

    VerifactuInvoice invoice;
    invoice.setInvoiceNumber(newInvoiceNumber);
    invoice.setInvoiceDate(invoiceDate);
    invoice.setInvoiceType(invoiceType);
    invoice.setSellerNIF(m_emitterNif);
    invoice.setSellerName(m_emitterName);
    invoice.setDescription(description.isEmpty()
        ? "Rectificativa de servicios de lavanderia"
        : description);

    // TaxItems hold the corrected values (substitution: new totals; differences: delta).
    VerifactuTaxItem taxItem;
    taxItem.setTaxBase(correctedTaxBase);
    taxItem.setTaxRate(taxRate);
    taxItem.setTaxAmount(correctedTaxAmount);
    invoice.addTaxItem(taxItem);

    invoice.setRectificationType(rectificationType);
    if (!rectifiedInvoiceNumber.isEmpty())
        invoice.addRectifiedInvoice(rectifiedInvoiceNumber, rectifiedInvoiceDate);
    if (rectificationType == VerifactuInvoice::BY_SUBSTITUTION) {
        invoice.setRectificationTaxBase(originalTaxBase);
        invoice.setRectificationTaxAmount(originalTaxAmount);
    }

    invoice.calculateTotals();
    return m_backend->submitInvoiceAsync(invoice);
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
    invoice.setSellerNIF(m_emitterNif);
    invoice.setSellerName(m_emitterName);
    invoice.setDescription(description.isEmpty() ? "Servicio de lavandería" : description);

    VerifactuTaxItem taxItem;
    taxItem.setTaxBase(taxBase);
    taxItem.setTaxRate(taxRate);
    taxItem.setTaxAmount(taxBase * taxRate / 100.0);

    invoice.addTaxItem(taxItem);
    invoice.calculateTotals();

    return m_backend->generateQRAsync(invoice);
}

QString VerifactuIntegration::queryInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate)
{
    if (!isConfigured()) {
        m_lastError = "Verifactu no está configurado correctamente";
        qWarning() << m_lastError;
        return QString();
    }
    return m_backend->queryInvoiceAsync(invoiceNumber, invoiceDate);
}

bool VerifactuIntegration::isConfigured() const
{
    return m_backend && m_backend->isConfigured();
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

    m_manager->getConfig()->setEmitterData(emitterNIF, emitterName);
    m_manager->getConfig()->setSystemData("LAIDEAL", QString(PROJECT_VERSION));
    m_manager->getConfig()->setServiceKey(serviceKey);

    VerifactuConfig::Environment env = settings->verifactuProduction()
        ? VerifactuConfig::PRODUCTION
        : VerifactuConfig::TESTING;
    m_manager->getConfig()->setEnvironment(env);

    if (!m_manager->getConfig()->isValid()) {
        qCritical() << "Verifactu configuration is invalid:" << m_manager->getConfig()->getValidationError();
        return false;
    }

    return true;
}
