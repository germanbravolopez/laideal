#include "aeatchainsync.h"
#include "textformat.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDebug>

namespace AeatChainSync {

QList<AeatResponse::RecordSummary> candidatesAfterRestart(QList<AeatResponse::RecordSummary> candidates,
                                                          const QDateTime &restartedAt, const QString &restartNumber)
{
    // Records from before a restart belong to the chain that ended there.
    if (!restartedAt.isValid())
        return candidates;
    // Whole seconds on both sides; in the restart's own second only a record AEAT
    // shows under the new number belongs to the new chain.
    const qint64 restartSecs = restartedAt.toSecsSinceEpoch();
    int unreadable = 0;
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const AeatResponse::RecordSummary &c) {
        const QDateTime generated = QDateTime::fromString(c.generatedAt, Qt::ISODate);
        if (!generated.isValid()) {
            ++unreadable;      // before or after the restart cannot be told: never a link
            return true;
        }
        const qint64 secs = generated.toSecsSinceEpoch();
        return secs < restartSecs || (secs == restartSecs && c.installationNumber != restartNumber);
    }), candidates.end());
    if (unreadable > 0)
        qWarning() << "AeatDirectBackend:" << unreadable << "record(s) with an unreadable generation time left out of"
                   << "the chain after its restart";
    return candidates;
}

// The huellas proven ours: `ours` (anchors, head, the settings' marker), every
// registration of our system, and a returned cancellation that chains to one of them
// (another system chains to its own).
QSet<QString> provenOurs(const QList<AeatResponse::RecordSummary> &candidates, QSet<QString> ours)
{
    for (const AeatResponse::RecordSummary &c : candidates) {
        if (!c.cancellationFromQuery && !c.foreignSystem)
            ours.insert(c.hash.toUpper());
    }
    ours.remove(QString());
    for (bool grown = true; grown;) {
        grown = false;
        for (const AeatResponse::RecordSummary &c : candidates) {
            const QString hash = c.hash.toUpper();
            if (c.cancellationFromQuery && !ours.contains(hash) && ours.contains(c.previousHash.toUpper())) {
                ours.insert(hash);
                grown = true;
            }
        }
    }
    return ours;
}

QList<AeatResponse::RecordSummary> chainOf(QList<AeatResponse::RecordSummary> &candidates, const QSet<QString> &ours,
                                           const AeatRecord::PreviousRecord &head, const QString &issuerNif)
{
    QList<AeatResponse::RecordSummary> chain;
    for (AeatResponse::RecordSummary &c : candidates) {
        // Unproven, a returned cancellation counts as one the query hides.
        if (c.cancellationFromQuery && !ours.contains(c.hash.toUpper())) {
            c.operation = kRegistration;
            c.cancelledAtAeat = true;
            continue;
        }
        if (c.foreignSystem && !c.cancellationFromQuery)
            continue;                    // another system's registration: never a link of ours
        chain << c;
    }
    QSet<QString> hashes;
    for (const AeatResponse::RecordSummary &c : chain)
        hashes.insert(c.hash.toUpper());
    // The current head takes part too: a newer record chains to it, or it is the tip.
    if (!head.isFirst() && !hashes.contains(head.hash.toUpper())) {
        AeatResponse::RecordSummary h;
        h.valid = true;
        h.issuerNif = issuerNif;
        h.invoiceNumber = head.invoice.invoiceNumber;
        h.issueDate = head.invoice.issueDate.toString(kDbDateFormat);
        h.hash = head.hash;
        h.generatedAt = head.generatedAt;
        chain << h;
    }
    return chain;
}

// The hidden cancellation that may be the chain's last record, or nullptr. `seen` is the
// settings' marker (empty: none, or it names no record, which proves nothing). The marker
// names the tip; not visible, it is the lost record; visible with records of ours after
// it, the settings are older than the database. Then, or without it, AEAT's times decide.
const AeatResponse::RecordSummary *lostRecord(const QList<AeatResponse::RecordSummary> &candidates,
                                              const QList<AeatResponse::RecordSummary> &chain,
                                              const AeatResponse::RecordSummary &tip, const QString &seen)
{
    if (seen.isEmpty())
        return AeatResponse::lostCancellationAfter(candidates, tip);
    bool visible = false;
    for (const AeatResponse::RecordSummary &c : chain)
        visible = visible || c.hash.compare(seen, Qt::CaseInsensitive) == 0;
    if (!visible)
        return AeatResponse::lostCancellation(candidates);
    if (tip.hash.compare(seen, Qt::CaseInsensitive) != 0)
        return AeatResponse::lostCancellationAfter(candidates, tip);
    return nullptr;
}

// The chain's NumeroInstalacion: its tip's or, for a returned cancellation (which may carry
// the block of the invoice it cancels), that of the newest registration at AEAT.
QString chainInstallationNumber(const QList<AeatResponse::RecordSummary> &chain, const AeatResponse::RecordSummary &tip)
{
    if (!tip.cancellationFromQuery)
        return tip.installationNumber;
    QString number;
    QDateTime newest;
    for (const AeatResponse::RecordSummary &c : chain) {
        const QDateTime generated = QDateTime::fromString(c.generatedAt, Qt::ISODate);
        if (!c.cancellationFromQuery && !c.installationNumber.isEmpty() && (!newest.isValid() || generated > newest)) {
            newest = generated;
            number = c.installationNumber;
        }
    }
    return number;
}

QString continuationMessage(const AeatResponse::RecordSummary &tip, bool cancellation)
{
    // The literal context keeps the strings where lupdate finds them and where they were.
    return QCoreApplication::translate("AeatDirectBackend", "La cadena continúa tras el registro %1 (%2, %3).")
        .arg(tip.invoiceNumber,
             cancellation ? QCoreApplication::translate("AeatDirectBackend", "anulación")
                          : QCoreApplication::translate("AeatDirectBackend", "alta"),
             tip.generatedAt.isEmpty() ? QCoreApplication::translate("AeatDirectBackend", "generado antes") : tip.generatedAt);
}

Plan plan(const Input &input)
{
    Plan result;
    QList<AeatResponse::RecordSummary> candidates =
        candidatesAfterRestart(input.candidates, input.restartedAt, input.restartNumber);
    QSet<QString> known = input.anchors;
    known.insert(input.head.hash.toUpper());
    if (!input.lastSeen.isEmpty())
        known.insert(input.lastSeen.toUpper());
    const QList<AeatResponse::RecordSummary> chain =
        chainOf(candidates, provenOurs(candidates, known), input.head, input.issuerNif);
    const AeatResponse::RecordSummary *tip = AeatResponse::chainTip(chain);
    if (!tip) {
        // Nothing to continue from, yet AEAT holds a cancellation that may be ours.
        const AeatResponse::RecordSummary *lost = input.mayRestart ? AeatResponse::lostCancellation(candidates) : nullptr;
        if (lost) {
            result.kind = Plan::Kind::Restart;
            result.lostInvoice = lost->invoiceNumber;
        }
        return result;
    }
    // A restored database lacking a cancellation AEAT hides, which may be the chain's last
    // record, starts a new chain as a new SIF (AEAT, 24-11-2026; art. 7.b of the Orden).
    if (input.mayRestart) {
        if (const AeatResponse::RecordSummary *lost = lostRecord(candidates, chain, *tip, input.lastSeen)) {
            result.kind = Plan::Kind::Restart;
            result.lostInvoice = lost->invoiceNumber;
            return result;
        }
    }
    result.kind = Plan::Kind::Continue;
    result.tip = *tip;
    const QString chainNumber = chainInstallationNumber(chain, *tip);
    if (input.adoptInstallationNumber && !chainNumber.isEmpty() && chainNumber != input.installationNumber)
        result.installationNumberToAdopt = chainNumber;
    result.headMoves = tip->hash.compare(input.head.hash, Qt::CaseInsensitive) != 0;
    return result;
}

} // namespace AeatChainSync
