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
    // Again after Configuración: one backend at a time (two direct clients would both
    // send the outbox and ignore each other's AEAT wait time).
    delete m_direct;
    delete m_manager;
    m_direct = nullptr;
    m_manager = nullptr;
    m_backend = nullptr;
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
        c.system = AeatRecord::laIdealSystem(QString(PROJECT_VERSION), settings->aeatInstallationNumber());
        c.certificateThumbprint = settings->aeatCertificateThumbprint();
        c.certificatePath       = settings->aeatCertificateFile();
        c.certificatePassword   = settings->aeatCertificatePassword();
        c.readChainMarker  = [] { return AppSettings::instance()->aeatChainMarker(); };
        c.writeChainMarker = [](const QString &marker) { AppSettings::instance()->setAeatChainMarker(marker); };
        m_direct = new AeatDirectBackend(c, db.isValid() ? db : QSqlDatabase::database(QSqlDatabase::defaultConnection, false), this);
        m_backend = m_direct;
        connect(m_direct, &AeatDirectBackend::chainSynced, this, &VerifactuIntegration::chainChecked);
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

    return m_backend->submitInvoiceAsync(simplifiedInvoice(invoiceNumber, invoiceDate, taxBase, taxRate, description));
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

    return m_backend->generateQRAsync(simplifiedInvoice(invoiceNumber, invoiceDate, taxBase, taxRate, description));
}

QPixmap VerifactuIntegration::qrBeforeReply(const QString &invoiceNumber, const QDate &invoiceDate, double taxBase,
                                            double taxRate) const
{
    // Only once the record exists: before the first chain sync it is generated later, or not at all.
    if (!m_direct || !isConfigured())
        return QPixmap();
    return m_direct->queuedRecordQr(simplifiedInvoice(invoiceNumber, invoiceDate, taxBase, taxRate, QString()));
}

VerifactuInvoice VerifactuIntegration::simplifiedInvoice(const QString &invoiceNumber, const QDate &invoiceDate,
                                                         double taxBase, double taxRate,
                                                         const QString &description) const
{
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
    return invoice;
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

int VerifactuIntegration::highestNumberAtAeat() const
{
    // Only the real AEAT: pre-production holds test numbers far above the shop's.
    return m_direct && m_direct->environmentName() == QLatin1String("produccion") ? m_direct->highestNumberAtAeat() : 0;
}

QString VerifactuIntegration::numberingBlockedReason()
{
    if (m_direct && m_direct->firstSyncRunning())
        return tr("La conexión directa está comprobando con la AEAT la cadena de registros (primer arranque, base de "
                  "datos restaurada u ordenador nuevo): espere a que termine para guardar o cobrar tickets.");
    if (!m_direct || !m_direct->chainSyncFailed())
        return QString();
    m_direct->syncChain(nullptr);                                   // try again: chainChecked reports it
    return tr("La conexión directa no ha podido comprobar la cadena de registros con la AEAT (primer arranque, "
              "base de datos restaurada u ordenador nuevo): no se puede guardar ni cobrar ningún ticket hasta "
              "conectar con la AEAT, porque sus números podrían estar ya registrados. Se vuelve a intentar ahora.");
}

QString VerifactuIntegration::invoicingBlockedReason()
{
    if (const QString numbering = numberingBlockedReason(); !numbering.isEmpty())
        return numbering;
    if (isConfigured())
        return QString();
    // Every invoice is a Verifactu record (declaración responsable e): none without it.
    QString missing = m_lastError;
    if (m_direct)
        missing = m_direct->configurationError();
    else if (m_manager)
        missing = m_manager->getConfig()->getValidationError();
    return tr("Verifactu no está configurado (%1): no se puede cobrar ni emitir facturas. Configúrelo en "
              "Archivo → Configuración.")
        .arg(missing.isEmpty() ? tr("falta la configuración") : missing);
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
