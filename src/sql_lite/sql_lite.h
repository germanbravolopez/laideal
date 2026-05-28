#ifndef SQL_LITE_H
#define SQL_LITE_H

#include <QDate>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QStringList>

struct VerifactuResult;

// Configured once at startup by main() via setDbPath().
// All sql_lite functions expand DB_PATH to the runtime-configured value.
void    setDbPath(const QString &path);
QString dbPath();
#define DB_PATH dbPath()

void migrateDatabase(QSqlDatabase &db);

// ---------------------------------------------------------------------------
// `ingresos` table column indices
// ---------------------------------------------------------------------------
// Constant name = uppercase of the SQL column name, so a grep for the column
// in the schema matches the constant directly (no English/Spanish translation
// in either direction). Used by every consumer that queries `ingresos`
// through a QSqlQueryModel: imprimir, recog_prendas, listado, mainwindow,
// mysortfilterproxymodel. Order must match `CREATE TABLE ingresos` plus the
// trailing ALTER TABLEs in migrateDatabase() - if a column is added to the
// schema, append a new constant here and do NOT renumber the existing ones.
#define INGRESOS_COL_N_RECIBO                       0
#define INGRESOS_COL_CLIENTE                        1
#define INGRESOS_COL_FECHA_RECEPCION                2
#define INGRESOS_COL_FECHA_PAGO                     3
#define INGRESOS_COL_FECHA_RECOGIDA                 4
#define INGRESOS_COL_IMPORTE                        5
#define INGRESOS_COL_PAGADO                         6
#define INGRESOS_COL_ESTADO                         7
#define INGRESOS_COL_CANTIDAD                       8
#define INGRESOS_COL_PRENDA                         9
#define INGRESOS_COL_SIZE                          10
#define INGRESOS_COL_SERVICIO                      11
#define INGRESOS_COL_OBSERVACIONES                 12
#define INGRESOS_COL_EDIT_LOCK                     13
#define INGRESOS_COL_HASH                          14
#define INGRESOS_COL_VERIFACTU_CSV                 15
#define INGRESOS_COL_VERIFACTU_TIMESTAMP           16
#define INGRESOS_COL_VERIFACTU_ESTADO              17
#define INGRESOS_COL_VERIFACTU_ERROR               18
#define INGRESOS_COL_VERIFACTU_URL_QR              19
#define INGRESOS_COL_VERIFACTU_XML                 20
#define INGRESOS_COL_VERIFACTU_HASH                21
#define INGRESOS_COL_VERIFACTU_RECTIFIES_N_RECIBO  22
#define INGRESOS_COL_VERIFACTU_RECTIFICATION_TYPE  23

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

// Patch an existing ingresos row with the AEAT reply (CSV, timestamp, estado, error,
// QR URL, signed XML, hash). Used by the async submit handlers in MainWindow and
// RecogPrendas once Verifactu finishes. Matches WHERE n_recibo = ticketNum.
void        updateTicketVerifactuFields(QSqlDatabase &db, const QString &ticketNum,
                                        const VerifactuResult &result);

#endif // SQL_LITE_H
