#ifndef AEATEXPORT_H
#define AEATEXPORT_H

// XML envelope of "Exportar registros AEAT" (Art. 14.1 RD 1007/2023: legible access
// for Hacienda): one <Registro> per invoice AEAT holds, with its stored payload
// inlined when there is one. The records come from sql_lite::aeatExportRecords.

#include <QDate>
#include <QString>
#include <QVector>

#include "sql_lite.h"

class QIODevice;

// Writes the whole document to `out` (already open for writing). Returns the
// number of <Registro> elements written.
int writeAeatExportXml(QIODevice *out, const QVector<AeatExportRecord> &records,
                       const QDate &from, const QDate &to,
                       const QString &nif, const QString &issuerName);

#endif // AEATEXPORT_H
