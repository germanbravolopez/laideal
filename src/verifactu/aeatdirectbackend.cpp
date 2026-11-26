#include "aeatdirectbackend.h"
#include "textformat.h"

#include "aeatchainsync.h"
#include "aeathash.h"
#include "aeatqr.h"
#include "aeatresponse.h"

#include <algorithm>

#include <QDebug>
#include <QPixmap>
#include <QSet>

namespace {

const QString kProductionUrl = QStringLiteral("https://www1.agenciatributaria.gob.es/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP");
const QString kTestUrl = QStringLiteral("https://prewww1.aeat.es/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP");
const QString kRegistration = QStringLiteral("Alta");
const QString kCancellation = QStringLiteral("Anulacion");
const int kTransferTimeoutMs = 30000;
const int kQrPixelsPerModule = 5;
const int kMonthsBack = 24;           // how far the first sync looks for the issuer's records
const int kMonthsAlwaysQueried = 2;   // this month and the previous one, always
const int kBlockingRecordsNamed = 5;  // unsent records a waiting chain restart names in its message
constexpr int kMaxUnverifiedDuplicates = 5;   // duplicate answers AEAT's query never confirms

// Test hooks.
QString s_endpointOverride;
int s_retryDelaySeconds = 60;         // after a failed send, before trying again on its own

// As the store keeps its records and the chain marker names the environment.
QString environmentText(bool testEnvironment)
{
    return testEnvironment ? QStringLiteral("pruebas") : QStringLiteral("produccion");
}

QString validationUrlOf(const QString &issuerNif, const QString &invoiceNumber, const QDate &issueDate, double total,
                        bool testEnvironment)
{
    return AeatHash::qrValidationUrl(issuerNif, invoiceNumber, issueDate, AeatHash::amountText(total), testEnvironment);
}

QPixmap qrPixmapOf(const QString &validationUrl)
{
    return QPixmap::fromImage(AeatQr::image(validationUrl, kQrPixelsPerModule));
}

QString operationOf(AeatStore::Kind kind)
{
    return kind == AeatStore::Kind::Registration ? kRegistration : kCancellation;
}

// A record the month query returned, as the chain sync reads it. An invoice AEAT holds as
// cancelled comes back as its cancellation record (huella, time, link) or, hidden, as the
// registration it was.
AeatResponse::RecordSummary summaryOf(const AeatResponse::QueryRecord &r, bool foreign)
{
    const bool cancelled = r.state.startsWith(QLatin1String("Anulad"));
    const bool cancellationShown = cancelled && AeatResponse::carriesCancellationRecord(r);
    AeatResponse::RecordSummary s;
    s.valid = r.hash.size() == 64 && !r.generatedAt.isEmpty();
    s.operation = cancellationShown ? kCancellation : kRegistration;
    s.cancellationFromQuery = cancellationShown;
    s.issuerNif = r.issuerNif;
    s.invoiceNumber = r.invoiceNumber;
    s.issueDate = r.issueDate;
    s.hash = r.hash;
    s.generatedAt = r.generatedAt;
    s.previousHash = r.previousHash;
    s.installationNumber = r.installationNumber;
    // AEAT received the cancellation when the invoice's state last changed.
    s.presentedAt = cancellationShown ? r.stateChangedAt : r.presentedAt;
    s.cancelledAtAeat = cancelled && !cancellationShown;
    s.cancelledAt = r.stateChangedAt;
    s.foreignSystem = foreign;
    return s;
}

} // namespace

AeatDirectBackend::AeatDirectBackend(const Config &config, const QSqlDatabase &db, QObject *parent)
    : VerifactuBackend(parent),
      m_config(config),
      m_store(db, environmentText(config.testEnvironment)),
      m_transport(new AeatTransport(this))
{
    m_sendTimer.setSingleShot(true);
    connect(&m_sendTimer, &QTimer::timeout, this, &AeatDirectBackend::sendPending);
    // Whole seconds, as the records' generation time is stored.
    m_sessionStart = m_config.sessionStart.isValid() ? m_config.sessionStart
                                                     : QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch());
    m_transport->setTimeoutMs(kTransferTimeoutMs);

    if (m_config.issuerNif.trimmed().isEmpty() || m_config.issuerName.trimmed().isEmpty())
        m_configError = tr("Falta el NIF o el nombre del emisor.");
    else if (!m_store.ensureSchema())
        m_configError = tr("No se pueden crear las tablas de registros AEAT: %1").arg(m_store.lastError());
    else {
        // The number lives with the chain (one SIF per database): no record is generated without it.
        m_config.system.installationNumber = m_store.installationNumber();
        if (m_config.system.installationNumber.isEmpty())
            m_configError = tr("No se puede leer el número de instalación de los registros AEAT: %1").arg(m_store.lastError());
    }
    if (!m_configError.isEmpty()) {
        qWarning() << "AeatDirectBackend: not configured -" << m_configError;
        return;
    }
    // Kept if already marked. A client that runs in an environment has begun there,
    // however it got its settings.
    m_store.markStarted();
    // The certificate is only needed to send: records are generated without it.
    if (endpointUrl(m_config.testEnvironment).startsWith(QLatin1String("https"))) {
        QString error;
        const bool loaded = m_certificate.load(m_config.certificateThumbprint, m_config.certificatePath,
                                               m_config.certificatePassword, &error);
        if (loaded)
            m_transport->setCertificate(&m_certificate);
        else
            m_sendError = error;
    }
    if (!m_sendError.isEmpty())
        qWarning() << "AeatDirectBackend: records will be generated but not sent -" << m_sendError;
    // Records left unsent when the app closed go first.
    const QList<AeatStore::Record> left = m_store.pending(1000, m_config.issuerNif);
    if (left.isEmpty())
        return;
    scheduleSend();
    refreshUnsentCount(std::any_of(left.cbegin(), left.cend(), [this](const AeatStore::Record &r) {
        return delayedByIncident(r, m_sessionStart);
    }));
}

AeatDirectBackend::~AeatDirectBackend() = default;

QDate AeatDirectBackend::startDate()
{
    return m_store.startedAt().date();
}

void AeatDirectBackend::restartPeriod()
{
    m_store.markStarted(true);
}

bool AeatDirectBackend::holdsRegistration(const QString &invoiceNumber)
{
    return m_store.holdsRegistrationIn(environmentName(), invoiceNumber);
}

bool AeatDirectBackend::acceptedInTestEnvironment(const QString &invoiceNumber)
{
    return m_store.holdsRegistrationIn(QStringLiteral("pruebas"), invoiceNumber, true);
}

QString AeatDirectBackend::endpointUrl(bool testEnvironment)
{
    if (!s_endpointOverride.isEmpty())
        return s_endpointOverride;
    return testEnvironment ? kTestUrl : kProductionUrl;
}

void AeatDirectBackend::setEndpointOverride(const QString &url)
{
    s_endpointOverride = url;
}

void AeatDirectBackend::setRetryDelaySeconds(int seconds)
{
    s_retryDelaySeconds = seconds;
}

QString AeatDirectBackend::environmentName() const
{
    return environmentText(m_config.testEnvironment);
}

QString AeatDirectBackend::configurationInfo() const
{
    return QStringLiteral("AEAT direct - environment: %1, issuer: %2, endpoint: %3, %4%5")
        .arg(environmentName().toUpper(), m_config.issuerNif, endpointUrl(m_config.testEnvironment),
             isConfigured() ? QStringLiteral("configured") : QStringLiteral("NOT configured: ") + m_configError,
             m_sendError.isEmpty() ? QString() : QStringLiteral(", not sending: ") + m_sendError);
}

QString AeatDirectBackend::nextRequestId()
{
    // Unique across every backend of the run: after Configuración a new one starts, and
    // the windows still hold the previous one's requests - a repeated id would hand one
    // ticket another invoice's answer.
    static int counter = 0;
    return QStringLiteral("aeat-%1").arg(++counter);
}

qint64 AeatDirectBackend::msecsUntilNextSend() const
{
    if (!m_nextSendAllowed.isValid())
        return 0;
    return qMax<qint64>(0, QDateTime::currentDateTime().msecsTo(m_nextSendAllowed));
}

int AeatDirectBackend::secondsUntilNextSend() const
{
    return int((msecsUntilNextSend() + 999) / 1000);    // rounded up: never earlier than AEAT allows
}

// ---------------------------------------------------------------------------
// Chain sync
// ---------------------------------------------------------------------------

QList<AeatResponse::RecordSummary> AeatDirectBackend::localCandidates()
{
    // Our own records only: one chain per system and issuer, never another system's
    // (the gateway's records stay in its own chain).
    QList<AeatResponse::RecordSummary> out;
    for (const QString &xml : m_store.recordXmls()) {
        const AeatResponse::RecordSummary s = AeatResponse::summarizeRecord(xml);
        if (s.valid && s.issuerNif == m_config.issuerNif)
            out << s;
    }
    return out;
}

bool AeatDirectBackend::adoptInstallationNumber(const QString &chainNumber, QString *message)
{
    // The number belongs to the chain: ours becomes that of its last record at AEAT before
    // anything is generated (and before the head moves, so a failure repeats).
    if (chainNumber.isEmpty())
        return true;
    if (!m_store.setInstallationNumber(chainNumber)) {
        *message = tr("No se pudo guardar el número de instalación de la cadena: %1").arg(m_store.lastError());
        return false;
    }
    qWarning() << "AeatDirectBackend: NumeroInstalacion" << m_config.system.installationNumber << "->"
               << chainNumber << "(the chain's last record at AEAT)";
    m_config.system.installationNumber = chainNumber;
    return true;
}

bool AeatDirectBackend::adoptTip(const QList<AeatResponse::RecordSummary> &found, QString *message)
{
    bool read = false;
    const AeatRecord::PreviousRecord head = m_store.chainHead(m_config.issuerNif, &read);
    if (!read) {
        *message = tr("No se puede leer la cadena de registros: %1").arg(m_store.lastError());
        return false;
    }
    AeatChainSync::Input input;
    input.candidates = found + m_foreignCancelled;
    input.issuerNif = m_config.issuerNif;
    input.head = head;
    QString restartNumber;
    input.restartedAt = m_store.chainRestartedAt(m_config.issuerNif, &restartNumber);
    input.restartNumber = restartNumber;
    QString seen;
    if (lastSeenHead(&seen) && !seen.isEmpty())
        input.lastSeen = seen;
    input.anchors = m_store.chainAnchors();
    input.mayRestart = m_firstSync && m_config.adoptInstallationNumber;
    input.adoptInstallationNumber = m_config.adoptInstallationNumber;
    input.installationNumber = m_config.system.installationNumber;
    const AeatChainSync::Plan plan = AeatChainSync::plan(input);
    using Kind = AeatChainSync::Plan::Kind;
    if (plan.kind == Kind::Restart)
        return restartChain(plan.lostInvoice, message);
    if (plan.kind == Kind::NoRecords) {
        *message = tr("No hay registros anteriores de este sistema para el emisor: la cadena empezará con el primer registro.");
        return true;
    }
    const AeatResponse::RecordSummary &tip = plan.tip;
    if (!adoptInstallationNumber(plan.installationNumberToAdopt, message))
        return false;
    if (plan.headMoves) {
        const AeatRecord::PreviousRecord moved{
            { m_config.issuerNif, tip.invoiceNumber, QDate::fromString(tip.issueDate, kDbDateFormat) }, tip.hash, tip.generatedAt };
        // The next record links to this huella: an anchor the chain verifier accepts.
        if (!m_store.addChainAnchor(m_config.issuerNif, tip.hash) || !m_store.setChainHead(m_config.issuerNif, moved)) {
            *message = tr("No se pudo guardar el punto de la cadena.");
            return false;
        }
    }
    // A record the query returns is read as a registration and the stored head carries no
    // operation: a tip with our cancellation's huella is named from the stored record.
    const bool cancellation = tip.operation == kCancellation
        || m_store.latest(AeatStore::Kind::Cancellation, tip.invoiceNumber, m_config.issuerNif)
               .hash.compare(tip.hash, Qt::CaseInsensitive) == 0;
    *message = AeatChainSync::continuationMessage(tip, cancellation);
    return true;
}

bool AeatDirectBackend::lastSeenHead(QString *hash) const
{
    if (!m_config.readChainMarker)
        return false;
    const QStringList parts = m_config.readChainMarker().split(QLatin1Char('|'));
    if (parts.size() != 3 || parts.at(0) != environmentName() || parts.at(1) != m_config.issuerNif
        || parts.at(2) == QLatin1String("?"))
        return false;
    *hash = parts.at(2);
    return true;
}

void AeatDirectBackend::resumeDeferredRestart()
{
    if (!m_restartDeferred || m_syncRunning || m_store.pendingCount(m_config.issuerNif) != 0)
        return;
    m_restartDeferred = false;
    QTimer::singleShot(0, this, [this] { syncChain(nullptr); });
}

QString AeatDirectBackend::takeRestartNotice()
{
    const QString notice = m_restartNotice;
    m_restartNotice.clear();
    return notice;
}

bool AeatDirectBackend::restartChain(const QString &cancelledInvoice, QString *message)
{
    // Records generated in the old chain and not yet at AEAT belong to it: they are sent
    // first (the outbox does it on its own), and the new chain starts after them.
    const int unsent = m_store.pendingCount(m_config.issuerNif);
    if (unsent != 0) {
        m_restartDeferred = true;
        if (unsent < 0) {
            *message = tr("No se pueden contar los registros pendientes de enviar: %1").arg(m_store.lastError());
            return false;
        }
        // Which ones, and why they do not go out: saving stays blocked until they do.
        QStringList invoices;
        QString refusal;
        for (const AeatStore::Record &r : m_store.pending(kBlockingRecordsNamed, m_config.issuerNif)) {
            invoices << r.invoiceNumber;
            if (refusal.isEmpty() && !r.errorDescription.isEmpty())
                refusal = r.invoiceNumber + QStringLiteral(": ") + r.errorDescription;
        }
        if (unsent > invoices.size())
            invoices << QStringLiteral("...");
        *message = tr("Hay %1 registro(s) anteriores pendientes de enviar a la AEAT (factura %2): se envían antes de "
                      "empezar una nueva cadena de registros, y la comprobación se repite sola.")
                       .arg(unsent).arg(invoices.join(QStringLiteral(", ")));
        if (!m_sendError.isEmpty())
            *message += QLatin1Char(' ') + tr("Ahora no se pueden enviar: %1").arg(m_sendError);
        else if (!refusal.isEmpty())
            *message += QLatin1Char(' ') + tr("Último error de la AEAT (%1).").arg(refusal);
        return false;
    }
    const QString number = AeatStore::newInstallationNumber();
    if (!m_store.restartChain(m_config.issuerNif, number, cancelledInvoice)) {
        *message = tr("No se pudo guardar el nuevo inicio de la cadena: %1").arg(m_store.lastError());
        return false;
    }
    qWarning() << "AeatDirectBackend: AEAT holds invoice" << cancelledInvoice << "as cancelled and this database lacks"
               << "that cancellation record: the chain restarts, NumeroInstalacion" << m_config.system.installationNumber
               << "->" << number;
    m_config.system.installationNumber = number;
    *message = tr("La AEAT tiene anulada la factura %1 y esta base de datos no guarda su registro de anulación, que "
                  "puede ser el último de la cadena: la cadena empieza de nuevo con el número de instalación %2.")
                   .arg(cancelledInvoice, number);
    m_restartNotice = *message;
    return true;
}

void AeatDirectBackend::syncChain(const ChainDone &done)
{
    if (!isConfigured()) {
        if (done)
            done(false, m_configError);
        return;
    }
    if (done)
        m_syncWaiters << done;
    if (m_syncRunning)
        return;
    m_syncRunning = true;
    m_foreignCancelled.clear();
    // The first time, AEAT must be asked (the database may be new for a system that
    // already sent records); afterwards it is asked too - a database restored from an
    // older backup continues the same chain - but the local chain is enough without it.
    m_firstSync = !m_store.chainSynced(m_config.issuerNif)
                  || (m_config.readChainMarker && m_config.readChainMarker() != chainMarker());
    if (m_firstSync)
        qDebug() << "AeatDirectBackend: chain sync must reach AEAT (first start, or a database other than the last one)";
    if (!m_sendError.isEmpty()) {
        if (m_firstSync) {
            syncFinished(false, tr("Antes del primer registro hay que consultar la AEAT, y no se puede: %1").arg(m_sendError));
            return;
        }
        QString message;
        const bool ok = adoptTip(localCandidates(), &message);
        syncFinished(ok, message);
        return;
    }
    const QDate today = QDate::currentDate();
    queryMonth(QDate(today.year(), today.month(), 1), m_firstSync ? kMonthsBack : kMonthsAlwaysQueried,
               kMonthsAlwaysQueried, {}, {}, false);
}

QString AeatDirectBackend::chainMarker()
{
    bool read = false;
    const AeatRecord::PreviousRecord head = m_store.chainHead(m_config.issuerNif, &read);
    return QStringLiteral("%1|%2|%3").arg(environmentName(), m_config.issuerNif, read ? head.hash : QStringLiteral("?"));
}

void AeatDirectBackend::saveChainMarker()
{
    if (m_config.writeChainMarker)
        m_config.writeChainMarker(chainMarker());
}

void AeatDirectBackend::syncFinished(bool ok, const QString &message)
{
    m_syncRunning = false;
    m_lastSyncFailed = !ok;
    if (ok) {
        m_chainReady = true;
        if (!m_store.markChainSynced(m_config.issuerNif))
            qWarning() << "AeatDirectBackend: chain sync not recorded (the next start asks AEAT again) -" << m_store.lastError();
        saveChainMarker();
    }
    qDebug() << "AeatDirectBackend: chain sync" << (ok ? "done -" : "failed -") << message;
    const QList<ChainDone> waiters = m_syncWaiters;
    m_syncWaiters.clear();
    for (const ChainDone &w : waiters)
        w(ok, message);
    emit chainSynced(ok);
}

void AeatDirectBackend::queryMonth(QDate month, int monthsLeft, int mustQuery, AeatRecord::InvoiceRef pageAfter,
                                   QList<AeatResponse::RecordSummary> found, bool sawUnusable)
{
    const QString envelope = AeatRecord::queryEnvelope(m_config.issuerName, m_config.issuerNif, month.year(),
                                                       month.month(), QString(), pageAfter, true);
    m_transport->post(endpointUrl(m_config.testEnvironment), envelope.toUtf8(),
                      [=](const AeatTransport::Response &response) mutable {
        const AeatResponse::Query reply = AeatResponse::parseQuery(response.body);
        if (!reply.parsed || reply.fault) {
            const QString why = reply.fault ? reply.faultText : (response.error.isEmpty() ? tr("respuesta no reconocida") : response.error);
            if (m_firstSync) {
                syncFinished(false, tr("No se pudo consultar la AEAT: %1").arg(why));
                return;
            }
            // What AEAT already answered (e.g. this month's records) still counts.
            QString message;
            const bool ok = adoptTip(found + localCandidates(), &message);
            syncFinished(ok, message + QLatin1Char(' ') + tr("(sin consultar la AEAT: %1)").arg(why));
            return;
        }
        for (const AeatResponse::QueryRecord &r : reply.records) {
            // The issuer's highest ticket number at AEAT, from any system (the gateway sent the
            // shop's own numbers): a database older than AEAT must not issue them again.
            // "31277", "31277-1"; never the self-test's PRUEBA-...
            bool numeric = false;
            const int number = r.invoiceNumber.section(QLatin1Char('-'), 0, 0).toInt(&numeric);
            if (numeric)
                m_highestNumberAtAeat = qMax(m_highestNumberAtAeat, number);
            if (r.invoiceNumber.startsWith(QLatin1String("R-"))) {
                const int rectification = r.invoiceNumber.section(QLatin1Char('-'), 1, 1).toInt(&numeric);
                if (numeric)
                    m_highestRectificationAtAeat = qMax(m_highestRectificationAtAeat, rectification);
            }
            // Without the SistemaInformatico block whose record it is cannot be told: as
            // unusable as a missing huella, never taken for another system's.
            if (r.systemId.isEmpty() || r.producerNif.isEmpty()) {
                sawUnusable = true;
                continue;
            }
            // Another system's records (e.g. the gateway's) belong to its own chain. A
            // cancelled one stays in view: its block may be the registration's or the
            // cancellation's, so a cancellation of ours may be behind it (adoptTip).
            const bool foreign = r.systemId != m_config.system.systemId || r.producerNif != m_config.system.producerNif;
            const bool cancelled = r.state.startsWith(QLatin1String("Anulad"));
            if (foreign && !cancelled)
                continue;
            const AeatResponse::RecordSummary s = summaryOf(r, foreign);
            if (foreign) {
                if (s.valid)
                    m_foreignCancelled << s;
                continue;
            }
            if (s.valid)
                found << s;
            else
                sawUnusable = true;
            if (s.valid && cancelled)
                qDebug() << "AeatDirectBackend: AEAT returns cancelled invoice" << s.invoiceNumber << "with the huella of its"
                         << (s.cancellationFromQuery ? QStringLiteral("cancellation") : QStringLiteral("registration"))
                         << "record, generated" << s.generatedAt;
        }
        if (reply.morePages && !reply.records.isEmpty()) {
            const AeatResponse::QueryRecord &last = reply.records.last();
            queryMonth(month, monthsLeft, mustQuery,
                       { last.issuerNif, last.invoiceNumber, QDate::fromString(last.issueDate, kDbDateFormat) },
                       found, sawUnusable);
            return;
        }
        // This month and the previous one always (a record generated now may be dated
        // last month); further back only while nothing has been found - except on a first
        // sync (first start, restored database, new PC), which reads every month: the
        // chain's newest record may carry an older date (a resend, a back-dated
        // rectificativa). Never on a daily start, whose chain marker matches.
        if (monthsLeft > 1 && (mustQuery > 1 || found.isEmpty() || m_firstSync) && !(found.isEmpty() && sawUnusable)) {
            queryMonth(month.addMonths(-1), monthsLeft - 1, mustQuery - 1, {}, found, sawUnusable);
            return;
        }
        // A record of ours without its huella may be the chain's newest: a first sync, which
        // has no local chain to trust, cannot know where it continues.
        if (sawUnusable && (found.isEmpty() || m_firstSync)) {
            syncFinished(false, tr("La AEAT devuelve registros de este emisor sin su huella o sin el sistema que "
                                   "los generó: no se puede saber dónde continúa la cadena."));
            return;
        }
        QString message;
        const bool ok = adoptTip(found + localCandidates(), &message);
        syncFinished(ok, message);
    });
}

// ---------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------

AeatRecord::Registration AeatDirectBackend::registrationFrom(const VerifactuInvoice &invoice,
                                                             const AeatRecord::PreviousRecord &previous,
                                                             const QDateTime &generatedAt)
{
    AeatRecord::Registration r;
    r.invoice     = { invoice.getSellerNIF(), invoice.getInvoiceNumber(), invoice.getInvoiceDate() };
    r.issuerName  = invoice.getSellerName();
    r.invoiceType = invoice.invoiceTypeCode();
    r.description = invoice.getDescription();
    for (const VerifactuTaxItem &item : invoice.getTaxItems())
        r.taxLines << AeatRecord::TaxLine{ item.getTaxRate(), item.getTaxBase(), item.getTaxAmount() };
    if (VerifactuInvoice::isRectificationInvoiceType(invoice.getInvoiceType()) && invoice.hasRectification()) {
        r.rectificationType = VerifactuInvoice::rectificationTypeToString(invoice.getRectificationType());
        for (const auto &rectified : invoice.getRectifiedInvoices())
            r.rectifiedInvoices << AeatRecord::InvoiceRef{ invoice.getSellerNIF(), rectified.first, rectified.second };
        // Substitution: the replaced invoice's amounts (ImporteRectificacion).
        if (invoice.getRectificationType() == VerifactuInvoice::BY_SUBSTITUTION) {
            r.hasRectifiedAmounts = true;
            r.rectifiedBase = invoice.getRectificationTaxBase();
            r.rectifiedTax = invoice.getRectificationTaxAmount();
        }
    }
    r.previous    = previous;
    r.generatedAt = generatedAt;
    return r;
}

void AeatDirectBackend::answerLater(const QString &requestId, const VerifactuResult &result)
{
    if (m_releasing) {                      // the backend goes away: a queued answer would be lost
        emit requestFinished(requestId, result);
        return;
    }
    QMetaObject::invokeMethod(this, [this, requestId, result]() {
        emit requestFinished(requestId, result);
    }, Qt::QueuedConnection);
}

bool AeatDirectBackend::storedOutcome(AeatStore::Kind kind, const QString &invoiceNumber, VerifactuResult *result)
{
    const AeatStore::Record record = m_store.latest(kind, invoiceNumber, m_config.issuerNif);
    if (!record.isValid() || record.state == AeatStore::kPending)
        return false;
    *result = resultFromStored(record);
    return true;
}

VerifactuResult AeatDirectBackend::resultFromStored(const AeatStore::Record &record) const
{
    VerifactuResult result;
    result.errorCode = record.errorCode;
    result.errorDescription = record.errorDescription;
    const bool ours = record.state == AeatStore::kAccepted;
    if (ours || record.state == AeatStore::kDuplicate) {
        result.status = VerifactuResult::SUCCESS;
        result.csv = record.csv;
    } else if (record.state == AeatStore::kRejected || record.state == AeatStore::kCancelledAtAeat) {
        result.status = VerifactuResult::ERROR;
    } else {
        result.status = VerifactuResult::PENDING;
    }
    // Our record is the invoice's only when AEAT registered ours.
    if (ours) {
        result.rawXml = record.xml;
        result.rawHash = record.hash;
    }
    if (result.isSuccess() && record.kind == AeatStore::Kind::Registration) {
        result.validationUrl = validationUrlOf(record.issuerNif, record.invoiceNumber, record.issueDate, record.total,
                                               m_config.testEnvironment);
        result.qrCode = qrPixmapOf(result.validationUrl);
    }
    return result;
}

QString AeatDirectBackend::storeAndQueue(
    AeatStore::Kind kind, const QString &invoiceNumber,
    const std::function<AeatStore::Built(const AeatRecord::PreviousRecord &, bool afterRejection)> &build)
{
    const QString requestId = nextRequestId();
    if (!isConfigured()) {
        VerifactuResult r;
        r.status = VerifactuResult::INVALID_CONFIG;
        r.errorDescription = m_configError;
        answerLater(requestId, r);
        return requestId;
    }
    if (m_chainReady) {
        storeAndQueueNow(requestId, kind, invoiceNumber, build);
        return requestId;
    }
    // No record before the chain is synced: this request waits for it.
    m_heldRequests << requestId;
    syncChain([this, requestId, kind, invoiceNumber, build](bool ok, const QString &message) {
        if (!m_heldRequests.removeOne(requestId))
            return;                         // already answered by releaseWaiters
        if (ok) {
            storeAndQueueNow(requestId, kind, invoiceNumber, build);
            return;
        }
        VerifactuResult r;
        r.status = VerifactuResult::NETWORK_ERROR;
        r.errorDescription = tr("No se ha generado el registro: la cadena no está preparada (%1)").arg(message);
        answerLater(requestId, r);
    });
    return requestId;
}

void AeatDirectBackend::storeAndQueueNow(
    const QString &requestId, AeatStore::Kind kind, const QString &invoiceNumber,
    const std::function<AeatStore::Built(const AeatRecord::PreviousRecord &, bool afterRejection)> &build)
{
    AeatStore::Record record = m_store.latest(kind, invoiceNumber, m_config.issuerNif);
    if (record.isValid() && (record.state == AeatStore::kAccepted || record.state == AeatStore::kDuplicate
                             || record.state == AeatStore::kCancelledAtAeat)) {
        // AEAT's answer about this invoice is known (e.g. the reply to an earlier send was lost).
        qDebug() << "AeatDirectBackend:" << operationOf(kind) << invoiceNumber << "already" << record.state
                 << "- answering from the store";
        answerLater(requestId, resultFromStored(record));
        return;
    }
    if (!record.isValid() || record.state == AeatStore::kRejected) {
        const bool afterRejection = record.isValid();
        record = m_store.append(kind, m_config.issuerNif, [&](const AeatRecord::PreviousRecord &previous) {
            return build(previous, afterRejection);
        });
        if (!record.isValid()) {
            VerifactuResult r;
            r.status = VerifactuResult::ERROR;
            r.errorDescription = tr("No se pudo guardar el registro de facturación: %1").arg(m_store.lastError());
            answerLater(requestId, r);
            return;
        }
        saveChainMarker();
        qDebug() << "AeatDirectBackend: new" << operationOf(kind) << "record" << record.id << "for" << invoiceNumber
                 << "huella" << record.hash << (afterRejection ? "(after a rejection)" : "");
    } else {
        qDebug() << "AeatDirectBackend: resending the stored" << operationOf(kind) << "record" << record.id << "for" << invoiceNumber;
    }
    m_waiters[record.id] << requestId;
    scheduleSend();
    refreshUnsentCount();
}

QString AeatDirectBackend::submitInvoiceAsync(const VerifactuInvoice &invoice)
{
    if (!invoice.isValid()) {
        const QString requestId = nextRequestId();
        VerifactuResult r;
        r.status = VerifactuResult::ERROR;
        r.errorDescription = invoice.getValidationError();
        answerLater(requestId, r);
        return requestId;
    }
    // The system block is read when the record is built: the chain sync a held request
    // waits for may change the installation number.
    return storeAndQueue(AeatStore::Kind::Registration, invoice.getInvoiceNumber(),
                         [this, invoice](const AeatRecord::PreviousRecord &previous, bool afterRejection) {
        AeatRecord::Registration r = registrationFrom(invoice, previous, QDateTime::currentDateTime());
        r.afterRejection = afterRejection;
        AeatStore::Built b;
        b.invoiceNumber = r.invoice.invoiceNumber;
        b.issueDate = r.invoice.issueDate;
        b.total = r.totalAmount();
        b.hash = AeatRecord::hashOf(r);
        b.xml = AeatRecord::registrationXml(r, m_config.system);
        b.generatedAt = r.generatedAt;
        return b;
    });
}

QString AeatDirectBackend::cancelInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate)
{
    const QString nif = m_config.issuerNif;
    return storeAndQueue(AeatStore::Kind::Cancellation, invoiceNumber,
                         [this, nif, invoiceNumber, invoiceDate](const AeatRecord::PreviousRecord &previous, bool afterRejection) {
        AeatRecord::Cancellation c;
        c.invoice = { nif, invoiceNumber, invoiceDate };
        c.previous = previous;
        c.generatedAt = QDateTime::currentDateTime();
        c.afterRejection = afterRejection;
        AeatStore::Built b;
        b.invoiceNumber = invoiceNumber;
        b.issueDate = invoiceDate;
        b.hash = AeatRecord::hashOf(c);
        b.xml = AeatRecord::cancellationXml(c, m_config.system);
        b.generatedAt = c.generatedAt;
        return b;
    });
}

QString AeatDirectBackend::generateQRAsync(const VerifactuInvoice &invoice)
{
    const QString requestId = nextRequestId();
    VerifactuResult r;
    r.status = VerifactuResult::SUCCESS;
    r.validationUrl = qrUrlFor(invoice);
    r.qrCode = qrFor(invoice);
    answerLater(requestId, r);
    return requestId;
}

QString AeatDirectBackend::qrUrlFor(const VerifactuInvoice &invoice) const
{
    return validationUrlOf(invoice.getSellerNIF(), invoice.getInvoiceNumber(), invoice.getInvoiceDate(),
                           invoice.getTotalAmount(), m_config.testEnvironment);
}

QPixmap AeatDirectBackend::qrFor(const VerifactuInvoice &invoice) const
{
    return qrPixmapOf(qrUrlFor(invoice));
}

bool AeatDirectBackend::holdsPendingRecord(AeatStore::Kind kind, const QString &invoiceNumber)
{
    return m_store.latest(kind, invoiceNumber, m_config.issuerNif).state == AeatStore::kPending;
}

QPixmap AeatDirectBackend::queuedRecordQr(const VerifactuInvoice &invoice)
{
    const AeatStore::Record r = m_store.latest(AeatStore::Kind::Registration, invoice.getInvoiceNumber(), m_config.issuerNif);
    // A stale record under a reused number, or another issuer's, never gets a QR printed.
    if (!r.isValid() || r.state != AeatStore::kPending || r.issuerNif != m_config.issuerNif
        || r.issueDate != invoice.getInvoiceDate()
        || AeatHash::amountText(r.total) != AeatHash::amountText(invoice.getTotalAmount()))
        return QPixmap();
    return qrPixmapOf(validationUrlOf(r.issuerNif, r.invoiceNumber, r.issueDate, r.total, m_config.testEnvironment));
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

QString AeatDirectBackend::queryInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate)
{
    const QString requestId = nextRequestId();
    if (!isConfigured() || !m_sendError.isEmpty()) {
        VerifactuRemoteRecord unavailable;
        unavailable.errorCode = isConfigured() ? m_sendError : m_configError;
        QMetaObject::invokeMethod(this, [this, requestId, unavailable]() {
            emit queryFinished(requestId, unavailable);
        }, Qt::QueuedConnection);
        return requestId;
    }
    // AEAT files the record under the month of its issue date: the caller's, else the
    // stored record's; without either, this month and then the previous one.
    const AeatStore::Record stored = m_store.latest(AeatStore::Kind::Registration, invoiceNumber, m_config.issuerNif);
    const QDate known = invoiceDate.isValid() ? invoiceDate : (stored.isValid() ? stored.issueDate : QDate());
    queryInvoiceMonth(requestId, invoiceNumber, known.isValid() ? known : QDate::currentDate(), !known.isValid(), invoiceDate);
    return requestId;
}

void AeatDirectBackend::queryInvoiceMonth(const QString &requestId, const QString &invoiceNumber, QDate month, bool tryPrevious,
                                          const QDate &invoiceDate)
{
    const QString envelope = AeatRecord::queryEnvelope(m_config.issuerName, m_config.issuerNif,
                                                       month.year(), month.month(), invoiceNumber);
    m_transport->post(endpointUrl(m_config.testEnvironment), envelope.toUtf8(),
                      [this, requestId, invoiceNumber, month, tryPrevious, invoiceDate](const AeatTransport::Response &response) {
        if (!response.error.isEmpty())
            qWarning() << "AeatDirectBackend: query of" << invoiceNumber << "failed -" << response.error;
        VerifactuRemoteRecord remote = AeatResponse::remoteRecordFor(AeatResponse::parseQuery(response.body), invoiceNumber, invoiceDate);
        remote.raw = QString::fromUtf8(response.body);
        if (remote.parsed && !remote.found && tryPrevious) {
            queryInvoiceMonth(requestId, invoiceNumber, month.addMonths(-1), false, invoiceDate);
            return;
        }
        emit queryFinished(requestId, remote);
    });
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

void AeatDirectBackend::scheduleSend()
{
    if (m_inFlight || m_sendTimer.isActive())
        return;
    m_sendTimer.start(int(msecsUntilNextSend()));
}

void AeatDirectBackend::refreshUnsentCount(bool incident)
{
    resumeDeferredRestart();           // called after every outcome: the outbox may be empty now
    m_incident = m_incident || incident;
    const int unsent = m_incident ? m_store.pendingCount(m_config.issuerNif) : 0;
    if (unsent < 0) {
        // Unreadable now: the notice stays as it is, never taken for "all sent".
        qWarning() << "AeatDirectBackend: pending records not counted -" << m_store.lastError();
        return;
    }
    if (unsent == 0)
        m_incident = false;
    if (unsent == m_unsentCount)
        return;
    m_unsentCount = unsent;
    emit unsentRecordsChanged(unsent);
}

void AeatDirectBackend::sendPending()
{
    if (m_inFlight)
        return;
    QList<AeatStore::Record> records;
    for (const AeatStore::Record &record : m_store.pending(1000, m_config.issuerNif))
        if (!m_verifying.contains(record.id))
            records << record;
    // After a fault on a batch, its records go one at a time until each has an answer.
    if (!m_sendSingly.isEmpty() && !records.isEmpty()) {
        const auto single = std::find_if(records.cbegin(), records.cend(), [this](const AeatStore::Record &r) {
            return m_sendSingly.contains(r.id);
        });
        if (single == records.cend()) {
            m_sendSingly.clear();
        } else {
            const AeatStore::Record one = *single;
            records = { one };
        }
    }
    if (records.isEmpty())
        return;
    if (!m_sendError.isEmpty()) {
        // Generated and kept; sent once the certificate is fixed (the app restarts the backend).
        VerifactuResult r;
        r.status = VerifactuResult::NETWORK_ERROR;
        r.errorDescription = tr("Registro guardado, pendiente de envío: %1").arg(m_sendError);
        for (const AeatStore::Record &record : records) {
            if (!record.delayed)
                m_store.markDelayed(record.id);
            for (const QString &requestId : m_waiters.take(record.id))
                emit requestFinished(requestId, r);
        }
        refreshUnsentCount(true);
        return;
    }
    QStringList xmls;
    m_inFlightIds.clear();
    m_inFlightRecords.clear();
    bool afterIncident = false;
    for (const AeatStore::Record &record : records) {
        afterIncident = afterIncident || delayedByIncident(record, m_sessionStart);
        xmls << record.xml;
        m_inFlightIds << record.id;
        m_inFlightRecords.insert(record.id, record);
        m_store.markAttempt(record.id);
        m_store.markSending(record.id);
    }
    const QString envelope = AeatRecord::submissionEnvelope(m_config.issuerName, m_config.issuerNif, xmls, afterIncident);
    qDebug() << "AeatDirectBackend: sending" << records.size() << "record(s) to" << endpointUrl(m_config.testEnvironment)
             << (afterIncident ? "(Incidencia = S)" : "");
    m_inFlight = true;
    m_transport->post(endpointUrl(m_config.testEnvironment), envelope.toUtf8(),
                      [this](const AeatTransport::Response &response) { onSubmissionReply(response); });
}

bool AeatDirectBackend::delayedByIncident(const AeatStore::Record &record, const QDateTime &sessionStart)
{
    return record.delayed || (record.generatedAt.isValid() && record.generatedAt < sessionStart);
}

void AeatDirectBackend::releaseWaiters()
{
    m_releasing = true;
    VerifactuResult stored;
    stored.status = VerifactuResult::PENDING;
    stored.errorDescription = tr("Registro guardado, pendiente de envío: se enviará con la nueva configuración");
    for (auto it = m_waiters.cbegin(); it != m_waiters.cend(); ++it)
        for (const QString &requestId : it.value())
            emit requestFinished(requestId, stored);
    m_waiters.clear();
    VerifactuResult held;
    held.status = VerifactuResult::NETWORK_ERROR;
    held.errorDescription = tr("No se ha generado el registro: la configuración cambió antes de comprobar la cadena");
    const QStringList requests = m_heldRequests;
    m_heldRequests.clear();
    for (const QString &requestId : requests)
        emit requestFinished(requestId, held);
    m_releasing = false;
}

void AeatDirectBackend::answerWaiters(qint64 recordId, const VerifactuResult &result)
{
    for (const QString &requestId : m_waiters.take(recordId))
        emit requestFinished(requestId, result);
}

void AeatDirectBackend::finishRecord(const AeatStore::Record &record, const VerifactuResult &result, bool claimRecord)
{
    VerifactuResult answer = result;
    // The invoice's record is ours only when AEAT registered ours.
    answer.rawXml = claimRecord ? record.xml : QString();
    answer.rawHash = claimRecord ? record.hash : QString();
    if (answer.isSuccess() && record.kind == AeatStore::Kind::Registration) {
        answer.validationUrl = validationUrlOf(record.issuerNif, record.invoiceNumber, record.issueDate, record.total,
                                               m_config.testEnvironment);
        answer.qrCode = qrPixmapOf(answer.validationUrl);
    }
    const QStringList waiters = m_waiters.take(record.id);
    for (const QString &requestId : waiters)
        emit requestFinished(requestId, answer);
    // Every definitive outcome, also when a dialog waited: it may have closed meanwhile
    // (the app never rewrites a settled row, so a second application changes nothing).
    if (answer.status == VerifactuResult::SUCCESS || answer.status == VerifactuResult::ERROR) {
        m_unverifiedDuplicates.remove(record.id);
        emit recordSettled(record.invoiceNumber, record.kind == AeatStore::Kind::Cancellation, answer);
    }
}

void AeatDirectBackend::settleRecord(const AeatStore::Record &record, const QString &state,
                                     const VerifactuResult &result, bool claimRecord)
{
    if (m_store.markOutcome(record.id, state, result.csv, result.errorCode, result.errorDescription)) {
        finishRecord(record, result, claimRecord);
        return;
    }
    const AeatStore::Record stored = m_store.byId(record.id);
    if (!stored.isValid() || stored.state == AeatStore::kPending) {
        // The write failed (not "settled meanwhile"): the record stays pending and is sent again later.
        qWarning() << "AeatDirectBackend: AEAT's answer for record" << record.id << "of" << record.invoiceNumber
                   << "could not be stored -" << m_store.lastError();
        VerifactuResult failed;
        failed.status = VerifactuResult::NETWORK_ERROR;
        failed.errorDescription = tr("La AEAT respondió, pero no se pudo guardar la respuesta: el registro sigue "
                                     "pendiente y se volverá a enviar");
        answerWaiters(record.id, failed);
        return;
    }
    qWarning() << "AeatDirectBackend: record" << record.id << "of" << record.invoiceNumber
               << "was no longer pending - AEAT's answer not applied";
    m_unverifiedDuplicates.remove(record.id);
    const AeatStore::Record now = m_store.latest(record.kind, record.invoiceNumber, m_config.issuerNif);
    answerWaiters(record.id, resultFromStored(now.isValid() ? now : record));
}

AeatChainCheck::Result AeatDirectBackend::verifyStoredChain()
{
    const QStringList xmls = m_store.recordXmls();
    bool tracked = false;
    QSet<QString> anchors = m_store.chainAnchors(&tracked);
    const QSet<QString> restarts = m_store.chainRestarts();
    if (!tracked) {
        // A store from before the anchors: what it continues today is taken as given, once.
        const AeatChainCheck::Result lenient = AeatChainCheck::verify(xmls, nullptr, restarts);
        if (!lenient.ok)
            return lenient;
        for (const QString &hash : lenient.continuationHashes)
            if (m_store.addChainAnchor(m_config.issuerNif, hash))
                anchors.insert(hash);
        m_store.markAnchorsTracked();
    }
    AeatChainCheck::Result result = AeatChainCheck::verify(xmls, &anchors, restarts);
    bool headRead = false;
    const QString head = m_store.chainHead(m_config.issuerNif, &headRead).hash;
    if (result.ok && headRead) {
        const AeatChainCheck::Result tail = AeatChainCheck::verifyTail(xmls, m_config.issuerNif, head, anchors);
        if (!tail.ok) {
            result.ok = false;
            result.problem = tail.problem;
            result.failedIndex = tail.failedIndex;
        }
    }
    return result;
}

QString AeatDirectBackend::expiryWarning(const QDateTime &expiry, const QDateTime &now, int days)
{
    if (!expiry.isValid() || expiry > now.addDays(days))
        return QString();
    return expiry < now ? tr("El certificado de la conexión directa con la AEAT caducó el %1: instale el nuevo y "
                             "elíjalo en Configuración.").arg(expiry.toString(kDbDateFormat))
                        : tr("El certificado de la conexión directa con la AEAT caduca el %1: renuévelo antes, o los "
                             "registros dejarán de enviarse.").arg(expiry.toString(kDbDateFormat));
}

void AeatDirectBackend::onSubmissionReply(const AeatTransport::Response &response)
{
    const QByteArray &body = response.body;
    const QString transportError = response.error;
    m_inFlight = false;
    const QList<qint64> sent = m_inFlightIds;
    m_inFlightIds.clear();

    const AeatResponse::Submission parsed = AeatResponse::parseSubmission(body);
    // Which records AEAT may hold: decided before this send is counted (see the duplicate case).
    QHash<qint64, bool> reachedBefore;
    for (const AeatStore::Record &r : m_store.pending(1000, m_config.issuerNif))
        reachedBefore.insert(r.id, r.reached);
    // No AEAT answer, or AEAT's own service failed: the outcome is unknown, so the
    // records stay pending and are sent unchanged again on their own.
    const bool transient = !parsed.parsed || (parsed.fault && parsed.faultCode.contains(QLatin1String("Server")));
    // A batch AEAT refused whole reached none of its records (isolateFaultedBatch reverts this send).
    const bool batchRefused = !transient && parsed.fault && sent.size() > 1;
    for (qint64 id : sent) {
        if (batchRefused)
            continue;
        if (response.requestSent || parsed.parsed)
            m_store.markReached(id);
        else
            m_store.markNotSent(id);
    }
    if (transient) {
        // A certificate that expired while the app was open: say so, instead of retrying forever.
        if (m_certificate.info().expiry.isValid() && m_certificate.info().expiry < QDateTime::currentDateTime()) {
            m_sendError = tr("el certificado caducó el %1; instale el nuevo y elíjalo en Configuración")
                              .arg(m_certificate.info().expiry.toString(kDbDateFormat));
            qWarning() << "AeatDirectBackend: the certificate expired -" << m_sendError;
        }
        const QString why = parsed.fault ? parsed.faultText : (transportError.isEmpty() ? tr("respuesta no reconocida") : transportError);
        qWarning() << "AeatDirectBackend: submission without an AEAT answer -" << why << "- retrying in" << s_retryDelaySeconds << "s";
        m_nextSendAllowed = QDateTime::currentDateTime().addSecs(s_retryDelaySeconds);
        VerifactuResult r;
        r.status = VerifactuResult::NETWORK_ERROR;
        r.errorDescription = tr("Sin respuesta de la AEAT: %1").arg(why);
        for (qint64 id : sent) {
            m_store.markDelayed(id);
            for (const QString &requestId : m_waiters.take(id))
                emit requestFinished(requestId, r);
        }
        scheduleSend();
        refreshUnsentCount(true);
        return;
    }

    m_nextSendAllowed = QDateTime::currentDateTime().addSecs(parsed.fault ? s_retryDelaySeconds : parsed.waitSeconds);
    if (batchRefused) {
        isolateFaultedBatch(sent, parsed.faultText, reachedBefore);
        return;
    }
    for (qint64 id : sent)
        m_sendSingly.removeOne(id);
    qDebug() << "AeatDirectBackend: reply" << (parsed.fault ? QStringLiteral("SOAP fault ") + parsed.faultText : parsed.sendState)
             << "CSV" << parsed.csv << "- next submission in" << secondsUntilNextSend() << "s";
    QHash<qint64, AeatStore::Record> pendingById;
    for (const AeatStore::Record &r : m_store.pending(1000, m_config.issuerNif))
        pendingById.insert(r.id, r);
    bool unanswered = false;
    for (qint64 id : sent) {
        const AeatStore::Record record = pendingById.value(id);
        if (!record.isValid()) {
            // Settled meanwhile: its waiters still hear the invoice's stored answer.
            m_unverifiedDuplicates.remove(id);
            const AeatStore::Record sentRecord = m_inFlightRecords.value(id);
            const AeatStore::Record now = m_store.latest(sentRecord.kind, sentRecord.invoiceNumber, m_config.issuerNif);
            if (m_waiters.contains(id))
                answerWaiters(id, now.isValid() ? resultFromStored(now) : resultFromStored(sentRecord));
            continue;
        }
        const QString operation = operationOf(record.kind);
        const AeatResponse::Line *line = parsed.fault ? nullptr : parsed.lineFor(record.invoiceNumber, operation);
        if (!parsed.fault && !line) {
            // AEAT did not report on it: still unknown, keep it pending.
            unanswered = true;
            m_store.markDelayed(record.id);
            VerifactuResult r;
            r.status = VerifactuResult::PENDING;
            r.errorDescription = tr("La AEAT no ha respondido sobre el registro %1").arg(record.invoiceNumber);
            finishRecord(record, r);
            continue;
        }
        VerifactuResult result = AeatResponse::resultFor(parsed, record.invoiceNumber, operation);
        if (line && line->duplicate && record.kind == AeatStore::Kind::Cancellation) {
            // A cancellation AEAT already holds (its reply was lost and the record resent):
            // the invoice is cancelled, which is what was asked. Ours only when an earlier
            // send of it reached AEAT.
            const bool ours = reachedBefore.value(record.id, false);
            result.status = VerifactuResult::SUCCESS;
            settleRecord(record, ours ? AeatStore::kAccepted : AeatStore::kDuplicate, result, ours);
        } else if (line && line->duplicate && line->duplicateState == QLatin1String("Anulada")) {
            // AEAT holds the invoice as cancelled: never register it again.
            result.status = VerifactuResult::ERROR;
            result.errorDescription = tr("La AEAT tiene la factura %1 anulada.").arg(record.invoiceNumber);
            result.csv.clear();
            settleRecord(record, AeatStore::kCancelledAtAeat, result, false);
        } else if (line && line->duplicate && result.isSuccess()) {
            verifyDuplicate(record, result, reachedBefore.value(record.id, false));
        } else {
            settleRecord(record, result.isSuccess() ? AeatStore::kAccepted : AeatStore::kRejected, result);
        }
    }
    if (unanswered)
        m_nextSendAllowed = qMax(m_nextSendAllowed, QDateTime::currentDateTime().addSecs(s_retryDelaySeconds));
    // Records created meanwhile, or left unanswered, go next.
    if (!m_store.pending(1, m_config.issuerNif).isEmpty())
        scheduleSend();
    refreshUnsentCount(unanswered);
}

void AeatDirectBackend::isolateFaultedBatch(const QList<qint64> &sent, const QString &faultText,
                                            const QHash<qint64, bool> &reachedBefore)
{
    // AEAT refused the whole message (often one malformed record): nothing was judged, so
    // no record is refused yet. One by one, the faulty record is refused alone.
    qWarning() << "AeatDirectBackend: SOAP fault on a batch of" << sent.size() << "records -" << faultText
               << "- resending them one by one";
    m_sendSingly = sent;
    // AEAT judged none of them: this send does not count as reached, so a later duplicate is
    // never taken for ours (one reached by an earlier send keeps it).
    for (qint64 id : sent)
        if (!reachedBefore.value(id))
            m_store.markNotSent(id);
    VerifactuResult r;
    r.status = VerifactuResult::PENDING;
    r.errorDescription = tr("La AEAT rechazó el envío conjunto (%1): se reenvía registro a registro").arg(faultText);
    for (qint64 id : sent)
        answerWaiters(id, r);
    scheduleSend();
}

void AeatDirectBackend::verifyDuplicate(const AeatStore::Record &record, const VerifactuResult &result, bool reachedBefore)
{
    m_verifying.insert(record.id);
    const QString envelope = AeatRecord::queryEnvelope(m_config.issuerName, m_config.issuerNif, record.issueDate.year(),
                                                       record.issueDate.month(), record.invoiceNumber);
    m_transport->post(endpointUrl(m_config.testEnvironment), envelope.toUtf8(),
                      [this, record, result, reachedBefore](const AeatTransport::Response &response) {
        m_verifying.remove(record.id);
        const AeatResponse::Query query = AeatResponse::parseQuery(response.body);
        const QString date = record.issueDate.toString(kDbDateFormat);
        const AeatResponse::QueryRecord *held = nullptr;
        for (const AeatResponse::QueryRecord &q : query.records)
            if (q.invoiceNumber == record.invoiceNumber && q.issueDate == date)
                held = &q;
        // An empty or unreadable amount is missing evidence, never "another amount".
        bool amountRead = false;
        const double heldTotal = held ? held->totalAmount.trimmed().toDouble(&amountRead) : 0.0;
        // AEAT answers "duplicate" but its query never shows the record: after a few rounds
        // an ERROR to check by hand, never resend -> duplicate -> query for ever.
        if (query.parsed && !query.fault && !held && ++m_unverifiedDuplicates[record.id] >= kMaxUnverifiedDuplicates) {
            VerifactuResult r = result;
            r.status = VerifactuResult::ERROR;
            r.csv.clear();
            r.errorDescription = tr("La AEAT dice que ya tiene la factura %1 del %2, pero no la devuelve al consultarla: "
                                    "compruébela con Consultar en AEAT.").arg(record.invoiceNumber, date);
            qWarning() << "AeatDirectBackend: duplicate of" << record.invoiceNumber << "never shown by the query - marked ERROR";
            settleRecord(record, AeatStore::kRejected, r, false);
            refreshUnsentCount();
            return;
        }
        if (!query.parsed || query.fault || !held || !amountRead) {
            // AEAT's record could not be read: still unknown, the record stays pending.
            qWarning() << "AeatDirectBackend: duplicate of" << record.invoiceNumber << "not verified -"
                       << (response.error.isEmpty() ? query.faultText : response.error);
            m_nextSendAllowed = qMax(m_nextSendAllowed, QDateTime::currentDateTime().addSecs(s_retryDelaySeconds));
            VerifactuResult r;
            r.status = VerifactuResult::NETWORK_ERROR;
            r.errorDescription = tr("La AEAT ya tiene la factura %1, pero no se ha podido comprobar su importe")
                                     .arg(record.invoiceNumber);
            answerWaiters(record.id, r);
            scheduleSend();
            // As for a submission: no answer, or AEAT's own service failing, is an incident.
            const bool incident = !query.parsed || (query.fault && query.faultCode.contains(QLatin1String("Server")));
            if (incident)
                m_store.markDelayed(record.id);
            refreshUnsentCount(incident);
            return;
        }
        if (qAbs(heldTotal - record.total) > 0.005) {
            VerifactuResult r = result;
            r.status = VerifactuResult::ERROR;
            r.csv.clear();
            r.errorDescription = tr("La AEAT ya tiene la factura %1 del %2 con otro importe (%3 €, aquí %4 €): "
                                    "no es esta factura.")
                                     .arg(record.invoiceNumber, date, held->totalAmount, AeatHash::amountText(record.total));
            qWarning() << "AeatDirectBackend: duplicate of" << record.invoiceNumber << "with another amount -" << held->totalAmount;
            settleRecord(record, AeatStore::kRejected, r, false);
        } else {
            // Ours only when an earlier send of it got to AEAT (the reply lost) and AEAT's
            // huella, when given, is our record's; else another system's record.
            const bool ours = reachedBefore && (held->hash.isEmpty() || held->hash.compare(record.hash, Qt::CaseInsensitive) == 0);
            settleRecord(record, ours ? AeatStore::kAccepted : AeatStore::kDuplicate, result, ours);
        }
        if (!m_store.pending(1, m_config.issuerNif).isEmpty())
            scheduleSend();
        refreshUnsentCount();
    });
}
