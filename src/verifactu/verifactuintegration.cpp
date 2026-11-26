#include "verifactuintegration.h"
#include "aeatdirectbackend.h"
#include "aeathash.h"
#include "appsettings.h"
#include <QDebug>
#include <QNetworkReply>
#include <algorithm>
#include <QSqlDatabase>
#include <QUrl>

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
// True when it was deleted now.
bool retireManager(VerifactuManager *manager)
{
    if (!manager)
        return true;
    auto running = [manager] {
        const auto replies = manager->findChildren<QNetworkReply *>();
        return std::any_of(replies.cbegin(), replies.cend(), [](QNetworkReply *r) { return r->isRunning(); });
    };
    if (!running()) {
        delete manager;
        return true;
    }
    // Deleted after the last in-flight reply has finished (and been handled); the
    // manager is gone by the time a later reply would ask again.
    const auto replies = manager->findChildren<QNetworkReply *>();
    for (QNetworkReply *reply : replies) {
        if (!reply->isRunning())
            continue;
        QObject::connect(reply, &QNetworkReply::finished, manager, [manager, running] {
            if (!running())
                manager->deleteLater();
        }, Qt::QueuedConnection);
    }
    return false;
}
} // namespace

bool VerifactuIntegration::initialize(const QSqlDatabase &db)
{
    // Again after Configuración: one backend at a time (two direct clients would both
    // send the outbox and ignore each other's AEAT wait time).
    if (m_direct)
        m_direct->releaseWaiters();         // the windows still waiting hear an answer
    const QObject *oldDirect = m_direct;
    const QObject *oldManager = m_manager;
    delete m_direct;
    // A queued answer dies with its backend: what is still in flight for a deleted one never ends.
    dropInFlightOf(oldDirect);
    if (retireManager(m_manager))
        dropInFlightOf(oldManager);
    m_direct = nullptr;
    m_manager = nullptr;
    m_backend = nullptr;
    VerifactuConfig::removeLegacyIni();
    if (!m_sessionStart.isValid())
        m_sessionStart = QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch());
    AppSettings *settings = AppSettings::instance();
    m_emitterNif  = settings->verifactuNif();
    m_emitterName = settings->verifactuName();
    if (m_emitterNif.isEmpty() || m_emitterName.isEmpty()) {
        m_lastError = tr("No se pudo cargar la configuración del emisor");
        qCritical() << "Emitter NIF or name not configured in AppSettings";
        return false;
    }

    if (settings->verifactuDirectAeat()) {
        AeatDirectBackend::Config c;
        // Pre-production unless explicitly enabled on its own (never the gateway's PRODUCCIÓN box).
        c.testEnvironment = !settings->aeatDirectProduction();
        c.issuerNif  = m_emitterNif;
        c.issuerName = m_emitterName;
        c.system = AeatRecord::laIdealSystem(QString(PROJECT_VERSION), QString());   // NumeroInstalacion: the database's
        c.certificateThumbprint = settings->aeatCertificateThumbprint();
        c.certificatePath       = settings->aeatCertificateFile();
        c.certificatePassword   = settings->aeatCertificatePassword();
        c.sessionStart = m_sessionStart;
        c.readChainMarker  = [] { return AppSettings::instance()->aeatChainMarker(); };
        c.writeChainMarker = [](const QString &marker) { AppSettings::instance()->setAeatChainMarker(marker); };
        m_direct = new AeatDirectBackend(c, db.isValid() ? db : QSqlDatabase::database(QSqlDatabase::defaultConnection, false), this);
        m_backend = m_direct;
        connect(m_direct, &AeatDirectBackend::chainSynced, this, &VerifactuIntegration::chainChecked);
        connect(m_direct, &AeatDirectBackend::unsentRecordsChanged, this, &VerifactuIntegration::unsentRecordsChanged);
    } else {
        m_manager = new VerifactuManager(this);
        loadEmitterConfiguration();
        m_backend = m_manager;
    }

    // The entries of a retired gateway manager stay: its running replies end through the same handler.
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
    if (refusesChainedRecord(__func__))
        return QString();
    // Dated before production (the test environment, or the gateway): a retry must not
    // register it at the real AEAT now. A new invoice is asked this before it is stored.
    if (refusesBeforeProduction(invoiceNumber, invoiceDate, __func__))
        return QString();

    const QString requestId = m_backend->submitInvoiceAsync(simplifiedInvoice(invoiceNumber, invoiceDate, taxBase, taxRate, description));
    if (!requestId.isEmpty())
        m_inFlight.insert(requestId, { invoiceNumber, m_backend });
    return requestId;
}

bool VerifactuIntegration::isInFlight(const QString &invoiceId) const
{
    for (auto it = m_inFlight.cbegin(); it != m_inFlight.cend(); ++it)
        if (it.value().invoiceId == invoiceId)
            return true;
    return false;
}

void VerifactuIntegration::dropInFlightOf(const QObject *backend)
{
    for (auto it = m_inFlight.begin(); it != m_inFlight.end();)
        it = it.value().backend == backend ? m_inFlight.erase(it) : std::next(it);
}

QString VerifactuIntegration::cancelInvoiceAsync(
    const QString &invoiceNumber,
    const QDate &invoiceDate,
    const QString &storedQrUrl)
{
    if (refusesChainedRecord(__func__))
        return QString();
    if (const QString undecided = cancellationUndecidedReason(invoiceNumber, invoiceDate, storedQrUrl); !undecided.isEmpty()) {
        m_lastError = undecided;
        qWarning() << "VerifactuIntegration:" << __func__ << "refused for" << invoiceNumber
                   << "- production start day unreadable";
        return QString();
    }
    // The real AEAT never held it: there is nothing to cancel there (Anular does it in the books).
    if (invoicePredatesProduction(invoiceNumber, invoiceDate, storedQrUrl)) {
        m_lastError = tr("La factura %1 es anterior al paso a PRODUCCIÓN: la AEAT real no la tiene y no se le envía "
                         "su anulación.").arg(invoiceNumber);
        qWarning() << "VerifactuIntegration:" << __func__ << "refused for" << invoiceNumber << "- issued before production";
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
    if (refusesChainedRecord(__func__))
        return QString();
    // The gateway's rectificativas carry no FacturasRectificadas (RectificationItems): fine in
    // AEAT's pre-production, never as a real invoice. The direct client sends them.
    if (m_manager && m_manager->getConfig()->getEnvironment() == VerifactuConfig::PRODUCTION) {
        m_lastError = tr("Con IreneSolutions las rectificativas no se envían en PRODUCCIÓN (no indican la factura "
                         "que rectifican). Use la conexión directa con la AEAT.");
        qWarning() << "VerifactuIntegration:" << __func__ << "refused with the gateway in production";
        return QString();
    }
    if (refusesBeforeProduction(newInvoiceNumber, invoiceDate, __func__))
        return QString();
    if (!VerifactuInvoice::isRectificationInvoiceType(invoiceType)) {
        m_lastError = tr("Tipo de factura no rectificativo");
        qWarning() << "VerifactuIntegration:" << __func__ << "refused: not a rectification type";
        return QString();
    }

    VerifactuInvoice invoice = invoiceHeader(newInvoiceNumber, invoiceDate, invoiceType,
                                             description.isEmpty() ? kRectificationDescription : description);

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
    const QString requestId = m_backend->submitInvoiceAsync(invoice);
    if (!requestId.isEmpty())
        m_inFlight.insert(requestId, { newInvoiceNumber, m_backend });
    return requestId;
}

QString VerifactuIntegration::generateQRAsync(
    const QString &invoiceNumber,
    const QDate &invoiceDate,
    double taxBase,
    double taxRate,
    const QString &description)
{
    if (refusesUnconfigured(__func__))
        return QString();

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

VerifactuInvoice VerifactuIntegration::invoiceHeader(const QString &number, const QDate &date,
                                                     VerifactuInvoice::InvoiceType type,
                                                     const QString &description) const
{
    VerifactuInvoice invoice;
    invoice.setInvoiceNumber(number);
    invoice.setInvoiceDate(date);
    invoice.setInvoiceType(type);
    invoice.setSellerNIF(m_emitterNif);
    invoice.setSellerName(m_emitterName);
    invoice.setDescription(description);
    return invoice;
}

bool VerifactuIntegration::refusesChainedRecord(const char *operation)
{
    // A chained record needs the checked chain, whichever window asks for it.
    if (const QString blocked = numberingBlockedReason(); !blocked.isEmpty()) {
        m_lastError = blocked;
        qWarning() << "VerifactuIntegration:" << operation << "refused while the chain is unchecked";
        return true;
    }
    return refusesUnconfigured(operation);
}

bool VerifactuIntegration::refusesUnconfigured(const char *operation)
{
    if (isConfigured())
        return false;
    m_lastError = tr("Verifactu no está configurado correctamente");
    qWarning() << "VerifactuIntegration:" << operation << "refused: Verifactu is not configured";
    return true;
}

bool VerifactuIntegration::refusesBeforeProduction(const QString &invoiceNumber, const QDate &invoiceDate,
                                                   const char *operation)
{
    bool startUnreadable = false;
    if (!isBeforeProductionStart(invoiceDate, &startUnreadable))
        return false;
    if (startUnreadable) {
        m_lastError = tr("No se pudo leer el día del paso a PRODUCCIÓN: la factura %1 del %2 es anterior a hoy y no se "
                         "envía a la AEAT real.").arg(invoiceNumber, AeatHash::dateText(invoiceDate));
        qWarning() << "VerifactuIntegration:" << operation << "refused for" << invoiceNumber
                   << "- production start day unreadable, invoice dated before today";
        return true;
    }
    m_lastError = tr("La factura %1 del %2 es anterior al paso a PRODUCCIÓN (%3): no se envía a la AEAT real.")
                      .arg(invoiceNumber, AeatHash::dateText(invoiceDate), AeatHash::dateText(productionStartDate()));
    qWarning() << "VerifactuIntegration:" << operation << "refused for" << invoiceNumber << "- dated before production";
    return true;
}

bool VerifactuIntegration::isBeforeProductionStart(const QDate &invoiceDate, bool *startUnreadable) const
{
    if (!m_direct || m_direct->environmentName() != QLatin1String("produccion") || !invoiceDate.isValid())
        return false;
    const QDate start = m_direct->startDate();
    if (start.isValid())
        return invoiceDate < start;
    // Production whose start day cannot be read: fail closed, only today's date passes.
    if (startUnreadable)
        *startUnreadable = true;
    return invoiceDate < QDate::currentDate();
}

namespace {
// The QR host tells where AEAT registered an invoice: its test site (prewww2) or the real one.
bool isRealAeatQrUrl(const QString &qrUrl)
{
    const QString host = QUrl(qrUrl.trimmed()).host().toLower();
    return host == QLatin1String("www2.agenciatributaria.gob.es") || host == QLatin1String("www2.aeat.es");
}
}

VerifactuInvoice VerifactuIntegration::simplifiedInvoice(const QString &invoiceNumber, const QDate &invoiceDate,
                                                         double taxBase, double taxRate,
                                                         const QString &description) const
{
    VerifactuInvoice invoice = invoiceHeader(invoiceNumber, invoiceDate, VerifactuInvoice::SIMPLIFIED,
                                             description.isEmpty() ? kInvoiceDescription : description);

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
    if (refusesUnconfigured(__func__))
        return QString();
    return m_backend->queryInvoiceAsync(invoiceNumber, invoiceDate);
}

QString VerifactuIntegration::takeChainRestartNotice()
{
    return m_direct ? m_direct->takeRestartNotice() : QString();
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

QDate VerifactuIntegration::productionStartDate() const
{
    return m_direct && m_direct->environmentName() == QLatin1String("produccion") ? m_direct->startDate() : QDate();
}

void VerifactuIntegration::markProductionStarted()
{
    if (m_direct && m_direct->environmentName() == QLatin1String("produccion"))
        m_direct->restartPeriod();
}

bool VerifactuIntegration::invoicePredatesProduction(const QString &invoiceNumber, const QDate &invoiceDate,
                                                     const QString &storedQrUrl) const
{
    if (!m_direct || m_direct->environmentName() != QLatin1String("produccion") || m_direct->holdsRegistration(invoiceNumber))
        return false;
    // The gateway itself registered it at the real AEAT: its QR points there.
    if (isRealAeatQrUrl(storedQrUrl))
        return false;
    const QDate start = m_direct->startDate();
    return (start.isValid() && invoiceDate.isValid() && invoiceDate < start) || m_direct->acceptedInTestEnvironment(invoiceNumber);
}

QString VerifactuIntegration::cancellationUndecidedReason(const QString &invoiceNumber, const QDate &invoiceDate,
                                                          const QString &storedQrUrl) const
{
    if (!m_direct || m_direct->environmentName() != QLatin1String("produccion") || m_direct->startDate().isValid())
        return QString();
    // What settles it without the start day: a production record, the real AEAT's QR, the
    // test environment's acceptance, or a date that cannot be earlier than the switch.
    if (m_direct->holdsRegistration(invoiceNumber) || isRealAeatQrUrl(storedQrUrl)
        || m_direct->acceptedInTestEnvironment(invoiceNumber) || !invoiceDate.isValid() || invoiceDate >= QDate::currentDate())
        return QString();
    return tr("No se pudo leer el día del paso a PRODUCCIÓN: no se sabe si la AEAT real tiene la factura %1 del %2, "
              "y no se anula.").arg(invoiceNumber, AeatHash::dateText(invoiceDate));
}

QString VerifactuIntegration::dateBeforeProductionReason(const QDate &invoiceDate) const
{
    bool startUnreadable = false;
    if (!isBeforeProductionStart(invoiceDate, &startUnreadable))
        return QString();
    if (startUnreadable) {
        qWarning() << "VerifactuIntegration: production start day unreadable, date" << AeatHash::dateText(invoiceDate)
                   << "refused as before today";
        return tr("No se pudo leer el día del paso a PRODUCCIÓN: la fecha %1 es anterior a hoy y una factura nueva no "
                  "puede llevarla.").arg(AeatHash::dateText(invoiceDate));
    }
    return tr("La fecha %1 es anterior al paso a PRODUCCIÓN (%2): una factura nueva no puede llevarla, porque no "
              "se registraría en la AEAT real.")
        .arg(AeatHash::dateText(invoiceDate), AeatHash::dateText(productionStartDate()));
}

QString VerifactuIntegration::sendingError() const
{
    return m_direct ? m_direct->sendingError() : QString();
}

int VerifactuIntegration::unsentRecordCount() const
{
    return m_direct ? m_direct->unsentRecordCount() : 0;
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
              "Archivo \u2192 Configuración.")
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
    m_manager->getConfig()->setServiceKey(serviceKey);

    VerifactuConfig::Environment env = settings->verifactuProduction()
        ? VerifactuConfig::PRODUCTION
        : VerifactuConfig::TESTING;
    m_manager->getConfig()->setEnvironment(env);
}
