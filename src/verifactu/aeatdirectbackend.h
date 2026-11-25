#ifndef AEATDIRECTBACKEND_H
#define AEATDIRECTBACKEND_H

// The direct AEAT client: a VerifactuBackend that builds the VERI*FACTU records
// itself (AeatRecord), keeps their chain and outbox in the shop database (AeatStore)
// and sends them to the AEAT SOAP service over mutual TLS with the owner's
// certificate (AeatCertificate, through WinHTTP: AeatTransport), honouring AEAT's
// wait time between submissions. The 12.0 direct connection; its research is in
// docs/modules/verifactu/aeat-direct-investigation.md.
//
// Behaviour the app relies on:
//  - the chain: one per system and issuer (AEAT, 13-10-2026), so it only ever
//    continues records La Ideal itself generated - never the gateway's - and starts
//    with PrimerRegistro when there are none. At each start it is synced with AEAT
//    (records of our system, this month and the previous; up to two years the first
//    time), so a database restored from an older backup continues where AEAT is (or
//    starts a new chain under a new NumeroInstalacion when a cancellation it lost may
//    be the chain's last record: AEAT's query does not return cancellations. The
//    settings' chain marker tells whether the last record is visible; without it, on
//    a new PC, AEAT's own times do. Unsent records of the old chain go first); no
//    record is generated before that and requests made meanwhile wait. The first sync
//    needs AEAT; later ones fall back to the local chain when AEAT cannot be reached;
//  - submit / cancel store the record at once (even when the certificate or the
//    network fails: only the sending waits) and answer when AEAT replies; a record
//    already stored for that invoice is resent unchanged, an accepted one answers
//    at once with its stored outcome, a rejected one is replaced by a new record
//    flagged Subsanacion / RechazoPrevio;
//  - records are sent together (up to 1000), never before TiempoEsperaEnvio of the
//    previous reply, retried on their own after a failure, and sent at the next
//    start if the app closed first (such late sends say Incidencia = S); outcomes
//    nobody waited for are reported on recordSettled;
//  - a duplicate is checked with a query first: AEAT's record must have our amount
//    and date (else ERROR); one from another system is accepted without claiming our
//    record; a duplicate of an invoice AEAT holds as cancelled is reported and never
//    registered again;
//  - a SOAP fault about the request on a batch is not taken as every record's refusal:
//    the records are resent one by one, so only the faulty one is refused;
//  - the QR is drawn locally (AeatQr), so it never depends on AEAT's reply.

#include <QDateTime>
#include <functional>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QStringList>
#include <QTimer>

#include "aeatcertificate.h"
#include "aeatchaincheck.h"
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
        // Producer and version; installationNumber is ignored: the backend takes the database's.
        AeatRecord::SystemInfo system;
        QString certificateThumbprint;       // from the Windows personal store, or else
        QString certificatePath;             // the owner's .pfx / .p12 file
        QString certificatePassword;
        // The chain's last record as this installation last saw it, kept outside the
        // database (the settings file): a database whose chain differs - restored from an
        // older backup, or moved to a new PC - must reach AEAT before any record, as on a
        // first start. Unset: not kept.
        // When the app started, for a backend that replaces another in the same run
        // (Configuración): the records that one generated were not left by an earlier
        // session. Unset: now.
        QDateTime sessionStart;
        // False for a backend that works beside the shop's own on another chain (the
        // self-test while the shop is in production): it never changes the database's number.
        bool adoptInstallationNumber = true;
        std::function<QString()> readChainMarker;
        std::function<void(const QString &)> writeChainMarker;
    };

    AeatDirectBackend(const Config &config, const QSqlDatabase &db, QObject *parent = nullptr);
    ~AeatDirectBackend() override;

    QString submitInvoiceAsync(const VerifactuInvoice &invoice) override;
    QString cancelInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate) override;
    QString generateQRAsync(const VerifactuInvoice &invoice) override;
    // The QR of the pending registration stored for `invoice` (this issuer, same date and
    // amount), drawn from that record; null when there is none.
    QPixmap queuedRecordQr(const VerifactuInvoice &invoice);
    // The day this environment's client began on this database (AeatStore::markStarted);
    // restartPeriod() moves it to now: the owner entered the environment anew.
    QDate startDate();
    void restartPeriod();
    // A registration record of the invoice is stored for this environment (whatever its
    // outcome) / AEAT's test environment accepted one. Whatever the issuer.
    bool holdsRegistration(const QString &invoiceNumber);
    bool acceptedInTestEnvironment(const QString &invoiceNumber);
    // A record of this kind for the invoice is pending at AEAT (generated, not answered:
    // it goes out on its own). A refused or settled one is not.
    bool holdsPendingRecord(AeatStore::Kind kind, const QString &invoiceNumber);
    // AEAT's stored answer to the invoice's latest record of this kind (accepted or
    // refused); false while none exists or it is pending.
    bool storedOutcome(AeatStore::Kind kind, const QString &invoiceNumber, VerifactuResult *result);
    QString queryInvoiceAsync(const QString &invoiceNumber, const QDate &invoiceDate = QDate()) override;

    // Records can be generated (issuer and database ready). Sending may still fail:
    // see sendingError().
    bool isConfigured() const override { return m_configError.isEmpty(); }
    QString configurationInfo() const override;
    QString configurationError() const { return m_configError; }
    // Why records cannot be sent now (the certificate), or empty.
    QString sendingError() const { return m_sendError; }
    AeatCertificate::Info certificateInfo() const { return m_certificate.info(); }
    // A warning when the certificate expires within `days` of `now` or already has; else empty.
    static QString expiryWarning(const QDateTime &expiry, const QDateTime &now, int days = 30);
    QString certificateExpiryWarning() const
    {
        return m_sendError.isEmpty() ? expiryWarning(m_certificate.info().expiry, QDateTime::currentDateTime()) : QString();
    }
    // Art. 8.1 RD 1007/2023: the stored records are recomputed and their links checked, so an
    // alteration is detected and reported.
    // Strict with the anchors the syncs adopted (a store from before them takes its current
    // continuations as its first anchors, once).
    AeatChainCheck::Result verifyStoredChain();
    QString issuerNif() const { return m_config.issuerNif; }
    // "pruebas" / "produccion", as the store keeps its records.
    QString environmentName() const { return m_config.testEnvironment ? QStringLiteral("pruebas") : QStringLiteral("produccion"); }
    QString issuerName() const { return m_config.issuerName; }
    bool chainReady() const { return m_chainReady; }
    // The last chain sync failed and none has succeeded since: no record can be generated
    // (and, after a restore or on a new PC, the local numbers may already be at AEAT).
    bool chainSyncFailed() const { return !m_chainReady && m_lastSyncFailed; }
    // The first sync (it must reach AEAT) is still running: numbers are not safe yet either.
    bool firstSyncRunning() const { return !m_chainReady && m_syncRunning && m_firstSync; }
    // The issuer's highest ticket number AEAT returned in a sync, from any system (0: none seen).
    int highestNumberAtAeat() const { return m_highestNumberAtAeat; }
    // The same for the rectificativas' own series ("R-<n>"), and the highest this database
    // stored in its environment (a restored copy keeps them).
    int highestRectificationAtAeat() const { return m_highestRectificationAtAeat; }
    int highestStoredRectification() { return m_store.highestRectificationNumber(); }

    // Syncs the chain now (see above). `done(ok, message)`.
    using ChainDone = std::function<void(bool ok, const QString &message)>;
    void syncChain(const ChainDone &done);

    // The registration record of an invoice for the chain, from the app's invoice.
    static AeatRecord::Registration registrationFrom(const VerifactuInvoice &invoice,
                                                     const AeatRecord::PreviousRecord &previous,
                                                     const QDateTime &generatedAt);

    // Answers every request still waiting (before this backend is replaced): a stored
    // record PENDING (the next backend sends it), one never generated NETWORK_ERROR.
    void releaseWaiters();

    // AEAT endpoint of the environment; tests point it at a local fake server.
    static QString endpointUrl(bool testEnvironment);
    static void setEndpointOverride(const QString &url);
    // Wait before an automatic retry after a failed send (60 s; tests shorten it).
    static void setRetryDelaySeconds(int seconds);

    // Whether a technical incident delayed the record: one kept it from being sent (no
    // answer, no certificate), or an earlier session left it unsent (`sessionStart`).
    static bool delayedByIncident(const AeatStore::Record &record, const QDateTime &sessionStart);

    // Art. 16.4 Orden HAC/1177/2024: the records still to send since an incident kept one
    // from being sent (no AEAT answer, certificate unusable, left by an earlier session),
    // until none is left. 0 while there is no incident.
    int unsentRecordCount() const { return m_unsentCount; }

    // The user-facing text of a chain restart nobody was shown yet (see restartChain); it
    // is handed out once.
    QString takeRestartNotice();

    // Seconds the next submission still has to wait (0 = it may go now).
    int secondsUntilNextSend() const;
    qint64 msecsUntilNextSend() const;

signals:
    // Every finished chain sync (AEAT may hold later numbers than the database).
    void chainSynced(bool ok);
    void unsentRecordsChanged(int count);

private:
    QString qrUrlFor(const VerifactuInvoice &invoice) const;
    // "<environment>|<issuer>|<head hash>" of the local chain; written after every change.
    QString chainMarker();
    void saveChainMarker();
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
    QDateTime                  m_sessionStart;   // records generated before it were left by an earlier session
    bool                       m_inFlight = false;
    QList<qint64>              m_inFlightIds;
    QHash<qint64, AeatStore::Record> m_inFlightRecords;
    QHash<qint64, QStringList> m_waiters;      // record id -> request ids to answer
    QStringList                m_heldRequests; // requests waiting for the chain sync (no record yet)
    QList<qint64>              m_sendSingly;   // records of a batch AEAT faulted, resent one by one
    QSet<qint64>               m_verifying;    // duplicates whose AEAT record is being queried
    QHash<qint64, int>         m_unverifiedDuplicates;   // rounds AEAT said "duplicate" but showed nothing
    bool                       m_releasing = false;
    bool                       m_chainReady = false;
    bool                       m_syncRunning = false;
    bool                       m_firstSync = false;      // no sync with AEAT stored yet
    int                        m_highestNumberAtAeat = 0;
    int                        m_highestRectificationAtAeat = 0;
    // Cancelled invoices the running sync was given under another system's block.
    QList<AeatResponse::RecordSummary> m_foreignCancelled;
    bool                       m_lastSyncFailed = false;
    bool                       m_restartDeferred = false;   // a restart waits for the outbox to empty
    QString                    m_restartNotice;
    bool                       m_incident = false;       // a send failed and records are still pending
    int                        m_unsentCount = 0;
    QList<ChainDone>           m_syncWaiters;   // held requests, resumed when the sync ends

    QString nextRequestId();
    QString storeAndQueue(AeatStore::Kind kind, const QString &invoiceNumber,
                          const std::function<AeatStore::Built(const AeatRecord::PreviousRecord &, bool afterRejection)> &build);
    void storeAndQueueNow(const QString &requestId, AeatStore::Kind kind, const QString &invoiceNumber,
                          const std::function<AeatStore::Built(const AeatRecord::PreviousRecord &, bool afterRejection)> &build);
    void answerLater(const QString &requestId, const VerifactuResult &result);
    VerifactuResult resultFromStored(const AeatStore::Record &record) const;
    void scheduleSend();
    // Recounts unsentRecordCount(); `incident`: one has just kept a record from being sent.
    void refreshUnsentCount(bool incident = false);
    void sendPending();
    void onSubmissionReply(const AeatTransport::Response &response);
    // A client fault on a batch: keep its records pending and resend them singly.
    void isolateFaultedBatch(const QList<qint64> &sent, const QString &faultText,
                             const QHash<qint64, bool> &reachedBefore);
    // A duplicate is claimed only once AEAT's record shows our amount and date.
    void verifyDuplicate(const AeatStore::Record &record, const VerifactuResult &result, bool reachedBefore);
    void answerWaiters(qint64 recordId, const VerifactuResult &result);
    void finishRecord(const AeatStore::Record &record, const VerifactuResult &result, bool claimRecord = true);
    QList<AeatResponse::RecordSummary> localCandidates();
    void syncFinished(bool ok, const QString &message);
    bool adoptTip(const QList<AeatResponse::RecordSummary> &candidates, QString *message);
    // Starts the issuer's chain again under a new NumeroInstalacion (a new SIF). Not while
    // records of the old chain are still to send: they go first, then the sync runs again.
    bool restartChain(const QString &cancelledInvoice, QString *message);
    // The chain's last record as this installation's settings last saw it; false when
    // they hold none for this environment and issuer (a new PC).
    bool lastSeenHead(QString *hash) const;
    void resumeDeferredRestart();
    void queryMonth(QDate month, int monthsLeft, int mustQuery, AeatRecord::InvoiceRef pageAfter,
                    QList<AeatResponse::RecordSummary> found, bool sawUnusable);
    void queryInvoiceMonth(const QString &requestId, const QString &invoiceNumber, QDate month, bool tryPrevious,
                           const QDate &invoiceDate);
};

#endif // AEATDIRECTBACKEND_H
