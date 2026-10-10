#include "aeatselftest.h"

#include <QDebug>
#include <QTimer>

AeatSelfTest::AeatSelfTest(AeatDirectBackend::Config config, const QSqlDatabase &db, QObject *parent)
    : QObject(parent)
{
    config.testEnvironment = true;    // never production
    m_backend = new AeatDirectBackend(config, db, this);
    m_issueDate = QDate::currentDate();
    m_invoiceNumber = QStringLiteral("PRUEBA-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss"));
}

void AeatSelfTest::fail(const QString &text)
{
    qWarning() << "AeatSelfTest:" << text;
    emit progress(false, text);
    emit finished(false);
}

void AeatSelfTest::run()
{
    if (!m_backend->isConfigured()) {
        fail(tr("Configuración: %1").arg(m_backend->configurationError()));
        return;
    }
    const AeatCertificate::Info cert = m_backend->certificateInfo();
    emit progress(true, cert.subject.isEmpty()
                            ? tr("Configuración correcta (servidor de pruebas local).")
                            : tr("Certificado: %1, válido hasta el %2.").arg(cert.subject, cert.expiry.toString("dd-MM-yyyy")));
    m_backend->continueChainFromAeat({}, [this](bool ok, const QString &message) {
        if (!ok) {
            fail(tr("Cadena de registros: %1").arg(message));
            return;
        }
        emit progress(true, tr("Cadena de registros: %1").arg(message));
        submit();
    });
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
        if (!r.isSuccess()) {
            fail(tr("Alta de %1: %2 %3").arg(m_invoiceNumber, r.errorCode, r.errorDescription));
            return;
        }
        emit progress(true, tr("Alta de %1 registrada por la AEAT, CSV %2.").arg(m_invoiceNumber, r.csv));
        query();
    });
    m_pendingRequest = m_backend->submitInvoiceAsync(invoice);
}

void AeatSelfTest::query()
{
    connect(m_backend, &VerifactuBackend::queryFinished, this, [this](const QString &id, const VerifactuRemoteRecord &remote) {
        if (id != m_pendingRequest)
            return;
        m_pendingRequest.clear();
        disconnect(m_backend, &VerifactuBackend::queryFinished, this, nullptr);
        if (!remote.hasUsableCsv() || qAbs(remote.totalAmount - 1.21) > 0.005) {
            fail(tr("Consulta de %1: la AEAT no devuelve el registro aceptado (%2).")
                     .arg(m_invoiceNumber, remote.found ? remote.statusResponse : tr("no encontrado")));
            return;
        }
        emit progress(true, tr("Consulta: la AEAT tiene %1 por %2 €.").arg(m_invoiceNumber, QString::number(remote.totalAmount, 'f', 2)));
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
        if (!r.isSuccess()) {
            fail(tr("Anulación de %1: %2 %3").arg(m_invoiceNumber, r.errorCode, r.errorDescription));
            return;
        }
        emit progress(true, tr("Anulación de %1 registrada por la AEAT. La conexión directa funciona.").arg(m_invoiceNumber));
        emit finished(true);
    });
    m_pendingRequest = m_backend->cancelInvoiceAsync(m_invoiceNumber, m_issueDate);
}
