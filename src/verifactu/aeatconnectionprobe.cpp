#include "aeatconnectionprobe.h"
#include "textformat.h"

#include "aeatdirectbackend.h"
#include "aeathash.h"
#include "aeatrecord.h"
#include "aeatresponse.h"

#include <QDate>
#include <QDebug>

namespace {
const int kProbeTimeoutMs = 15000;
}

AeatConnectionProbe::AeatConnectionProbe(const Settings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_transport(new AeatTransport(this))
{
    m_transport->setTimeoutMs(kProbeTimeoutMs);
}

void AeatConnectionProbe::fail(const char *step, const QString &text)
{
    // The text may name the certificate's holder and NIF: only the step goes to the log.
    qWarning() << "AeatConnectionProbe: failed at the" << step << "step";
    emit progress(false, text);
    emit finished(false);
}

void AeatConnectionProbe::run()
{
    if (m_settings.issuerNif.trimmed().isEmpty() || m_settings.issuerName.trimmed().isEmpty()) {
        fail("settings", tr("Falta el NIF o el nombre del emisor."));
        return;
    }
    const QString url = AeatDirectBackend::endpointUrl(m_settings.testEnvironment);
    // The tests' local server is plain http and takes no certificate.
    if (url.startsWith(QLatin1String("https"))) {
        QString error;
        if (!m_certificate.load(m_settings.certificateThumbprint, m_settings.certificatePath,
                                m_settings.certificatePassword, &error)) {
            fail("certificate", tr("Certificado: %1").arg(error));
            return;
        }
        const AeatCertificate::Info cert = m_certificate.info();
        emit progress(true, tr("Certificado: %1, válido hasta el %2.").arg(cert.subject, cert.expiry.toString(kDbDateFormat)));
        const QString warning = AeatCertificate::representativeWarning(cert.nif, m_settings.issuerNif);
        if (!warning.isEmpty())
            emit progress(false, warning);
        m_transport->setCertificate(&m_certificate);
    }
    const QDate today = QDate::currentDate();
    const QString envelope = AeatRecord::queryEnvelope(m_settings.issuerName, m_settings.issuerNif, today.year(), today.month());
    qDebug() << "AeatConnectionProbe: query to" << url;
    m_transport->post(url, envelope.toUtf8(), [this](const AeatTransport::Response &response) {
        bool ok = false;
        const QString text = verdict(response, &ok);
        if (!ok) {
            fail("query", text);
            return;
        }
        qDebug() << "AeatConnectionProbe: AEAT answered the query, HTTP" << response.httpStatus;
        emit progress(true, text);
        emit finished(true);
    });
}

QString AeatConnectionProbe::verdict(const AeatTransport::Response &response, bool *ok)
{
    *ok = false;
    if (!response.error.isEmpty())
        return tr("No se pudo conectar con la AEAT: %1").arg(response.error);
    const AeatResponse::Query reply = AeatResponse::parseQuery(response.body);
    if (reply.fault)
        return tr("La AEAT responde, pero rechaza la consulta: %1").arg(reply.faultText);
    if (!reply.parsed)
        return tr("La AEAT no ha dado una respuesta reconocible (HTTP %1): puede que no acepte el certificado.")
            .arg(response.httpStatus);
    *ok = true;
    if (reply.records.isEmpty())
        return tr("La AEAT responde: no tiene facturas del emisor este mes.");
    return reply.morePages ? tr("La AEAT responde: tiene más de %1 facturas del emisor este mes.").arg(reply.records.size())
                           : tr("La AEAT responde: tiene %1 factura(s) del emisor este mes.").arg(reply.records.size());
}
