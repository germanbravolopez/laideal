#include "aeatdirectbackend.h"

#include "aeathash.h"
#include "aeatqr.h"
#include "aeatresponse.h"

#include <QDebug>
#include <QPixmap>
#include <QSet>

namespace {

const QString kProductionUrl = QStringLiteral("https://www1.agenciatributaria.gob.es/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP");
const QString kTestUrl = QStringLiteral("https://prewww1.aeat.es/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP");
const QString kRegistration = QStringLiteral("Alta");
const QString kCancellation = QStringLiteral("Anulacion");
const QString kDateFormat = QStringLiteral("dd-MM-yyyy");
const int kTransferTimeoutMs = 30000;
const int kQrPixelsPerModule = 5;
const int kMonthsBack = 24;           // how far the first sync looks for the issuer's records
const int kMonthsAlwaysQueried = 2;   // this month and the previous one, always

QString s_endpointOverride;
int s_retryDelaySeconds = 60;         // after a failed send, before trying again on its own

QString operationOf(AeatStore::Kind kind)
{
    return kind == AeatStore::Kind::Registration ? kRegistration : kCancellation;
}

} // namespace

AeatDirectBackend::AeatDirectBackend(const Config &config, const QSqlDatabase &db, QObject *parent)
    : VerifactuBackend(parent),
      m_config(config),
      m_store(db, config.testEnvironment ? QStringLiteral("pruebas") : QStringLiteral("produccion")),
      m_transport(new AeatTransport(this))
{
    m_sendTimer.setSingleShot(true);
    connect(&m_sendTimer, &QTimer::timeout, this, &AeatDirectBackend::sendPending);
    m_transport->setTimeoutMs(kTransferTimeoutMs);

    if (m_config.issuerNif.trimmed().isEmpty() || m_config.issuerName.trimmed().isEmpty())
        m_configError = tr("Falta el NIF o el nombre del emisor.");
    else if (!m_store.ensureSchema())
        m_configError = tr("No se pueden crear las tablas de registros AEAT: %1").arg(m_store.lastError());
    if (!m_configError.isEmpty()) {
        qWarning() << "AeatDirectBackend: not configured -" << m_configError;
        return;
    }
    // The certificate is only needed to send: records are generated without it.
    if (endpointUrl(m_config.testEnvironment).startsWith(QLatin1String("https"))) {
        QString error;
        const bool loaded = m_config.certificateThumbprint.isEmpty()
            ? m_certificate.loadFromFile(m_config.certificatePath, m_config.certificatePassword, &error)
            : m_certificate.loadFromStore(m_config.certificateThumbprint, &error);
        if (loaded)
            m_transport->setCertificate(&m_certificate);
        else
            m_sendError = error;
    }
    if (!m_sendError.isEmpty())
        qWarning() << "AeatDirectBackend: records will be generated but not sent -" << m_sendError;
    // Records left unsent when the app closed go first.
    if (!m_store.pending(1).isEmpty())
        scheduleSend();
}

AeatDirectBackend::~AeatDirectBackend() = default;

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

QString AeatDirectBackend::configurationInfo() const
{
    return QStringLiteral("AEAT directo - entorno: %1, emisor: %2, endpoint: %3, %4%5")
        .arg(m_config.testEnvironment ? QStringLiteral("PRUEBAS") : QStringLiteral("PRODUCCION"),
             m_config.issuerNif, endpointUrl(m_config.testEnvironment),
             isConfigured() ? QStringLiteral("configurado") : QStringLiteral("NO configurado: ") + m_configError,
             m_sendError.isEmpty() ? QString() : QStringLiteral(", sin envío: ") + m_sendError);
}

QString AeatDirectBackend::nextRequestId()
{
    return QStringLiteral("aeat-%1").arg(++m_requestCounter);
}

bool AeatDirectBackend::seedChain(const AeatRecord::PreviousRecord &lastGatewayRecord)
{
    return m_store.seedChainHead(m_config.issuerNif, lastGatewayRecord);
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
    QList<AeatResponse::RecordSummary> out;
    const QStringList own = m_store.recordXmls();
    for (const QString &xml : m_gatewayXmls + own) {
        const AeatResponse::RecordSummary s = AeatResponse::summarizeRecord(xml);
        if (s.valid && s.issuerNif == m_config.issuerNif)
            out << s;
    }
    return out;
}

bool AeatDirectBackend::adoptTip(const QList<AeatResponse::RecordSummary> &found, QString *message)
{
    bool read = false;
    const AeatRecord::PreviousRecord head = m_store.chainHead(m_config.issuerNif, &read);
    if (!read) {
        *message = tr("No se puede leer la cadena de registros: %1").arg(m_store.lastError());
        return false;
    }
    QList<AeatResponse::RecordSummary> candidates = found;
    QSet<QString> hashes;
    for (const AeatResponse::RecordSummary &c : candidates)
        hashes.insert(c.hash);
    // The current head takes part too: a newer record chains to it, or it is the tip.
    if (!head.isFirst() && !hashes.contains(head.hash)) {
        AeatResponse::RecordSummary h;
        h.valid = true;
        h.issuerNif = m_config.issuerNif;
        h.invoiceNumber = head.invoice.invoiceNumber;
        h.issueDate = head.invoice.issueDate.toString(kDateFormat);
        h.hash = head.hash;
        h.generatedAt = head.generatedAt;
        candidates << h;
    }
    const AeatResponse::RecordSummary *tip = AeatResponse::chainTip(candidates);
    if (!tip) {
        *message = tr("No hay registros anteriores de este emisor: la cadena empezará con el primer registro.");
        return true;
    }
    if (tip->hash != head.hash) {
        const AeatRecord::PreviousRecord moved{
            { m_config.issuerNif, tip->invoiceNumber, QDate::fromString(tip->issueDate, kDateFormat) }, tip->hash, tip->generatedAt };
        if (!m_store.setChainHead(m_config.issuerNif, moved)) {
            *message = tr("No se pudo guardar el punto de la cadena.");
            return false;
        }
    }
    *message = tr("La cadena continúa tras el registro %1 (%2, %3).")
                   .arg(tip->invoiceNumber, tip->operation == kCancellation ? tr("anulación") : tr("alta"),
                        tip->generatedAt.isEmpty() ? tr("generado antes") : tip->generatedAt);
    return true;
}

void AeatDirectBackend::continueChainFromAeat(const QStringList &localRecordXmls, const ChainDone &done)
{
    setGatewayRecordXmls(localRecordXmls);
    syncChain(done);
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
    if (m_store.chainSynced(m_config.issuerNif)) {
        // Synced with AEAT before: the gateway's stored records and ours are enough.
        QString message;
        const bool ok = adoptTip(localCandidates(), &message);
        syncFinished(ok, message);
        return;
    }
    if (!m_sendError.isEmpty()) {
        syncFinished(false, tr("Antes del primer registro hay que consultar la AEAT, y no se puede: %1").arg(m_sendError));
        return;
    }
    const QDate today = QDate::currentDate();
    queryMonth(QDate(today.year(), today.month(), 1), kMonthsBack, kMonthsAlwaysQueried, {}, {}, false);
}

void AeatDirectBackend::syncFinished(bool ok, const QString &message)
{
    m_syncRunning = false;
    if (ok) {
        m_chainReady = true;
        m_store.markChainSynced(m_config.issuerNif);
    }
    qDebug() << "AeatDirectBackend: chain sync" << (ok ? "done -" : "failed -") << message;
    const QList<ChainDone> waiters = m_syncWaiters;
    m_syncWaiters.clear();
    for (const ChainDone &w : waiters)
        w(ok, message);
}

void AeatDirectBackend::queryMonth(QDate month, int monthsLeft, int mustQuery, AeatRecord::InvoiceRef pageAfter,
                                   QList<AeatResponse::RecordSummary> found, bool sawUnusable)
{
    const QString envelope = AeatRecord::queryEnvelope(m_config.issuerName, m_config.issuerNif, month.year(),
                                                       month.month(), QString(), pageAfter);
    m_transport->post(endpointUrl(m_config.testEnvironment), envelope.toUtf8(),
                      [=](const AeatTransport::Response &response) mutable {
        const AeatResponse::Query reply = AeatResponse::parseQuery(response.body);
        if (!reply.parsed || reply.fault) {
            syncFinished(false, tr("No se pudo consultar la AEAT: %1")
                                    .arg(reply.fault ? reply.faultText : (response.error.isEmpty() ? tr("respuesta no reconocida") : response.error)));
            return;
        }
        for (const AeatResponse::QueryRecord &r : reply.records) {
            AeatResponse::RecordSummary s;
            s.valid = r.hash.size() == 64 && !r.generatedAt.isEmpty();
            s.operation = kRegistration;
            s.issuerNif = r.issuerNif;
            s.invoiceNumber = r.invoiceNumber;
            s.issueDate = r.issueDate;
            s.hash = r.hash;
            s.generatedAt = r.generatedAt;
            s.previousHash = r.previousHash;
            if (s.valid)
                found << s;
            else
                sawUnusable = true;
        }
        if (reply.morePages && !reply.records.isEmpty()) {
            const AeatResponse::QueryRecord &last = reply.records.last();
            queryMonth(month, monthsLeft, mustQuery,
                       { last.issuerNif, last.invoiceNumber, QDate::fromString(last.issueDate, kDateFormat) },
                       found, sawUnusable);
            return;
        }
        // This month and the previous one always (a record generated now may be dated
        // last month); further back only while nothing has been found.
        if (monthsLeft > 1 && (mustQuery > 1 || found.isEmpty()) && !(found.isEmpty() && sawUnusable)) {
            queryMonth(month.addMonths(-1), monthsLeft - 1, mustQuery - 1, {}, found, sawUnusable);
            return;
        }
        if (found.isEmpty() && sawUnusable) {
            syncFinished(false, tr("La AEAT devuelve registros de este emisor sin su huella: no se puede saber "
                                   "dónde continúa la cadena."));
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
    QMetaObject::invokeMethod(this, [this, requestId, result]() {
        emit requestFinished(requestId, result);
    }, Qt::QueuedConnection);
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
        result.validationUrl = AeatHash::qrValidationUrl(record.issuerNif, record.invoiceNumber, record.issueDate,
                                                         AeatHash::amountText(record.total), m_config.testEnvironment);
        result.qrCode = QPixmap::fromImage(AeatQr::image(result.validationUrl, kQrPixelsPerModule));
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
    syncChain([this, requestId, kind, invoiceNumber, build](bool ok, const QString &message) {
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
    AeatStore::Record record = m_store.latest(kind, invoiceNumber);
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
        qDebug() << "AeatDirectBackend: new" << operationOf(kind) << "record" << record.id << "for" << invoiceNumber
                 << "huella" << record.hash << (afterRejection ? "(after a rejection)" : "");
    } else {
        qDebug() << "AeatDirectBackend: resending the stored" << operationOf(kind) << "record" << record.id << "for" << invoiceNumber;
    }
    m_waiters[record.id] << requestId;
    scheduleSend();
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
    const AeatRecord::SystemInfo system = m_config.system;
    return storeAndQueue(AeatStore::Kind::Registration, invoice.getInvoiceNumber(),
                         [invoice, system](const AeatRecord::PreviousRecord &previous, bool afterRejection) {
        AeatRecord::Registration r = registrationFrom(invoice, previous, QDateTime::currentDateTime());
        r.afterRejection = afterRejection;
        AeatStore::Built b;
        b.invoiceNumber = r.invoice.invoiceNumber;
        b.issueDate = r.invoice.issueDate;
        b.total = r.totalAmount();
        b.hash = AeatRecord::hashOf(r);
        b.xml = AeatRecord::registrationXml(r, system);
        b.generatedAt = r.generatedAt;
        return b;
    });
}

QString AeatDirectBackend::cancelInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate)
{
    const QString nif = m_config.issuerNif;
    const AeatRecord::SystemInfo system = m_config.system;
    return storeAndQueue(AeatStore::Kind::Cancellation, invoiceNumber,
                         [=](const AeatRecord::PreviousRecord &previous, bool afterRejection) {
        AeatRecord::Cancellation c;
        c.invoice = { nif, invoiceNumber, invoiceDate };
        c.previous = previous;
        c.generatedAt = QDateTime::currentDateTime();
        c.afterRejection = afterRejection;
        AeatStore::Built b;
        b.invoiceNumber = invoiceNumber;
        b.issueDate = invoiceDate;
        b.hash = AeatRecord::hashOf(c);
        b.xml = AeatRecord::cancellationXml(c, system);
        b.generatedAt = c.generatedAt;
        return b;
    });
}

QString AeatDirectBackend::generateQRAsync(const VerifactuInvoice &invoice)
{
    const QString requestId = nextRequestId();
    VerifactuResult r;
    r.status = VerifactuResult::SUCCESS;
    r.validationUrl = AeatHash::qrValidationUrl(invoice.getSellerNIF(), invoice.getInvoiceNumber(), invoice.getInvoiceDate(),
                                                AeatHash::amountText(invoice.getTotalAmount()), m_config.testEnvironment);
    r.qrCode = QPixmap::fromImage(AeatQr::image(r.validationUrl, kQrPixelsPerModule));
    answerLater(requestId, r);
    return requestId;
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

QString AeatDirectBackend::queryInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate)
{
    const QString requestId = nextRequestId();
    if (!isConfigured() || !m_sendError.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, requestId]() {
            emit queryFinished(requestId, VerifactuRemoteRecord());
        }, Qt::QueuedConnection);
        return requestId;
    }
    // AEAT files the record under the month of its issue date: the caller's, else the
    // stored record's; without either, this month and then the previous one.
    const AeatStore::Record stored = m_store.latest(AeatStore::Kind::Registration, invoiceNumber);
    const QDate known = invoiceDate.isValid() ? invoiceDate : (stored.isValid() ? stored.issueDate : QDate());
    queryInvoiceMonth(requestId, invoiceNumber, known.isValid() ? known : QDate::currentDate(), !known.isValid());
    return requestId;
}

void AeatDirectBackend::queryInvoiceMonth(const QString &requestId, const QString &invoiceNumber, QDate month, bool tryPrevious)
{
    const QString envelope = AeatRecord::queryEnvelope(m_config.issuerName, m_config.issuerNif,
                                                       month.year(), month.month(), invoiceNumber);
    m_transport->post(endpointUrl(m_config.testEnvironment), envelope.toUtf8(),
                      [this, requestId, invoiceNumber, month, tryPrevious](const AeatTransport::Response &response) {
        if (!response.error.isEmpty())
            qWarning() << "AeatDirectBackend: query of" << invoiceNumber << "failed -" << response.error;
        VerifactuRemoteRecord remote = AeatResponse::remoteRecordFor(AeatResponse::parseQuery(response.body), invoiceNumber);
        remote.raw = QString::fromUtf8(response.body);
        if (remote.parsed && !remote.found && tryPrevious) {
            queryInvoiceMonth(requestId, invoiceNumber, month.addMonths(-1), false);
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

void AeatDirectBackend::sendPending()
{
    if (m_inFlight)
        return;
    const QList<AeatStore::Record> records = m_store.pending(1000);
    if (records.isEmpty())
        return;
    if (!m_sendError.isEmpty()) {
        // Generated and kept; sent once the certificate is fixed (the app restarts the backend).
        VerifactuResult r;
        r.status = VerifactuResult::NETWORK_ERROR;
        r.errorDescription = tr("Registro guardado, pendiente de envío: %1").arg(m_sendError);
        for (const AeatStore::Record &record : records)
            for (const QString &requestId : m_waiters.take(record.id))
                emit requestFinished(requestId, r);
        return;
    }
    QStringList xmls;
    m_inFlightIds.clear();
    for (const AeatStore::Record &record : records) {
        xmls << record.xml;
        m_inFlightIds << record.id;
        m_store.markAttempt(record.id);
    }
    const QString envelope = AeatRecord::submissionEnvelope(m_config.issuerName, m_config.issuerNif, xmls);
    qDebug() << "AeatDirectBackend: sending" << records.size() << "record(s) to" << endpointUrl(m_config.testEnvironment);
    m_inFlight = true;
    m_transport->post(endpointUrl(m_config.testEnvironment), envelope.toUtf8(),
                      [this](const AeatTransport::Response &response) { onSubmissionReply(response); });
}

void AeatDirectBackend::finishRecord(const AeatStore::Record &record, const VerifactuResult &result, bool claimRecord)
{
    VerifactuResult answer = result;
    // The invoice's record is ours only when AEAT registered ours.
    answer.rawXml = claimRecord ? record.xml : QString();
    answer.rawHash = claimRecord ? record.hash : QString();
    if (answer.isSuccess() && record.kind == AeatStore::Kind::Registration) {
        answer.validationUrl = AeatHash::qrValidationUrl(record.issuerNif, record.invoiceNumber, record.issueDate,
                                                         AeatHash::amountText(record.total), m_config.testEnvironment);
        answer.qrCode = QPixmap::fromImage(AeatQr::image(answer.validationUrl, kQrPixelsPerModule));
    }
    const QStringList waiters = m_waiters.take(record.id);
    for (const QString &requestId : waiters)
        emit requestFinished(requestId, answer);
    // Nobody asked (e.g. sent at start): the app still records a definitive outcome.
    if (waiters.isEmpty() && (answer.status == VerifactuResult::SUCCESS || answer.status == VerifactuResult::ERROR))
        emit recordSettled(record.invoiceNumber, record.kind == AeatStore::Kind::Cancellation, answer);
}

void AeatDirectBackend::onSubmissionReply(const AeatTransport::Response &response)
{
    const QByteArray &body = response.body;
    const QString transportError = response.error;
    m_inFlight = false;
    const QList<qint64> sent = m_inFlightIds;
    m_inFlightIds.clear();

    const AeatResponse::Submission parsed = AeatResponse::parseSubmission(body);
    // No AEAT answer, or AEAT's own service failed: the outcome is unknown, so the
    // records stay pending and are sent unchanged again on their own.
    const bool transient = !parsed.parsed || (parsed.fault && parsed.faultCode.contains(QLatin1String("Server")));
    if (transient) {
        const QString why = parsed.fault ? parsed.faultText : (transportError.isEmpty() ? tr("respuesta no reconocida") : transportError);
        qWarning() << "AeatDirectBackend: submission without an AEAT answer -" << why << "- retrying in" << s_retryDelaySeconds << "s";
        m_nextSendAllowed = QDateTime::currentDateTime().addSecs(s_retryDelaySeconds);
        VerifactuResult r;
        r.status = VerifactuResult::NETWORK_ERROR;
        r.errorDescription = tr("Sin respuesta de la AEAT: %1").arg(why);
        for (qint64 id : sent) {
            for (const QString &requestId : m_waiters.take(id))
                emit requestFinished(requestId, r);
        }
        scheduleSend();
        return;
    }

    m_nextSendAllowed = QDateTime::currentDateTime().addSecs(parsed.fault ? s_retryDelaySeconds : parsed.waitSeconds);
    qDebug() << "AeatDirectBackend: reply" << (parsed.fault ? QStringLiteral("SOAP fault ") + parsed.faultText : parsed.sendState)
             << "CSV" << parsed.csv << "- next submission in" << secondsUntilNextSend() << "s";
    QHash<qint64, AeatStore::Record> pendingById;
    for (const AeatStore::Record &r : m_store.pending(1000))
        pendingById.insert(r.id, r);
    bool unanswered = false;
    for (qint64 id : sent) {
        const AeatStore::Record record = pendingById.value(id);
        if (!record.isValid())
            continue;
        const QString operation = operationOf(record.kind);
        const AeatResponse::Line *line = parsed.fault ? nullptr : parsed.lineFor(record.invoiceNumber, operation);
        if (!parsed.fault && !line) {
            // AEAT did not report on it: still unknown, keep it pending.
            unanswered = true;
            VerifactuResult r;
            r.status = VerifactuResult::PENDING;
            r.errorDescription = tr("La AEAT no ha respondido sobre el registro %1").arg(record.invoiceNumber);
            finishRecord(record, r);
            continue;
        }
        VerifactuResult result = AeatResponse::resultFor(parsed, record.invoiceNumber, operation);
        if (line && line->duplicate && line->duplicateState == QLatin1String("Anulada")) {
            // AEAT holds the invoice as cancelled: never register it again.
            result.status = VerifactuResult::ERROR;
            result.errorDescription = tr("La AEAT tiene la factura %1 anulada.").arg(record.invoiceNumber);
            m_store.markOutcome(record.id, AeatStore::kCancelledAtAeat, QString(), result.errorCode, result.errorDescription);
            finishRecord(record, result, false);
        } else if (line && line->duplicate && result.isSuccess()) {
            // A resend of our own record (sent before, the reply lost) is ours; on a
            // first send AEAT already held another system's record for the invoice.
            const bool ours = record.attempts > 1;
            m_store.markOutcome(record.id, ours ? AeatStore::kAccepted : AeatStore::kDuplicate,
                                result.csv, result.errorCode, result.errorDescription);
            finishRecord(record, result, ours);
        } else {
            m_store.markOutcome(record.id, result.isSuccess() ? AeatStore::kAccepted : AeatStore::kRejected,
                                result.csv, result.errorCode, result.errorDescription);
            finishRecord(record, result);
        }
    }
    if (unanswered)
        m_nextSendAllowed = qMax(m_nextSendAllowed, QDateTime::currentDateTime().addSecs(s_retryDelaySeconds));
    // Records created meanwhile, or left unanswered, go next.
    if (!m_store.pending(1).isEmpty())
        scheduleSend();
}
