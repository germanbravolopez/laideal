#ifndef AEATCHAINSYNC_H
#define AEATCHAINSYNC_H

// The rules that decide, from AEAT's answer to a query, which record is the tip of the
// issuer's hash chain: records before a restart dropped, records proven ours, the chain,
// a lost cancellation, the installation number to adopt. Pure: plain data in, a plan out;
// the backend applies it (docs/modules/verifactu/README.md "When does the chain continue").

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>

#include "aeatrecord.h"
#include "aeatresponse.h"

namespace AeatChainSync {

// The operation of a record summary, as the backend writes it and the rules read it.
inline const QString kRegistration = QStringLiteral("Alta");
inline const QString kCancellation = QStringLiteral("Anulacion");

struct Input {
    QList<AeatResponse::RecordSummary> candidates;   // AEAT's records plus our local ones
    QString issuerNif;
    AeatRecord::PreviousRecord head;                 // the stored chain head
    QSet<QString> anchors;                           // huellas a past sync adopted
    QDateTime restartedAt;                           // invalid: the chain never restarted
    QString restartNumber;                           // NumeroInstalacion the last restart began
    QString lastSeen;                                // the settings' marker huella; empty: none
    bool mayRestart = false;                         // first sync and adopting is allowed
    bool adoptInstallationNumber = true;
    QString installationNumber;                      // the number in use now
};

struct Plan {
    enum class Kind {
        NoRecords,      // nothing to continue from: the chain starts with PrimerRegistro
        Restart,        // start the chain again as a new SIF (`lostInvoice` is the cancellation's)
        Continue        // continue after `tip`
    };
    Kind kind = Kind::NoRecords;
    QString lostInvoice;
    AeatResponse::RecordSummary tip;
    QString installationNumberToAdopt;               // empty: keep the current one
    bool headMoves = false;                          // `tip` is not the stored head
};

Plan plan(const Input &input);

// The pieces of plan(), public so each rule is testable on its own.
QList<AeatResponse::RecordSummary> candidatesAfterRestart(QList<AeatResponse::RecordSummary> candidates,
                                                          const QDateTime &restartedAt, const QString &restartNumber);
QSet<QString> provenOurs(const QList<AeatResponse::RecordSummary> &candidates, QSet<QString> ours);
QList<AeatResponse::RecordSummary> chainOf(QList<AeatResponse::RecordSummary> &candidates, const QSet<QString> &ours,
                                           const AeatRecord::PreviousRecord &head, const QString &issuerNif);
const AeatResponse::RecordSummary *lostRecord(const QList<AeatResponse::RecordSummary> &candidates,
                                              const QList<AeatResponse::RecordSummary> &chain,
                                              const AeatResponse::RecordSummary &tip, const QString &seen);
QString chainInstallationNumber(const QList<AeatResponse::RecordSummary> &chain, const AeatResponse::RecordSummary &tip);

// The user's text for a continuing chain; `cancellation`: the tip is a cancellation of ours.
QString continuationMessage(const AeatResponse::RecordSummary &tip, bool cancellation);

} // namespace AeatChainSync

#endif // AEATCHAINSYNC_H
