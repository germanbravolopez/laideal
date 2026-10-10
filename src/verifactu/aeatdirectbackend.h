#ifndef AEATDIRECTBACKEND_H
#define AEATDIRECTBACKEND_H

// The direct AEAT client: a VerifactuBackend that builds the VERI*FACTU records
// itself (AeatRecord), keeps their chain and outbox in the shop database (AeatStore)
// and sends them to the AEAT SOAP service over mutual TLS with the owner's
// certificate (AeatCertificate, through WinHTTP: AeatTransport), honouring AEAT's wait time between submissions. Research branch:
// see docs/modules/verifactu/aeat-direct-investigation.md.
//
// Behaviour the app relies on:
//  - submit / cancel store the record at once and answer when AEAT replies; a
//    record already stored for that invoice is reused unchanged (a retry after a
//    lost reply sends the same record), an accepted one answers at once with its
//    stored outcome, a rejected one is replaced by a new record flagged
//    Subsanacion / RechazoPrevio;
//  - records are sent together (up to 1000) and never before TiempoEsperaEnvio of
//    the previous reply has passed; records left unsent when the app closed are
//    sent at the next start;
//  - a transport failure or a server-side SOAP fault leaves the record pending
//    (NETWORK_ERROR), a client-side fault or Incorrecto rejects it (ERROR);
//  - the QR is drawn locally (AeatQr), so it never depends on AEAT's reply.

#include <QDateTime>
#include <QHash>
#include <QSqlDatabase>
#include <QStringList>
#include <QTimer>

#include "aeatcertificate.h"
#include "aeatrecord.h"
#include "aeatresponse.h"
#include "aeattransport.h"
#include "aeatstore.h"
#include "verifactubackend.h"


class AeatDirectBackend : public VerifactuBackend
{
    Q_OBJECT

public:
    struct Config {
        bool    testEnvironment = true;      // AEAT pre-production (no tax effect) vs production
        QString issuerNif;
        QString issuerName;
        AeatRecord::SystemInfo system;
        QString certificateThumbprint;       // from the Windows personal store, or else
        QString certificatePath;             // the owner's .pfx / .p12 file
        QString certificatePassword;
    };

    AeatDirectBackend(const Config &config, const QSqlDatabase &db, QObject *parent = nullptr);
    ~AeatDirectBackend() override;

    QString submitInvoiceAsync(const VerifactuInvoice &invoice) override;
    QString cancelInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate) override;
    QString generateQRAsync(const VerifactuInvoice &invoice) override;
    QString queryInvoiceAsync(const QString &invoiceNumber) override;

    bool isConfigured() const override { return m_configError.isEmpty(); }
    QString configurationInfo() const override;
    QString configurationError() const { return m_configError; }

    // Starts the chain after the last record sent by the gateway (only while empty).
    bool seedChain(const AeatRecord::PreviousRecord &lastGatewayRecord);

    // Before the first direct record: finds the issuer's newest record - the newest
    // registration AEAT returns, looking back month by month (up to 24), or a newer
    // cancellation among `localRecordXmls` (the query does not return cancellations;
    // the gateway's stored record XML has them) - and starts the chain after it.
    // `done(ok, message)`; ok with nothing found means the chain starts with
    // PrimerRegistro. Does nothing when the chain already has a head.
    using ChainDone = std::function<void(bool ok, const QString &message)>;
    void continueChainFromAeat(const QStringList &localRecordXmls, const ChainDone &done);

    // The registration record of an invoice for the chain, from the app's invoice.
    static AeatRecord::Registration registrationFrom(const VerifactuInvoice &invoice,
                                                     const AeatRecord::PreviousRecord &previous,
                                                     const QDateTime &generatedAt);

    // AEAT endpoint of the environment; tests point it at a local fake server.
    static QString endpointUrl(bool testEnvironment);
    static void setEndpointOverride(const QString &url);

    // Seconds the next submission still has to wait (0 = it may go now).
    int secondsUntilNextSend() const;
    qint64 msecsUntilNextSend() const;

private:
    Config                     m_config;
    QString                    m_configError;
    AeatStore                  m_store;
    AeatCertificate            m_certificate;
    AeatTransport             *m_transport = nullptr;
    QTimer                     m_sendTimer;
    QDateTime                  m_nextSendAllowed;
    bool                       m_inFlight = false;
    QList<qint64>              m_inFlightIds;
    QHash<qint64, QStringList> m_waiters;      // record id -> request ids to answer
    int                        m_requestCounter = 0;

    QString nextRequestId();
    QString storeAndQueue(AeatStore::Kind kind, const QString &invoiceNumber,
                          const std::function<AeatStore::Built(const AeatRecord::PreviousRecord &, bool afterRejection)> &build);
    void answerLater(const QString &requestId, const VerifactuResult &result);
    VerifactuResult resultFromStored(const AeatStore::Record &record) const;
    void scheduleSend();
    void sendPending();
    void onSubmissionReply(const AeatTransport::Response &response);
    void queryMonth(QDate month, int monthsLeft, AeatRecord::InvoiceRef pageAfter,
                    QList<AeatResponse::RecordSummary> found, const ChainDone &done);
    void finishRecord(const AeatStore::Record &record, const VerifactuResult &result);
};

#endif // AEATDIRECTBACKEND_H
