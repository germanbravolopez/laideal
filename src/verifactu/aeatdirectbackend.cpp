#include "aeatdirectbackend.h"

#include "aeathash.h"
#include "aeatqr.h"
#include "aeatresponse.h"

#include <QDebug>
#include <QPixmap>

namespace {

const QString kProductionUrl = QStringLiteral("https://www1.agenciatributaria.gob.es/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP");
const QString kTestUrl = QStringLiteral("https://prewww1.aeat.es/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP");
const QString kRegistration = QStringLiteral("Alta");
const QString kCancellation = QStringLiteral("Anulacion");
const int kTransferTimeoutMs = 30000;
const int kRetryAfterFailureSeconds = 60;    // after a failed send, before the next try
const int kQrPixelsPerModule = 5;

QString s_endpointOverride;

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

    if (m_config.issuerNif.trimmed().isEmpty() || m_config.issuerName.trimmed().isEmpty())
        m_configError = tr("Falta el NIF o el nombre del emisor.");
    else if (!m_store.ensureSchema())
        m_configError = tr("No se pueden crear las tablas de registros AEAT: %1").arg(m_store.lastError());
    else if (s_endpointOverride.isEmpty()) {
        QString error;
        const bool loaded = m_config.certificateThumbprint.isEmpty()
            ? m_certificate.loadFromFile(m_config.certificatePath, m_config.certificatePassword, &error)
            : m_certificate.loadFromStore(m_config.certificateThumbprint, &error);
        if (loaded)
            m_transport->setCertificate(&m_certificate);
        else
            m_configError = error;
    }
    m_transport->setTimeoutMs(kTransferTimeoutMs);
    if (!m_configError.isEmpty()) {
        qWarning() << "AeatDirectBackend: not configured -" << m_configError;
        return;
    }
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

QString AeatDirectBackend::configurationInfo() const
{
    return QStringLiteral("AEAT directo - entorno: %1, emisor: %2, endpoint: %3, %4")
        .arg(m_config.testEnvironment ? QStringLiteral("PRUEBAS") : QStringLiteral("PRODUCCION"),
             m_config.issuerNif, endpointUrl(m_config.testEnvironment),
             isConfigured() ? QStringLiteral("configurado") : QStringLiteral("NO configurado: ") + m_configError);
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

void AeatDirectBackend::continueChainFromAeat(const QStringList &localRecordXmls, const ChainDone &done)
{
    if (!isConfigured()) {
        done(false, m_configError);
        return;
    }
    const AeatRecord::PreviousRecord head = m_store.chainHead(m_config.issuerNif);
    if (!head.isFirst()) {
        done(true, tr("La cadena ya continúa tras el registro %1.").arg(head.invoice.invoiceNumber));
        return;
    }
    QList<AeatResponse::RecordSummary> local;
    for (const QString &xml : localRecordXmls) {
        const AeatResponse::RecordSummary s = AeatResponse::summarizeRecord(xml);
        if (s.valid && s.issuerNif == m_config.issuerNif)
            local << s;
    }
    const QDate today = QDate::currentDate();
    queryMonth(QDate(today.year(), today.month(), 1), 24, {}, local, done);
}

void AeatDirectBackend::queryMonth(QDate month, int monthsLeft, AeatRecord::InvoiceRef pageAfter,
                                   QList<AeatResponse::RecordSummary> found, const ChainDone &done)
{
    const QString envelope = AeatRecord::queryEnvelope(m_config.issuerName, m_config.issuerNif, month.year(),
                                                       month.month(), QString(), pageAfter);
    m_transport->post(endpointUrl(m_config.testEnvironment), envelope.toUtf8(),
                      [=](const AeatTransport::Response &response) mutable {
        const AeatResponse::Query reply = AeatResponse::parseQuery(response.body);
        if (!reply.parsed || reply.fault) {
            done(false, tr("No se pudo consultar la AEAT: %1")
                            .arg(reply.fault ? reply.faultText : (response.error.isEmpty() ? tr("respuesta no reconocida") : response.error)));
            return;
        }
        for (const AeatResponse::QueryRecord &r : reply.records) {
            AeatResponse::RecordSummary s;
            s.valid = r.hash.size() == 64 && !r.generatedAt.isEmpty();
            s.operation = QStringLiteral("Alta");
            s.issuerNif = r.issuerNif;
            s.invoiceNumber = r.invoiceNumber;
            s.issueDate = r.issueDate;
            s.hash = r.hash;
            s.generatedAt = r.generatedAt;
            s.previousHash = r.previousHash;
            if (s.valid)
                found << s;
        }
        if (reply.morePages && !reply.records.isEmpty()) {
            const AeatResponse::QueryRecord &last = reply.records.last();
            queryMonth(month, monthsLeft,
                       { last.issuerNif, last.invoiceNumber, QDate::fromString(last.issueDate, "dd-MM-yyyy") }, found, done);
            return;
        }
        // Keep looking back until a month has records at AEAT.
        if (!reply.hasData && monthsLeft > 1) {
            queryMonth(month.addMonths(-1), monthsLeft - 1, {}, found, done);
            return;
        }
        const AeatResponse::RecordSummary *newest = AeatResponse::chainTip(found);
        if (!newest) {
            done(true, tr("La AEAT no tiene registros de este emisor: la cadena empezará con el primer registro."));
            return;
        }
        const AeatRecord::PreviousRecord head{
            { m_config.issuerNif, newest->invoiceNumber, QDate::fromString(newest->issueDate, "dd-MM-yyyy") }, newest->hash };
        if (!seedChain(head)) {
            done(false, tr("No se pudo guardar el inicio de la cadena."));
            return;
        }
        done(true, tr("La cadena continúa tras el registro %1 (%2, %3).")
                       .arg(newest->invoiceNumber, newest->operation == QLatin1String("Alta") ? tr("alta") : tr("anulación"),
                            newest->generatedAt));
    });
}

int AeatDirectBackend::secondsUntilNextSend() const
{
    return int((msecsUntilNextSend() + 999) / 1000);    // rounded up: never earlier than AEAT allows
}

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
    result.rawXml = record.xml;
    result.rawHash = record.hash;
    result.errorCode = record.errorCode;
    result.errorDescription = record.errorDescription;
    if (record.state == AeatStore::kAccepted) {
        result.status = VerifactuResult::SUCCESS;
        result.csv = record.csv;
    } else if (record.state == AeatStore::kRejected) {
        result.status = VerifactuResult::ERROR;
    } else {
        result.status = VerifactuResult::PENDING;
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
    AeatStore::Record record = m_store.latest(kind, invoiceNumber);
    if (record.isValid() && record.state == AeatStore::kAccepted) {
        // AEAT already holds it (the answer to an earlier send was lost): same outcome again.
        qDebug() << "AeatDirectBackend:" << operationOf(kind) << invoiceNumber << "already accepted, answering from the store";
        answerLater(requestId, resultFromStored(record));
        return requestId;
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
            return requestId;
        }
        qDebug() << "AeatDirectBackend: new" << operationOf(kind) << "record" << record.id << "for" << invoiceNumber
                 << "huella" << record.hash << (afterRejection ? "(after a rejection)" : "");
    } else {
        qDebug() << "AeatDirectBackend: resending the stored" << operationOf(kind) << "record" << record.id << "for" << invoiceNumber;
    }
    m_waiters[record.id] << requestId;
    scheduleSend();
    return requestId;
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

QString AeatDirectBackend::queryInvoiceAsync(const QString &invoiceNumber)
{
    const QString requestId = nextRequestId();
    if (!isConfigured()) {
        QMetaObject::invokeMethod(this, [this, requestId]() {
            emit queryFinished(requestId, VerifactuRemoteRecord());
        }, Qt::QueuedConnection);
        return requestId;
    }
    // The query needs the month of the invoice: the stored record's, else this month.
    const AeatStore::Record stored = m_store.latest(AeatStore::Kind::Registration, invoiceNumber);
    const QDate period = stored.isValid() ? stored.issueDate : QDate::currentDate();
    const QString envelope = AeatRecord::queryEnvelope(m_config.issuerName, m_config.issuerNif,
                                                       period.year(), period.month(), invoiceNumber);
    m_transport->post(endpointUrl(m_config.testEnvironment), envelope.toUtf8(),
                      [this, requestId, invoiceNumber](const AeatTransport::Response &response) {
        if (!response.error.isEmpty())
            qWarning() << "AeatDirectBackend: query of" << invoiceNumber << "failed -" << response.error;
        VerifactuRemoteRecord remote = AeatResponse::remoteRecordFor(AeatResponse::parseQuery(response.body), invoiceNumber);
        remote.raw = QString::fromUtf8(response.body);
        emit queryFinished(requestId, remote);
    });
    return requestId;
}

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

void AeatDirectBackend::finishRecord(const AeatStore::Record &record, const VerifactuResult &result)
{
    VerifactuResult answer = result;
    answer.rawXml = record.xml;
    answer.rawHash = record.hash;
    if (answer.isSuccess() && record.kind == AeatStore::Kind::Registration) {
        answer.validationUrl = AeatHash::qrValidationUrl(record.issuerNif, record.invoiceNumber, record.issueDate,
                                                         AeatHash::amountText(record.total), m_config.testEnvironment);
        answer.qrCode = QPixmap::fromImage(AeatQr::image(answer.validationUrl, kQrPixelsPerModule));
    }
    for (const QString &requestId : m_waiters.take(record.id))
        emit requestFinished(requestId, answer);
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
    // records stay pending and are sent unchanged next time.
    const bool transient = !parsed.parsed || (parsed.fault && parsed.faultCode.contains(QLatin1String("Server")));
    if (transient) {
        const QString why = parsed.fault ? parsed.faultText : (transportError.isEmpty() ? tr("respuesta no reconocida") : transportError);
        qWarning() << "AeatDirectBackend: submission without an AEAT answer -" << why;
        m_nextSendAllowed = QDateTime::currentDateTime().addSecs(kRetryAfterFailureSeconds);
        VerifactuResult r;
        r.status = VerifactuResult::NETWORK_ERROR;
        r.errorDescription = tr("Sin respuesta de la AEAT: %1").arg(why);
        for (qint64 id : sent) {
            for (const QString &requestId : m_waiters.take(id))
                emit requestFinished(requestId, r);
        }
        return;
    }

    m_nextSendAllowed = QDateTime::currentDateTime().addSecs(parsed.fault ? kRetryAfterFailureSeconds : parsed.waitSeconds);
    qDebug() << "AeatDirectBackend: reply" << (parsed.fault ? QStringLiteral("SOAP fault ") + parsed.faultText : parsed.sendState)
             << "CSV" << parsed.csv << "- next submission in" << secondsUntilNextSend() << "s";
    QHash<qint64, AeatStore::Record> pendingById;
    for (const AeatStore::Record &r : m_store.pending(1000))
        pendingById.insert(r.id, r);
    for (qint64 id : sent) {
        const AeatStore::Record record = pendingById.value(id);
        if (!record.isValid())
            continue;
        const QString operation = operationOf(record.kind);
        if (!parsed.fault && !parsed.lineFor(record.invoiceNumber, operation)) {
            // AEAT did not report on it: still unknown, keep it pending.
            VerifactuResult r;
            r.status = VerifactuResult::PENDING;
            r.errorDescription = tr("La AEAT no ha respondido sobre el registro %1").arg(record.invoiceNumber);
            finishRecord(record, r);
            continue;
        }
        const VerifactuResult result = AeatResponse::resultFor(parsed, record.invoiceNumber, operation);
        m_store.markOutcome(record.id, result.isSuccess() ? AeatStore::kAccepted : AeatStore::kRejected,
                            result.csv, result.errorCode, result.errorDescription);
        finishRecord(record, result);
    }
    // Records created while this submission was in flight go next.
    for (const AeatStore::Record &r : m_store.pending(1000)) {
        if (m_waiters.contains(r.id)) {
            scheduleSend();
            break;
        }
    }
}
