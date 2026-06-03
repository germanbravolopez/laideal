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
// Importe for one garment line, shared by MainWindow/AddGarment setGarmentPrice.
// Pure (no DB): quantity * unitPrice, multiplied by a non-zero size factor (m2
// garments), clamped to >= 0. Quantity and size are parsed with comma->dot
// normalisation (Spanish input), matching how both are stored at save time - so
// a size like "2,6" is not silently read as 0 (the 9.0 comma-decimal bug).
double      garmentImporte(const QString &quantityText, const QString &sizeText, double unitPrice);
QString     selectFromWhereLike(QSqlDatabase &db, const QString &itemToGet, const QString &table,
                                const QString &columnToSearch, const QString &itemToSearch,
                                bool exactMatch, bool printMsg);
QString     searchItemFromClient(QSqlDatabase &db, const QString &item, const QString &client, bool printMsg);
// {tel_fijo, movil} for an exact client name, fetched in one query (both empty
// if the client is not found). Avoids two separate single-column lookups.
QStringList readClientPhones(QSqlDatabase &db, const QString &client);
bool        updateItemToClient(QSqlDatabase &db, const QString &column, const QString &item, const QString &client);
bool        addNewClient(QSqlDatabase &db, const QString &client, const QString &telFijo,
                         const QString &direccion, const QString &movil);
float       totalPriceBetweenDates(QSqlDatabase &db, const QString &table, QDate startDate, QDate endDate, int iva);
// Number of operations in [startDate, endDate): distinct paid tickets (n_recibo) for
// "ingresos", invoice rows for "gastos". Same estado/date filters as totalPriceBetweenDates.
int         countOperationsBetweenDates(QSqlDatabase &db, const QString &table, QDate startDate, QDate endDate);
int         readLockForMonthAndYear(QSqlDatabase &db, const QString &table, int month, int year);
// Quarter-wide edit_lock state (quarter 1-4). Returns 1 if any row of the quarter
// is accounting-locked, 0 if the quarter has data but is open, 2 if it has no rows.
// Reads all three months of the quarter, so a quarter is not mistaken for empty
// when only its last month lacks data (which readLockForMonthAndYear, reading a
// single month, would do).
int         readLockForQuarter(QSqlDatabase &db, const QString &table, int quarter, int year);
// Set edit_lock = value on both ingresos and gastos rows whose date falls in the
// given month/year (1 = locked after doing the contabilidad, 0 = reverted/unlocked).
void        updateLockForMonth(QSqlDatabase &db, int value, int month, int year);
int         updateComasInDecimalData(QSqlDatabase &db, const QString &table, const QString &item);
void        insertNewItemToTable(QSqlDatabase &db, const QStringList &items, const QString &table);
QString     genHash16();

// Strip diacritics / non-Latin1 marks for accent-insensitive name matching:
// NFD-normalise, narrow to Latin-1 (combining marks become '?'), drop every '?'.
// Pure; used by MainWindow client-name matching. Does not change case.
QString     removeSpecialChars(const QString &str);

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

// AEAT InvoiceID for a ticket payment event: bare n_recibo for seq 0 (save-time
// / first event), "<n_recibo>-<seq>" for a later partial-pay event. Single source
// of truth for the format used at submit / persist / cancel / reprint, so they
// can never disagree (a seq-0 event submitted as "<n>-0" but stored as "<n>").
QString     verifactuInvoiceId(const QString &nRecibo, int seq);

#endif // SQL_LITE_H
