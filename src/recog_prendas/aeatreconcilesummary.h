#ifndef AEATRECONCILESUMMARY_H
#define AEATRECONCILESUMMARY_H

#include <QString>

#include "verifactutypes.h"

// The text above the table of the "Consultar en AEAT" dialog: what AEAT answered, as HTML.
// `matches`: the record is provably the local invoice. `predatesProduction`: the invoice is
// older than the production start (`productionStart`, shown), so the real AEAT cannot have it.
QString aeatReconcileSummaryHtml(const VerifactuRemoteRecord &rec, bool matches,
                                 const QString &invoiceId, const QString &paymentDate,
                                 bool predatesProduction, const QString &productionStart);

#endif // AEATRECONCILESUMMARY_H
