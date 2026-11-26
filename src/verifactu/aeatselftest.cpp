#include "aeatselftest.h"

#include "aeathash.h"

#include <QDebug>
#include <QTimer>

namespace {
const int kSettleTimeoutMs = 5 * 60 * 1000;   // a record left pending: how long AEAT's answer is awaited
}

AeatSelfTest::AeatSelfTest(AeatDirectBackend::Config config, const QSqlDatabase &db, QObject *parent)
    : QObject(parent)
{
    config.testEnvironment = true;    // never production
    m_backend = new AeatDirectBackend(config, db, this);
    connect(m_backend, &VerifactuBackend::recordSettled, this, &AeatSelfTest::recordSettled);
    m_issueDate = QDate::currentDate();
    m_invoiceNumber = QStringLiteral("PRUEBA-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmsszzz"));
    m_settleTimer.setSingleShot(true);
    connect(&m_settleTimer, &QTimer::timeout, this, [this] {
        disconnect(m_settled);
        fail("waiting for AEAT", tr("%1: la AEAT no ha respondido en %2 minutos; el registro de prueba queda "
                                    "pendiente en el entorno de pruebas.").arg(m_invoiceNumber).arg(kSettleTimeoutMs / 60000));
    });
}

void AeatSelfTest::fail(const char *step, const QString &text)
{
    // The text may name the certificate's holder and NIF: only the step goes to the log.
    qWarning() << "AeatSelfTest: failed at the" << step << "step";
    emit progress(false, text);
    emit finished(false);
}

void AeatSelfTest::run()
{
    if (!m_backend->isConfigured()) {
        fail("configuration", tr("Configuración: %1").arg(m_backend->configurationError()));
        return;
    }
    const AeatCertificate::Info cert = m_backend->certificateInfo();
    emit progress(true, cert.subject.isEmpty()
                            ? tr("Configuración correcta (servidor de pruebas local).")
                            : tr("Certificado: %1, válido hasta el %2.").arg(cert.subject, cert.expiry.toString(QLatin1String(AeatHash::kDateFormat))));
    const QString warning = AeatCertificate::representativeWarning(cert.nif, m_backend->issuerNif());
    if (!warning.isEmpty())
        emit progress(false, warning);
    m_backend->syncChain([this](bool ok, const QString &message) {
        if (!ok) {
            fail("chain sync", tr("Cadena de registros: %1").arg(message));
            return;
        }
        emit progress(true, tr("Cadena de registros: %1").arg(message));
        submit();
    });
}

bool AeatSelfTest::awaitSettlement(bool cancellation, const VerifactuResult &result)
{
    // A record kept pending (no answer yet) is sent again on its own: its outcome decides.
    if (result.status != VerifactuResult::PENDING && result.status != VerifactuResult::NETWORK_ERROR)
        return false;
    if (!m_backend->holdsPendingRecord(cancellation ? AeatStore::Kind::Cancellation : AeatStore::Kind::Registration,
                                       m_invoiceNumber))
        return false;
    // Nothing will send the record (an unusable certificate): waiting would only delay the failure.
    if (!m_backend->sendingError().isEmpty()) {
        fail("sending", tr("%1: no se puede enviar a la AEAT (%2). El registro de prueba queda pendiente y no se "
                           "enviará hasta que se corrija.").arg(m_invoiceNumber, m_backend->sendingError()));
        return true;
    }
    emit progress(true, tr("%1 de %2: la AEAT aún no ha respondido (%3); se reintenta solo...")
                            .arg(cancellation ? tr("Anulación") : tr("Alta"), m_invoiceNumber, result.errorDescription));
    m_settled = connect(m_backend, &VerifactuBackend::recordSettled, this,
                        [this, cancellation](const QString &invoice, bool isCancellation, const VerifactuResult &r) {
        if (invoice != m_invoiceNumber || isCancellation != cancellation)
            return;
        m_settleTimer.stop();
        disconnect(m_settled);
        if (cancellation)
            cancellationAnswered(r);
        else
            registrationAnswered(r);
    });
    m_settleTimer.start(kSettleTimeoutMs);
    return true;
}

void AeatSelfTest::submit()
{
    VerifactuInvoice invoice;
    invoice.setInvoiceNumber(m_invoiceNumber);
    invoice.setInvoiceDate(m_issueDate);
    invoice.setInvoiceType(VerifactuInvoice::SIMPLIFIED);
    invoice.setSellerNIF(m_backend->issuerNif());
    invoice.setSellerName(m_backend->issuerName());
    invoice.setDescription(tr("Prueba de conexión directa con la AEAT"));
    VerifactuTaxItem item;
    item.setTaxRate(21.0);
    item.setTaxBase(1.00);
    item.setTaxAmount(0.21);
    invoice.addTaxItem(item);
    invoice.calculateTotals();
    connect(m_backend, &VerifactuBackend::requestFinished, this, [this](const QString &id, const VerifactuResult &r) {
        if (id != m_pendingRequest)
            return;
        m_pendingRequest.clear();
        disconnect(m_backend, &VerifactuBackend::requestFinished, this, nullptr);
        if (!awaitSettlement(false, r))
            registrationAnswered(r);
    });
    m_pendingRequest = m_backend->submitInvoiceAsync(invoice);
}

void AeatSelfTest::registrationAnswered(const VerifactuResult &r)
{
    if (!r.isSuccess()) {
        fail("registration", tr("Alta de %1: %2 %3").arg(m_invoiceNumber, r.errorCode, r.errorDescription));
        return;
    }
    emit progress(true, tr("Alta de %1 registrada por la AEAT, CSV %2.").arg(m_invoiceNumber, r.csv));
    query();
}

void AeatSelfTest::query()
{
    connect(m_backend, &VerifactuBackend::queryFinished, this, [this](const QString &id, const VerifactuRemoteRecord &remote) {
        if (id != m_pendingRequest)
            return;
        m_pendingRequest.clear();
        disconnect(m_backend, &VerifactuBackend::queryFinished, this, nullptr);
        if (!remote.hasUsableCsv() || qAbs(remote.totalAmount - 1.21) > 0.005) {
            fail("query", tr("Consulta de %1: la AEAT no devuelve el registro aceptado (%2).")
                              .arg(m_invoiceNumber, remote.found ? remote.statusResponse : tr("no encontrado")));
            return;
        }
        emit progress(true, tr("Consulta: la AEAT tiene %1 por %2 €.").arg(m_invoiceNumber, QString::number(remote.totalAmount, 'f', 2).replace('.', ',')));
        cancel();
    });
    m_pendingRequest = m_backend->queryInvoiceAsync(m_invoiceNumber);
}

void AeatSelfTest::cancel()
{
    const int wait = m_backend->secondsUntilNextSend();
    if (wait > 0)
        emit progress(true, tr("Esperando %1 s, el tiempo que pide la AEAT entre envíos...").arg(wait));
    connect(m_backend, &VerifactuBackend::requestFinished, this, [this](const QString &id, const VerifactuResult &r) {
        if (id != m_pendingRequest)
            return;
        m_pendingRequest.clear();
        disconnect(m_backend, &VerifactuBackend::requestFinished, this, nullptr);
        if (!awaitSettlement(true, r))
            cancellationAnswered(r);
    });
    m_pendingRequest = m_backend->cancelInvoiceAsync(m_invoiceNumber, m_issueDate);
}

void AeatSelfTest::cancellationAnswered(const VerifactuResult &r)
{
    if (!r.isSuccess()) {
        fail("cancellation", tr("Anulación de %1: %2 %3").arg(m_invoiceNumber, r.errorCode, r.errorDescription));
        return;
    }
    emit progress(true, tr("Anulación de %1 registrada por la AEAT. La conexión directa funciona.").arg(m_invoiceNumber));
    emit finished(true);
}
