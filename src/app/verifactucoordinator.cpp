#include "verifactucoordinator.h"
#include "aeatconnectionprobe.h"
#include "aeatselftest.h"
#include "appsettings.h"
#include "sql_lite.h"
#include "uikit.h"
#include "updater.h"
#include "verifactuconfig.h"
#include "verifactumanager.h"
#include "verifactutypes.h"
#include <QApplication>
#include <QDialog>
#include <QEventLoop>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>

// The strings were MainWindow's: they keep its translation context.
#define MW_TR(...) QCoreApplication::translate("MainWindow", __VA_ARGS__)

namespace {

AeatConnectionProbe::Settings probeSettings(bool testEnvironment, const QString &nif, const QString &name,
                                            const QString &thumbprint, const QString &certificateFile,
                                            const QString &certificatePassword)
{
    AeatConnectionProbe::Settings settings;
    settings.testEnvironment = testEnvironment;
    settings.issuerNif = nif;
    settings.issuerName = name;
    settings.certificateThumbprint = thumbprint;
    settings.certificatePath = certificateFile;
    settings.certificatePassword = certificatePassword;
    return settings;
}

// The window of the AEAT tests: what the test does, the panel its progress goes to, Cerrar.
UiKit::ResultPanel *setUpAeatTestDialog(QDialog &dlg, const QString &title, const QString &intro,
                                        const QString &waiting, const char *panelName)
{
    UiKit::setUpDialog(&dlg, title, 620);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(UiKit::introPanel(intro));
    auto *panel = new UiKit::ResultPanel(waiting);
    panel->setObjectName(panelName);
    layout->addWidget(panel);
    layout->addLayout(UiKit::closeRow(&dlg));
    return panel;
}

// The progress lines of a test worker, shown in its panel. Declared before the worker:
// its progress slot uses it until the worker is gone.
class ProgressLines
{
public:
    explicit ProgressLines(UiKit::ResultPanel *panel) : m_panel(panel) {}
    void add(bool ok, const QString &text)
    {
        m_lines << (ok ? UiKit::okHtml(text.toHtmlEscaped()) : UiKit::errorHtml(text.toHtmlEscaped()));
        m_panel->setText(m_lines.join("<br>"));
    }

private:
    UiKit::ResultPanel *m_panel;
    QStringList m_lines;
};

} // namespace

VerifactuCoordinator::VerifactuCoordinator(const QSqlDatabase &db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

bool VerifactuCoordinator::initialize(bool *created)
{
    // One integration for the window's life, re-initialised after Configuración: the
    // windows that hold it keep working, and its connections are made once.
    const bool first = !m_integration;
    if (first)
        m_integration = new VerifactuIntegration(this);
    if (created)
        *created = first;

    const bool ok = m_integration->initialize();
    if (first) {
        connect(m_integration, &VerifactuIntegration::requestFinished, this, &VerifactuCoordinator::onRequestFinished);
        // Direct client: outcomes nobody waited for (records sent at start or retried on
        // their own) still settle their rows.
        connect(m_integration, &VerifactuIntegration::recordSettled, this, &VerifactuCoordinator::recordSettled);
        connect(m_integration, &VerifactuIntegration::unsentRecordsChanged, this, &VerifactuCoordinator::unsentRecordsChanged);
        connect(m_integration, &VerifactuIntegration::chainChecked, this, &VerifactuCoordinator::chainChecked);
    }
    return ok;
}

QString VerifactuCoordinator::submitInvoice(const QString &ticketNum, const QDate &invoiceDate,
                                            double totalAmount, int seq)
{
    if (!m_integration || !m_integration->isConfigured()) {
        qDebug() << "Verifactu not configured - skipping invoice submission for ticket" << ticketNum;
        return QString();
    }
    double ivaRate = AppSettings::instance()->ivaRate();
    // The InvoiceID carries the seq (bare n_recibo for seq 0, "<n>-<seq>" else)
    // so a recovered partial-pay event re-submits under the same ID it stored.
    const QString invoiceId = verifactuInvoiceId(ticketNum, seq);
    const QString reqId = m_integration->submitSimplifiedInvoiceAsync(
        invoiceId,
        invoiceDate,
        // Tax base from the IVA-inclusive ticket total (same split as the Facturas dialog).
        taxBaseFromGross(totalAmount, ivaRate),
        ivaRate,
        kInvoiceDescription
    );
    if (reqId.isEmpty()) {
        qWarning() << "Verifactu submit rejected for ticket" << invoiceId;
        return QString();
    }
    m_pendingSubmits.insert(reqId, { ticketNum, seq });
    emit statusMessage(MW_TR("Enviando ticket %1 a AEAT...").arg(invoiceId), 0);
    return reqId;
}

QPixmap VerifactuCoordinator::qrBeforeReply(const QString &ticketNum, const QDate &invoiceDate, double totalAmount,
                                            int seq) const
{
    if (!m_integration)
        return QPixmap();
    const double ivaRate = AppSettings::instance()->ivaRate();
    return m_integration->qrBeforeReply(verifactuInvoiceId(ticketNum, seq), invoiceDate,
                                        taxBaseFromGross(totalAmount, ivaRate), ivaRate);
}

void VerifactuCoordinator::markPrintedWithoutQr(const QString &reqId)
{
    auto it = m_pendingSubmits.find(reqId);
    if (it != m_pendingSubmits.end())
        it.value().printedWithoutQr = true;
}

VerifactuCoordinator::AeatReply VerifactuCoordinator::waitForReply(const QString &reqId, const QString &ticketNum)
{
    AeatReply reply;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QMetaObject::Connection conn = connect(m_integration,
        &VerifactuIntegration::requestFinished, this,
        [&](const QString &id, const VerifactuResult &res) {
            if (id != reqId) return;
            reply.refusal = rejectionHtml(verifactuInvoiceId(ticketNum, 0), res);
            reply.success = res.isSuccess() && !res.qrCode.isNull();
            reply.qrCode = res.qrCode;
            loop.quit();
        });
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(3000);
    emit aeatWaitStarted();
    m_waitingForAeat = true;
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    m_waitingForAeat = false;
    emit aeatWaitFinished();
    QObject::disconnect(conn);
    return reply;
}

void VerifactuCoordinator::onRequestFinished(const QString &requestId, const VerifactuResult &result)
{
    auto it = m_pendingSubmits.find(requestId);
    if (it == m_pendingSubmits.end()) return; // not one of ours (cancel/QR/etc. or other consumer)
    const QString ticketNum = it.value().ticketNum;
    const int     seq       = it.value().seq;
    const bool    lateReply = it.value().printedWithoutQr;
    m_pendingSubmits.erase(it);

    const int changed = updateTicketVerifactuFields(m_db, ticketNum, result, seq);
    if (changed <= 0) {
        emit statusMessage(changed == 0
            ? MW_TR("Respuesta de AEAT para el ticket %1 ignorada: la factura ya estaba registrada")
                  .arg(verifactuInvoiceId(ticketNum, seq))
            : MW_TR("No se pudo guardar la respuesta de AEAT del ticket %1").arg(verifactuInvoiceId(ticketNum, seq)),
            15000);
        return;
    }

    if (result.isSuccess()) {
        emit statusMessage(
            lateReply ? MW_TR("AEAT ha confirmado el ticket %1 - ya se puede imprimir la factura con QR "
                              "desde Recogida de Prendas").arg(verifactuInvoiceId(ticketNum, seq))
                      : MW_TR("Ticket %1 enviado a AEAT (CSV: %2)").arg(ticketNum, result.csv),
            lateReply ? 30000 : 10000);
    } else {
        emit statusMessage(
            MW_TR("Error al enviar ticket %1: %2").arg(ticketNum, result.errorDescription), 15000);
        qWarning() << "Verifactu submission failed for ticket" << ticketNum << "-" << result.errorDescription;
        emit submitRefused(verifactuInvoiceId(ticketNum, seq), result);
    }
    emit submitSettled();
}

QString VerifactuCoordinator::rejectionHtml(const QString &invoiceId, const VerifactuResult &result) const
{
    if (result.status != VerifactuResult::ERROR
        || verifactuErrorIsDuplicate(result.errorCode, result.errorDescription))
        return QString();
    return UiKit::errorHtml(MW_TR("AEAT ha rechazado la factura %1: %2")
                                .arg(invoiceId.toHtmlEscaped(), result.errorDescription.toHtmlEscaped()))
           + "<br>" + MW_TR("Reenvíela desde Recogida de Prendas (botón Verifactu…) después de corregir la causa.");
}

VerifactuCoordinator::Notice VerifactuCoordinator::aeatEnvironmentNotice() const
{
    // The direct client registers in AEAT's test environment unless production was chosen:
    // real sales must never go there unnoticed.
    Notice notice;
    const bool direct = m_integration && m_integration->isDirect();
    const QDate productionStart = direct ? m_integration->productionStartDate() : QDate();
    // In production too: which AEAT the tickets go to is always in sight, with the day it began.
    if (productionStart.isValid())
        notice.text = MW_TR("AEAT: PRODUCCIÓN desde el %1").arg(dbDateText(productionStart));
    else
        notice.text = UiKit::warnHtml(MW_TR("AEAT: entorno de PRUEBAS (sin efectos tributarios)"));
    notice.visible = direct;
    return notice;
}

VerifactuCoordinator::Notice VerifactuCoordinator::unsentRecordsNotice() const
{
    // Art. 16.4 Orden HAC/1177/2024: in view from the incident until every record is sent.
    Notice notice;
    const int unsent = m_integration ? m_integration->unsentRecordCount() : 0;
    notice.text = UiKit::warnHtml(MW_TR("%n registro(s) pendiente(s) de enviar a la AEAT", "", unsent));
    notice.visible = unsent > 0;
    return notice;
}

// The problem that stops production (empty when none): the certificate, and the real AEAT's answer.
QString VerifactuCoordinator::checkAeatProduction(QObject *context, const QString &nif, const QString &name,
                                                  const QString &thumbprint, const QString &certificateFile,
                                                  const QString &certificatePassword)
{
    AeatConnectionProbe probe(probeSettings(false, nif, name, thumbprint, certificateFile, certificatePassword));
    QString problem;
    bool done = false;
    connect(&probe, &AeatConnectionProbe::progress, context, [&problem](bool ok, const QString &text) {
        if (!ok)
            problem = text;
    });
    QEventLoop loop;
    connect(&probe, &AeatConnectionProbe::finished, &loop, [&done, &loop, &problem](bool ok) {
        done = true;
        if (ok)
            problem.clear();             // a warning on the way (the certificate's NIF) does not stop it
        loop.quit();
    });
    QApplication::setOverrideCursor(Qt::WaitCursor);
    probe.run();
    if (!done)
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    QApplication::restoreOverrideCursor();
    return problem;
}

void VerifactuCoordinator::testGatewayConnection(QWidget *parent, const QString &nif, const QString &name,
                                                 const QString &serviceKey, bool production)
{
    if (nif.isEmpty() || name.isEmpty() || serviceKey.isEmpty()) {
        QMessageBox::warning(parent, MW_TR("Datos incompletos"),
            MW_TR("Introduce NIF, nombre y clave de servicio antes de probar la conexión."));
        return;
    }
    VerifactuManager mgr;
    mgr.getConfig()->setEmitterData(nif, name);
    mgr.getConfig()->setServiceKey(serviceKey);
    mgr.getConfig()->setEnvironment(production
        ? VerifactuConfig::PRODUCTION
        : VerifactuConfig::TESTING);

    VerifactuResult result = mgr.testConnection();
    const QString env = production ? MW_TR("PRODUCCIÓN") : MW_TR("TESTING");
    const QString endpoint = mgr.getConfig()->getEndpointUrl();
    if (result.isSuccess()) {
        QMessageBox::information(parent, MW_TR("Conexión correcta"),
            MW_TR("Servidor accesible (%1).\n\nEntorno: %2\nNIF: %3\nEndpoint: %4")
                .arg(result.errorDescription, env, nif, endpoint));
    } else if (result.status == VerifactuResult::ERROR) {
        // The server answered and refused the key (testConnection's only ERROR).
        QMessageBox::warning(parent, MW_TR("Clave rechazada"),
            MW_TR("El servidor responde, pero ha rechazado la clave de servicio: compruébela.\n\n"
                  "Detalle: %1\nEntorno: %2\nEndpoint: %3")
                .arg(result.errorDescription, env, endpoint));
    } else {
        QMessageBox::warning(parent, MW_TR("Conexión fallida"),
            MW_TR("No se pudo conectar al servidor.\n\nDetalle: %1\nEntorno: %2\nEndpoint: %3")
                .arg(result.errorDescription, env, endpoint));
    }
}

void VerifactuCoordinator::runAeatConnectionTest(QWidget *parent, const QString &nif, const QString &name,
                                                 const QString &thumbprint, const QString &certificateFile,
                                                 const QString &certificatePassword, bool production)
{
    QDialog dlg(parent);
    UiKit::ResultPanel *panel = setUpAeatTestDialog(dlg, MW_TR("Probar conexión con la AEAT"),
        production ? MW_TR("Comprueba el certificado elegido y consulta a la AEAT real (PRODUCCIÓN) las facturas del "
                           "emisor de este mes. Solo consulta: no se registra ni se envía nada.")
                   : MW_TR("Comprueba el certificado elegido y consulta al entorno de pruebas de la AEAT las facturas "
                           "del emisor de este mes. Solo consulta: no se registra ni se envía nada."),
        MW_TR("Conectando con la AEAT..."), "lblAeatConnectionTest");

    ProgressLines lines(panel);
    AeatConnectionProbe probe(probeSettings(!production, nif, name, thumbprint, certificateFile, certificatePassword));
    connect(&probe, &AeatConnectionProbe::progress, &dlg, [&lines](bool ok, const QString &text) { lines.add(ok, text); });
    QTimer::singleShot(0, &probe, &AeatConnectionProbe::run);
    dlg.exec();
}

void VerifactuCoordinator::runAeatSelfTest(QWidget *parent, const QString &nif, const QString &name,
                                           const QString &thumbprint, const QString &certificateFile,
                                           const QString &certificatePassword)
{
    if (nif.isEmpty() || name.isEmpty()) {
        QMessageBox::warning(parent, MW_TR("Datos incompletos"), MW_TR("Introduce el NIF y el nombre del emisor antes de la prueba."));
        return;
    }
    QDialog dlg(parent);
    UiKit::ResultPanel *panel = setUpAeatTestDialog(dlg, MW_TR("Ensayo en el entorno de pruebas de la AEAT"),
        MW_TR("Registra, consulta y anula una factura de prueba en el entorno de pruebas "
              "de la AEAT, sin efectos tributarios, con el certificado elegido."),
        MW_TR("Conectando con la AEAT..."), "lblSelfTest");

    AeatDirectBackend::Config c;
    c.testEnvironment = true;
    c.issuerNif = nif;
    c.issuerName = name;
    c.system = AeatRecord::laIdealSystem(Updater::currentVersion(), QString());
    c.certificateThumbprint = thumbprint;
    c.certificatePath = certificateFile;
    c.certificatePassword = certificatePassword;
    AppSettings *settings = AppSettings::instance();
    const bool shopInPreProduction = VerifactuIntegration::selfTestSharesShopChain(settings->verifactuDirectAeat(),
                                                                                 settings->aeatDirectProduction());
    // The shop's chain only when it is the one the self-test works on.
    c.adoptInstallationNumber = shopInPreProduction;
    if (m_integration)
        c.sessionStart = m_integration->sessionStart();
    if (shopInPreProduction) {
        c.readChainMarker  = [] { return AppSettings::instance()->aeatChainMarker(); };
        c.writeChainMarker = [](const QString &marker) { AppSettings::instance()->setAeatChainMarker(marker); };
    }
    ProgressLines lines(panel);
    AeatSelfTest selfTest(c, m_db);
    if (shopInPreProduction)
        connect(&selfTest, &AeatSelfTest::recordSettled, this, &VerifactuCoordinator::recordSettled);
    connect(&selfTest, &AeatSelfTest::progress, &dlg, [&lines](bool ok, const QString &text) { lines.add(ok, text); });
    QTimer::singleShot(0, &selfTest, &AeatSelfTest::run);
    dlg.exec();
}
