#ifndef AEATSTORE_H
#define AEATSTORE_H

// What the direct AEAT client keeps in the shop database (two tables it creates
// itself): the head of each issuer's record chain (aeat_chain) and every record it
// generated, with the exact XML sent and its outcome (aeat_records) - the outbox a
// resend reuses unchanged. Test and production records are kept apart by
// `environment`, each with its own chain. A connection that is already open stays
// open (the app's table models read through it).

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

    // The issuer's last record (empty hash: the chain has not started). `ok` is false
    // when the table could not be read - never to be taken as "no chain".
    AeatRecord::PreviousRecord chainHead(const QString &issuerNif, bool *ok = nullptr);
    // Starts the chain at a record generated elsewhere (the last one the gateway sent),
    // only while this chain is still empty. Returns false if it already had a head.
    bool seedChainHead(const QString &issuerNif, const AeatRecord::PreviousRecord &head);
    // Moves the head to `head` (the chain tip found by a sync), whatever it was.
    bool setChainHead(const QString &issuerNif, const AeatRecord::PreviousRecord &head);

    // Whether the chain of this issuer and environment was synced with AEAT once
    // (the hand-over from the gateway). Until then no record may be generated.
    bool chainSynced(const QString &issuerNif);
    bool markChainSynced(const QString &issuerNif);

    // The XML of every record of this environment, for the chain sync.
    QStringList recordXmls();

    // Builds and stores the next record of the issuer's chain in one transaction:
    // reads the head, calls `build` with it, inserts the record and moves the head to
    // it. Nothing is stored when `build` returns an empty hash or the commit fails.
    Record append(Kind kind, const QString &issuerNif,
                  const std::function<Built(const AeatRecord::PreviousRecord &)> &build);

    // The newest record of an invoice and kind, or an invalid Record.
    Record latest(Kind kind, const QString &invoiceNumber);
    // Records AEAT has not confirmed yet, oldest first.
    QList<Record> pending(int limit = 1000);

    bool markAttempt(qint64 id);
    bool markOutcome(qint64 id, const QString &state, const QString &csv,
                     const QString &errorCode, const QString &errorDescription);

    QString lastError() const { return m_lastError; }

private:
    QSqlDatabase m_db;
    QString      m_environment;
    QString      m_lastError;

    bool open(bool *wasOpen);
    void close(bool wasOpen);
    Record readRecord(QSqlQuery &q) const;
};

#endif // AEATSTORE_H
