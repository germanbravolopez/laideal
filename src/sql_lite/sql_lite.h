#ifndef SQL_LITE_H
#define SQL_LITE_H

#include <QDate>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QStringList>

#include "ingresos_schema.h"

struct VerifactuResult;

// Configured once at startup by main() via setDbPath().
// All sql_lite functions expand DB_PATH to the runtime-configured value.
void    setDbPath(const QString &path);
QString dbPath();
#define DB_PATH dbPath()

void migrateDatabase(QSqlDatabase &db);

int         readMaxValueInColumnFromTable(QSqlDatabase &db, const QString &column, const QString &table);
int         readMaxNMinYearInColumnFromTable(QSqlDatabase &db, bool maxNMin, const QString &column, const QString &table);
QStringList readColumnFromTable(QSqlDatabase &db, const QString &column, const QString &table, const QString &orderByColumn);
float       readGarmentPrice(QSqlDatabase &db, const QString &garment, const QString &service);
QString     selectFromWhereLike(QSqlDatabase &db, const QString &itemToGet, const QString &table,
                                const QString &columnToSearch, const QString &itemToSearch,
                                bool exactMatch, bool printMsg);
QString     searchItemFromClient(QSqlDatabase &db, const QString &item, const QString &client, bool printMsg);
bool        updateItemToClient(QSqlDatabase &db, const QString &column, const QString &item, const QString &client);
bool        addNewClient(QSqlDatabase &db, const QString &client, const QString &telFijo,
                         const QString &direccion, const QString &movil);
float       totalPriceBetweenDates(QSqlDatabase &db, const QString &table, QDate startDate, QDate endDate, int iva);
int         readLockForMonthAndYear(QSqlDatabase &db, const QString &table, int month, int year);
void        updateLockInIngresos(QSqlDatabase &db, int value, int month, int year);
int         updateComasInDecimalData(QSqlDatabase &db, const QString &table, const QString &item);
void        insertNewItemToTable(QSqlDatabase &db, const QStringList &items, const QString &table);
QString     genHash16();

// Patch the rows of (n_recibo, verifactu_invoice_seq) with the AEAT reply
// (CSV, timestamp, estado, error, QR URL, signed XML, hash, invoice_id).
// seq=0 binds invoice_id=ticketNum (save-time submit format); seq>0 binds
// invoice_id="<ticketNum>-<seq>" (PayDialog format). The seq filter prevents
// a retry of the save-time submit from clobbering later PayDialog rows.
void        updateTicketVerifactuFields(QSqlDatabase &db, const QString &ticketNum,
                                        const VerifactuResult &result, int seq = 0);

// Next free verifactu_invoice_seq for a ticket. Counts paid rows so a local-
// only PayDialog event (Verifactu disabled, no estado written) still
// increments the seq for the next event.
int         nextVerifactuInvoiceSeq(QSqlDatabase &db, const QString &ticketNum);

#endif // SQL_LITE_H
