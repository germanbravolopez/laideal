#include "verifactuintegration.h"
#include "aeatdirectbackend.h"
#include "appsettings.h"
#include <QDebug>
#include <QNetworkReply>
#include <QTimer>
#include <algorithm>
#include <QSqlDatabase>

VerifactuIntegration::VerifactuIntegration(QObject *parent)
    : QObject(parent)
{
}

VerifactuIntegration::~VerifactuIntegration()
{
}

namespace {
// An old gateway client keeps its signals until the replies it is still waiting for are
// in, then goes: deleting it at once would drop a submit whose outcome is unknown.
void retireManager(VerifactuManager *manager)
{
    if (!manager)
        return;
    auto running = [manager] {
        const auto replies = manager->findChildren<QNetworkReply *>();
        return std::any_of(replies.cbegin(), replies.cend(), [](QNetworkReply *r) { return r->isRunning(); });
    };
    if (!running()) {
        delete manager;
        return;
    }
    auto *poll = new QTimer(manager);
    QObject::connect(poll, &QTimer::timeout, manager, [manager, running] {
        if (!running())
            manager->deleteLater();
    });
    poll->start(200);
}
} // namespace

bool VerifactuIntegration::initialize(const QSqlDatabase &db)
{
    // Again after Configuración: one backend at a time (two direct clients would both
    // send the outbox and ignore each other's AEAT wait time).
    if (m_direct)
        m_direct->releaseWaiters();         // the windows still waiting hear an answer
    delete m_direct;
    retireManager(m_manager);
    m_direct = nullptr;
    m_manager = nullptr;
    m_backend = nullptr;
    VerifactuConfig::removeLegacyIni();
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
        loadEmitterConfiguration();
        m_backend = m_manager;
    }

    m_inFlight.clear();   // a new backend: the old one answered (or released) its waiters
    connect(m_backend, &VerifactuBackend::requestFinished, this,
            [this](const QString &requestId, const VerifactuResult &result) {
        m_inFlight.remove(requestId);
        emit requestFinished(requestId, result);
    });
    connect(m_backend, &VerifactuBackend::queryFinished,
            this, &VerifactuIntegration::queryFinished);
    connect(m_backend, &VerifactuBackend::recordSettled,
            this, &VerifactuIntegration::recordSettled);

    qDebug().noquote() << m_backend->configurationInfo();
    if (!m_backend->isConfigured()) {
        m_lastError = m_direct ? m_direct->configurationError() : m_manager->getConfig()->getValidationError();
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
    // A chained record needs the checked chain, whichever window asks for it.
    if (const QString blocked = numberingBlockedReason(); !blocked.isEmpty()) {
        m_lastError = blocked;
        qWarning() << "VerifactuIntegration:" << __func__ << "refused while the chain is unchecked";
        return QString();
    }
    if (!isConfigured()) {
        m_lastError = "Verifactu no está configurado correctamente";
        qWarning() << m_lastError;
        return QString();
    }

    const QString requestId = m_backend->submitInvoiceAsync(simplifiedInvoice(invoiceNumber, invoiceDate, taxBase, taxRate, description));
    if (!requestId.isEmpty())
        m_inFlight.insert(requestId, invoiceNumber);
    return requestId;
}

bool VerifactuIntegration::isInFlight(const QString &invoiceId) const
{
    for (auto it = m_inFlight.cbegin(); it != m_inFlight.cend(); ++it)
        if (it.value() == invoiceId)
            return true;
    return false;
}

QString VerifactuIntegration::cancelInvoiceAsync(
    const QString &invoiceNumber,
    const QDate &invoiceDate)
{
    // A chained record needs the checked chain, whichever window asks for it.
    if (const QString blocked = numberingBlockedReason(); !blocked.isEmpty()) {
        m_lastError = blocked;
        qWarning() << "VerifactuIntegration:" << __func__ << "refused while the chain is unchecked";
        return QString();
    }
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
    // A chained record needs the checked chain, whichever window asks for it.
    if (const QString blocked = numberingBlockedReason(); !blocked.isEmpty()) {
        m_lastError = blocked;
        qWarning() << "VerifactuIntegration:" << __func__ << "refused while the chain is unchecked";
        return QString();
    }
    if (!isConfigured()) {
        m_lastError = "Verifactu no está configurado correctamente";
        qWarning() << m_lastError;
        return QString();
    }
    // The gateway's rectificativas carry no FacturasRectificadas (RectificationItems): fine in
    // AEAT's pre-production, never as a real invoice. The direct client sends them.
    if (m_manager && m_manager->getConfig()->getEnvironment() == VerifactuConfig::PRODUCTION) {
        m_lastError = tr("Con IreneSolutions las rectificativas no se envían en PRODUCCIÓN (no indican la factura "
                         "que rectifican). Use la conexión directa con la AEAT.");
        qWarning() << "VerifactuIntegration:" << __func__ << "refused with the gateway in production";
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
        ? kRectificationDescription
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
    invoice.setDescription(description.isEmpty() ? kInvoiceDescription : description);

    // The callers pass the gross over (1 + rate): split back from the gross in cents, so the
    // record's base + cuota equal the amount charged and printed.
    const GrossSplit split = splitGross(taxBase * (1.0 + taxRate / 100.0), taxRate);
    VerifactuTaxItem taxItem;
    taxItem.setTaxBase(split.base);
    taxItem.setTaxRate(taxRate);
    taxItem.setTaxAmount(split.tax);

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

int VerifactuIntegration::highestRectificationAtAeat() const
{
    // AEAT's numbers only from the real AEAT (as for tickets); the direct store's own R-
    // records of its environment always count, so a number is never stored twice.
    if (!m_direct)
        return 0;
    const int atAeat = m_direct->environmentName() == QLatin1String("produccion") ? m_direct->highestRectificationAtAeat() : 0;
    return qMax(atAeat, m_direct->highestStoredRectification());
}

bool VerifactuIntegration::holdsPendingRegistration(const QString &invoiceNumber) const
{
    return m_direct && m_direct->holdsPendingRecord(AeatStore::Kind::Registration, invoiceNumber);
}

bool VerifactuIntegration::holdsPendingCancellation(const QString &invoiceNumber) const
{
    return m_direct && m_direct->holdsPendingRecord(AeatStore::Kind::Cancellation, invoiceNumber);
}

bool VerifactuIntegration::storedRegistrationOutcome(const QString &invoiceNumber, VerifactuResult *result) const
{
    return m_direct && m_direct->storedOutcome(AeatStore::Kind::Registration, invoiceNumber, result);
}

QString VerifactuIntegration::environmentName() const
{
    return m_direct ? m_direct->environmentName() : QString();
}

QString VerifactuIntegration::sendingError() const
{
    return m_direct ? m_direct->sendingError() : QString();
}

QString VerifactuIntegration::certificateExpiryWarning() const
{
    return m_direct ? m_direct->certificateExpiryWarning() : QString();
}

bool VerifactuIntegration::verifyStoredChain(QString *problem, int *continuations) const
{
    if (!m_direct)
        return true;
    const AeatChainCheck::Result chain = m_direct->verifyStoredChain();
    if (problem)
        *problem = chain.problem;
    if (continuations)
        *continuations = chain.continuations;
    return chain.ok;
}

void VerifactuIntegration::syncDirectChain(const std::function<void(bool ok, const QString &message)> &done)
{
    if (m_direct)
        m_direct->syncChain(done);
}

void VerifactuIntegration::setAeatEndpointOverride(const QString &url)
{
    AeatDirectBackend::setEndpointOverride(url);
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

void VerifactuIntegration::loadEmitterConfiguration()
{
    if (!m_manager) return;

    AppSettings *settings = AppSettings::instance();
    QString emitterNIF  = settings->verifactuNif();
    QString emitterName = settings->verifactuName();
    QString serviceKey  = settings->verifactuServiceKey();

    m_manager->getConfig()->setEmitterData(emitterNIF, emitterName);
    m_manager->getConfig()->setSystemData("LAIDEAL", QString(PROJECT_VERSION));
    m_manager->getConfig()->setServiceKey(serviceKey);

    VerifactuConfig::Environment env = settings->verifactuProduction()
        ? VerifactuConfig::PRODUCTION
        : VerifactuConfig::TESTING;
    m_manager->getConfig()->setEnvironment(env);
}
