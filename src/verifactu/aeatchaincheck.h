#ifndef AEATCHAINCHECK_H
#define AEATCHAINCHECK_H

// Verifies the chain of the direct AEAT client's stored records: each record's huella
// is recomputed from its own XML and each record must link to the previous one.
// Pure: no network, no database.

#include <QSet>
#include <QString>
#include <QStringList>

namespace AeatChainCheck {

struct Result {
    bool    ok = true;
    int     checked = 0;       // records examined, including the failing one
    QString problem;           // Spanish, user-facing; empty when ok
    int     failedIndex = -1;  // index in the input of the first bad record
    int     continuations = 0; // records linking to a huella only AEAT holds (restored copy)
    QStringList continuationHashes;   // those huellas, upper-case
};

// `recordXmls`: the exact XML of every stored record, in generation order
// (AeatStore::recordXmls()); each issuer is checked as its own chain. Refused records
// are links of the chain too. A record whose huella does not match its data, a link to
// another stored record than the previous one, or a second PrimerRegistro fails. A link
// to a huella this database does not hold continues a record only AEAT has (a restored
// copy) and is counted, since it cannot be told from a deleted record here.
//
// With `anchors` (the huellas a chain sync adopted, AeatStore::chainAnchors), a link to a
// huella this database does not hold, or to another stored record than the previous one
// (the head moved back by a sync), is accepted only when it is an anchor: a deleted record
// is then reported too.
Result verify(const QStringList &recordXmls, const QSet<QString> *anchors = nullptr);

// The issuer's newest stored record must be the chain head (`headHash`, the huella the next
// record will link to), unless the head is an anchor a sync adopted: otherwise the newest
// record(s) were deleted. An empty head (chain not started) passes.
Result verifyTail(const QStringList &recordXmls, const QString &issuerNif, const QString &headHash,
                  const QSet<QString> &anchors);

} // namespace AeatChainCheck

#endif // AEATCHAINCHECK_H
