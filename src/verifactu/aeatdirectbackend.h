#ifndef AEATDIRECTBACKEND_H
#define AEATDIRECTBACKEND_H

// The direct AEAT client: a VerifactuBackend that builds the VERI*FACTU records
// itself (AeatRecord), keeps their chain and outbox in the shop database (AeatStore)
// and sends them to the AEAT SOAP service over mutual TLS with the owner's
// certificate (AeatCertificate, through WinHTTP: AeatTransport), honouring AEAT's
// wait time between submissions. Research branch: see
// docs/modules/verifactu/aeat-direct-investigation.md.
//
// Behaviour the app relies on:
//  - the chain: one per system and issuer (AEAT, 13-10-2026), so it only ever
//    continues records La Ideal itself generated - never the gateway's - and starts
//    with PrimerRegistro when there are none. At each start it is synced with AEAT
//    (records of our system, this month and the previous; up to two years the first
//    time), so a database restored from an older backup continues where AEAT is; no
//    record is generated before that and requests made meanwhile wait. The first sync
//    needs AEAT; later ones fall back to the local chain when AEAT cannot be reached;
//  - submit / cancel store the record at once (even when the certificate or the
//    network fails: only the sending waits) and answer when AEAT replies; a record
//    already stored for that invoice is resent unchanged, an accepted one answers
//    at once with its stored outcome, a rejected one is replaced by a new record
//    flagged Subsanacion / RechazoPrevio;
//  - records are sent together (up to 1000), never before TiempoEsperaEnvio of the
//    previous reply, retried on their own after a failure, and sent at the next
//    start if the app closed first; outcomes nobody waited for are reported on
//    recordSettled;
//  - a duplicate of a record AEAT already holds from another system is accepted
//    without claiming our record; a duplicate of an invoice AEAT holds as
//    cancelled is reported and never registered again;
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
    // The QR of the pending registration stored for `invoice` (this issuer, same date and
    // amount), drawn from that record; null when there is none.
    QPixmap queuedRecordQr(const VerifactuInvoice &invoice);
    QString queryInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate = QDate()) override;

    // Records can be generated (issuer and database ready). Sending may still fail:
    // see sendingError().
    bool isConfigured() const override { return m_configError.isEmpty(); }
    QString configurationInfo() const override;
    QString configurationError() const { return m_configError; }
    // Why records cannot be sent now (the certificate), or empty.
    QString sendingError() const { return m_sendError; }
    AeatCertificate::Info certificateInfo() const { return m_certificate.info(); }
    QString issuerNif() const { return m_config.issuerNif; }
    QString issuerName() const { return m_config.issuerName; }
    bool chainReady() const { return m_chainReady; }
    // The highest ticket number of our system AEAT returned in a sync (0: none seen).
    int highestNumberAtAeat() const { return m_highestNumberAtAeat; }

    // Syncs the chain now (see above). `done(ok, message)`.
    using ChainDone = std::function<void(bool ok, const QString &message)>;
    void syncChain(const ChainDone &done);

    // The registration record of an invoice for the chain, from the app's invoice.
    static AeatRecord::Registration registrationFrom(const VerifactuInvoice &invoice,
                                                     const AeatRecord::PreviousRecord &previous,
                                                     const QDateTime &generatedAt);

    // AEAT endpoint of the environment; tests point it at a local fake server.
    static QString endpointUrl(bool testEnvironment);
    static void setEndpointOverride(const QString &url);
    // Wait before an automatic retry after a failed send (60 s; tests shorten it).
    static void setRetryDelaySeconds(int seconds);

    // Seconds the next submission still has to wait (0 = it may go now).
    int secondsUntilNextSend() const;
    qint64 msecsUntilNextSend() const;

private:
    QString qrUrlFor(const VerifactuInvoice &invoice) const;
    // The invoice's QR, drawn from its own data.
    QPixmap qrFor(const VerifactuInvoice &invoice) const;
    Config                     m_config;
    QString                    m_configError;
    QString                    m_sendError;
    AeatStore                  m_store;
    AeatCertificate            m_certificate;
    AeatTransport             *m_transport = nullptr;
    QTimer                     m_sendTimer;
    QDateTime                  m_nextSendAllowed;
    bool                       m_inFlight = false;
    QList<qint64>              m_inFlightIds;
    QHash<qint64, QStringList> m_waiters;      // record id -> request ids to answer
    int                        m_requestCounter = 0;
    bool                       m_chainReady = false;
    bool                       m_syncRunning = false;
    bool                       m_firstSync = false;      // no sync with AEAT stored yet
    int                        m_highestNumberAtAeat = 0;
    QList<ChainDone>           m_syncWaiters;   // held requests, resumed when the sync ends

    QString nextRequestId();
    QString storeAndQueue(AeatStore::Kind kind, const QString &invoiceNumber,
                          const std::function<AeatStore::Built(const AeatRecord::PreviousRecord &, bool afterRejection)> &build);
    void storeAndQueueNow(const QString &requestId, AeatStore::Kind kind, const QString &invoiceNumber,
                          const std::function<AeatStore::Built(const AeatRecord::PreviousRecord &, bool afterRejection)> &build);
    void answerLater(const QString &requestId, const VerifactuResult &result);
    VerifactuResult resultFromStored(const AeatStore::Record &record) const;
    void scheduleSend();
    void sendPending();
    void onSubmissionReply(const AeatTransport::Response &response);
    void finishRecord(const AeatStore::Record &record, const VerifactuResult &result, bool claimRecord = true);
    QList<AeatResponse::RecordSummary> localCandidates();
    void syncFinished(bool ok, const QString &message);
    bool adoptTip(const QList<AeatResponse::RecordSummary> &candidates, QString *message);
    void queryMonth(QDate month, int monthsLeft, int mustQuery, AeatRecord::InvoiceRef pageAfter,
                    QList<AeatResponse::RecordSummary> found, bool sawUnusable);
    void queryInvoiceMonth(const QString &requestId, const QString &invoiceNumber, QDate month, bool tryPrevious);
};

#endif // AEATDIRECTBACKEND_H
