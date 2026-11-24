#ifndef AEATSTORE_H
#define AEATSTORE_H

// What the direct AEAT client keeps in the shop database (tables it creates
// itself): the head of each issuer's record chain (aeat_chain) and every record it
// generated, with the exact XML sent and its outcome (aeat_records) - the outbox a
// resend reuses unchanged. Test and production records are kept apart by
// `environment`, each with its own chain. The records' NumeroInstalacion
// (aeat_installation) is one per database: it travels with the chain to a new PC.
// A connection that is already open stays open (the app's table models read through it).

#include <QSet>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <functional>

#include "aeatrecord.h"

class QSqlQuery;

class AeatStore
{
public:
    enum class Kind { Registration, Cancellation };

    // Outcome of a stored record.
    static const QString kPending;    // generated, AEAT has not confirmed it (not sent, or no reply)
    static const QString kAccepted;   // Correcto / AceptadoConErrores / duplicate of an accepted one
    static const QString kRejected;   // Incorrecto, or the request was refused
    // AEAT already holds another record for this invoice (e.g. the gateway's): ours
    // was refused as a duplicate, the invoice is registered; never sent again.
    static const QString kDuplicate;
    // AEAT holds this invoice as cancelled: never registered again.
    static const QString kCancelledAtAeat;

    struct Record {
        qint64    id = 0;
        Kind      kind = Kind::Registration;
        QString   issuerNif;
        QString   invoiceNumber;
        QDate     issueDate;
        double    total = 0.0;
        QString   hash;
        QString   xml;               // the record exactly as sent
        QDateTime generatedAt;
        QString   state;
        QString   csv;
        QString   errorCode;
        QString   errorDescription;
        int       attempts = 0;
        bool      reached = false;     // an earlier send got to AEAT, or was cut off (AEAT may hold it)
        bool      delayed = false;     // an incident kept it from being sent (no answer, no certificate)
        bool      isValid() const { return id > 0; }
    };

    // What the builder callback of append() returns for the new record.
    struct Built {
        QString   invoiceNumber;
        QDate     issueDate;
        double    total = 0.0;
        QString   hash;
        QString   xml;
        QDateTime generatedAt;
    };

    AeatStore(const QSqlDatabase &db, const QString &environment);

    bool ensureSchema();

    // NumeroInstalacion of this database's records, created and stored on first use.
    // Empty (see lastError) when it cannot be read or stored - never an unstored number.
    QString installationNumber();
    // Replaces it with the number of the chain this database continues (AEAT's).
    bool setInstallationNumber(const QString &number);
    // The NumeroInstalacion a record's XML carries, or empty.
    static QString installationNumberFromXml(const QString &xml);
    // A number never used before ("LAIDEAL-XXXXXXXX").
    static QString newInstallationNumber();

    // The issuer's last record (empty hash: the chain has not started). `ok` is false
    // when the table could not be read - never to be taken as "no chain".
    AeatRecord::PreviousRecord chainHead(const QString &issuerNif, bool *ok = nullptr);
    // Moves the head to `head` (the chain tip found by a sync), whatever it was.
    bool setChainHead(const QString &issuerNif, const AeatRecord::PreviousRecord &head);

    // Whether the chain of this issuer and environment was synced with AEAT once (the
    // first sync, which must reach AEAT). Until then no record may be generated.
    bool chainSynced(const QString &issuerNif);
    bool markChainSynced(const QString &issuerNif);

    // The XML of every record of this environment, for the chain sync.
    QStringList recordXmls();
    // The highest "R-<n>" invoice number stored in this environment (0: none).
    int highestRectificationNumber();
    // A huella the chain continues from although its record is not stored (an adopted head).
    bool addChainAnchor(const QString &issuerNif, const QString &hash);
    // The anchors of this environment; *tracked is false until markAnchorsTracked().
    QSet<QString> chainAnchors(bool *tracked = nullptr);
    bool markAnchorsTracked();

    // The issuer's chain starts again under `installationNumber` (a new SIF, art. 7.b of
    // Orden HAC/1177/2024: the previous record cannot be obtained): in one transaction the
    // restart is recorded (with the number it replaces and the invoice whose lost
    // cancellation caused it), the number becomes the database's and the head is cleared,
    // so the next record says PrimerRegistro.
    bool restartChain(const QString &issuerNif, const QString &installationNumber,
                      const QString &cancelledInvoice = QString());
    // When the issuer's chain last restarted (invalid: never), and under which number.
    // Older records are not its links.
    QDateTime chainRestartedAt(const QString &issuerNif, QString *installationNumber = nullptr);
    // The restarts of this environment's chains, as "<ISSUER NIF>|<INSTALLATION NUMBER>", upper-case.
    QSet<QString> chainRestarts();

    // Builds and stores the next record of the issuer's chain in one transaction:
    // reads the head, calls `build` with it, inserts the record and moves the head to
    // it. Nothing is stored when `build` returns an empty hash or the commit fails.
    Record append(Kind kind, const QString &issuerNif,
                  const std::function<Built(const AeatRecord::PreviousRecord &)> &build);

    // The newest record of an invoice and kind, or an invalid Record.
    // `issuerNif` empty: any issuer (an invoice number is only unique per issuer).
    Record latest(Kind kind, const QString &invoiceNumber, const QString &issuerNif = QString());
    // Records AEAT has not confirmed yet, oldest first; only `issuerNif`'s when given
    // (a submission carries one issuer in its header).
    QList<Record> pending(int limit = 1000, const QString &issuerNif = QString());

    // How many there are (no limit); -1 when the table cannot be read.
    int pendingCount(const QString &issuerNif = QString());

    bool markAttempt(qint64 id);
    // An incident kept the record from being sent: its later send says Incidencia = S.
    bool markDelayed(qint64 id);
    // A send of the record got to AEAT (answered, or sent and the reply lost).
    bool markReached(qint64 id);
    // Around a send: before it, "may reach AEAT" (a send cut off by closing the app or a
    // new backend then counts as reached); after it, cleared if the request never left.
    bool markSending(qint64 id);
    bool markNotSent(qint64 id);
    bool markOutcome(qint64 id, const QString &state, const QString &csv,
                     const QString &errorCode, const QString &errorDescription);

    QString lastError() const { return m_lastError; }

private:
    QSqlDatabase m_db;
    QString      m_environment;
    QString      m_lastError;

    bool open(bool *wasOpen);
    void close(bool wasOpen);
    // Upgrade of a table made by an earlier build (call with the connection open).
    void addColumnIfMissing(const QString &table, const QString &column, const QString &definition);
    Record readRecord(QSqlQuery &q) const;
    // The number for a database without one yet (connection open); false: records unreadable.
    bool seedInstallationNumber(QString *number);
};

#endif // AEATSTORE_H
