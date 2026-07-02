#ifndef SQL_LITE_H
#define SQL_LITE_H

#include <QDate>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QVector>

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

// RecogPrendas DB-write seams. Each performs one parameterised UPDATE/INSERT on
// `ingresos` keyed by (n_recibo, hash) - the same shape RecogPrendas::updateDb
// used inline. Pure of any widget: the dialog reads the new values off its fields
// and hands them here, so the writes are unit-testable against a temp DB. The
// edit_lock / blocked-quarter / Verifactu-submit business rules stay in the slot.

// PAY_YES / PAY_NO: set fecha_pago + pagado. PAY_NO passes an empty fechaPago.
bool        updateTicketPayment(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                const QString &fechaPago, const QString &pagado);
// PKU_YES / PKU_NO: set fecha_recogida + estado. PKU_NO passes an empty fechaRecogida.
bool        updateTicketPickup(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                               const QString &fechaRecogida, const QString &estado);
// "Recoger todo": mark every garment of a ticket as Recogido in one UPDATE.
// Anulado rows are excluded so a voided garment is never revived to Recogido.
bool        markTicketPickedUp(QSqlDatabase &db, const QString &nRecibo, const QString &fechaRecogida);
// OBSV: set observaciones.
bool        updateTicketObservations(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                     const QString &observaciones);
// SIZE_AND_PRICE: set size + importe.
bool        updateTicketSizeAndPrice(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                     const QString &size, const QString &importe);
// SEPARATE_GARM (1/2) / QTY: set cantidad + importe on the row being reduced.
bool        updateGarmentQtyAndImporte(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                       const QString &cantidad, const QString &importe);
// SERVICE: set servicio + importe (a service change re-prices the row).
bool        updateGarmentServiceAndImporte(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                           const QString &servicio, const QString &importe);

// True when a garment row can be voided locally (VoidGarmentsDialog) instead of
// via an AEAT anulacion: it must be unpaid (pagado != "SI") and never sent to
// AEAT (verifactu_estado PENDIENTE/empty). A paid/ENVIADA row was registered at
// AEAT and must be cancelled through CancelInvoiceDialog, not voided in place.
bool        garmentIsLocallyVoidable(const QString &pagado, const QString &verifactuEstado);
// Void one garment row in place: estado -> "Anulado", verifactu_estado -> "ANULADA".
// pagado is left untouched (stays "NO"); the caller is expected to have gated the
// row through garmentIsLocallyVoidable first.
bool        voidGarmentRow(QSqlDatabase &db, const QString &nRecibo, const QString &hash);
// True if the ticket has at least one paid garment (pagado = 'SI'). A paid ticket
// has been submitted to AEAT, so AddGarment refuses to append new garments to it
// (only unpaid, not-yet-submitted receipts may be altered locally).
bool        ticketHasPaidGarment(QSqlDatabase &db, const QString &nRecibo);

// One `ingresos` garment line to insert. Shared by RecogPrendas SEPARATE_GARM
// (the split-off row) and MainWindow saveTicket (a freshly-saved ticket row).
// `verifactuEstado` is the only verifactu_* column written; the rest start empty:
//  - split-off row: leave it "" so the row reads as legacy/NotSubmitted - the AEAT
//    submission for the ticket covered the full importe and the chained Huella
//    stays on the original rows, so re-submitting a split row would create a
//    duplicate-InvoiceID error at AEAT.
//  - saveTicket row: pass "PENDIENTE" (verifactuEstadoToString(NotSubmitted)); the
//    async AEAT submit patches the row once a reply arrives.
struct IngresoGarmentRow {
    QString nRecibo;
    QString cliente;
    QString fechaRecepcion;
    QString fechaPago;        // empty if unpaid
    QString fechaRecogida;    // empty if not picked up
    QString importe;
    QString pagado;
    QString estado;
    QString cantidad;
    QString prenda;
    QString size;
    QString servicio;
    QString observaciones;
    QString editLock = "0";
    QString hash;
    QString verifactuEstado;  // "" (legacy/split) or "PENDIENTE" (saveTicket)
};
bool        insertGarmentRow(QSqlDatabase &db, const IngresoGarmentRow &row);

// verifactu_estado of the first row of a ticket ("" if the ticket has no rows).
// Read after a payment write so the pay-all loop dedup sees the persisted estado.
QString     ticketVerifactuEstado(QSqlDatabase &db, const QString &nRecibo);

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

// The InvoiceID to print for a ticket: the first non-empty literal AEAT id among
// the ticket's rows, else the bare fallback (n_recibo). Scans rather than reading
// row 0 because row 0 may be an unpaid row of a multi-event ticket with an empty
// invoice_id while a later paid row carries the real "<n>-<seq>" AEAT has on record.
QString     verifactuDisplayInvoiceId(const QStringList &invoiceIds, const QString &fallback);

// One still-PENDIENTE Verifactu submission event, as surfaced by the startup
// recovery dialog. fechaPago is the stored dd-MM-yyyy payment date the invoice
// was/should be submitted under (caller parses to QDate; empty/invalid for an
// unpaid row, which must not be re-submitted); importe is this event's total
// (SUM over its rows).
struct PendingVerifactuEvent {
    QString nRecibo;
    int     seq = 0;
    QString fechaPago;    // dd-MM-yyyy as stored
    QString cliente;
    double  importe = 0.0;
};

// Pending Verifactu events for startup recovery: one entry per
// (n_recibo, verifactu_invoice_seq) whose estado is still PENDIENTE / empty and
// whose fecha_recepcion (rebuilt to ISO) is on or after floorIso. Grouping by
// seq (not just n_recibo) surfaces partial-pay events (seq>0) alongside save-
// time ones (seq=0); SUM(importe) is that event's own total, the amount to
// re-submit under InvoiceID verifactuInvoiceId(n_recibo, seq). fechaPago is the
// original submission date AEAT keys the invoice on - a retry must reuse it (not
// the reception date) or AEAT registers a new invoice and the duplicate guard
// never fires. Ordered newest ticket first, then by seq.
QVector<PendingVerifactuEvent> pendingVerifactuEvents(QSqlDatabase &db, const QString &floorIso);

// Raw accounting totals for one year, bucketed by quarter (index 0 = Q1 .. 3 = Q4).
// Produced by annualAccountingByQuarter with one grouped query per table, the
// IVA base/cuota math is left to the caller (Contabilidad::figuresFromTotals).
// The per-quarter filters mirror totalPriceBetweenDates / countOperationsBetweenDates
// exactly. importe is SUM()'d in SQL, so - unlike the per-row helpers - this path
// does not raise the comma-decimal corruption dialog; the trimestral/mensual
// reports (which still call the per-row helpers) keep that guard.
struct QuarterlyAccountingTotals {
    double ingImporte[4]   = {0, 0, 0, 0};  // paid ingresos total (IVA incl.), ANULADA/RECTIFICADA excluded
    int    ingTickets[4]   = {0, 0, 0, 0};  // distinct paid n_recibo
    double gas10Importe[4] = {0, 0, 0, 0};  // gastos iva = 10
    double gas21Importe[4] = {0, 0, 0, 0};  // gastos iva = 21
    double gasNiImporte[4] = {0, 0, 0, 0};  // gastos iva = 0 (sin IVA)
    int    gasFacturas[4]  = {0, 0, 0, 0};  // gastos rows, every iva rate (matches countOperations)
};

// One grouped query per table over the whole year, bucketed by quarter, for the
// annual Contabilidad report - replaces the 24 per-quarter full-table substr()
// scans (computeFigures x 4 quarters) with 2 scans. ingresos: SUM(importe) +
// COUNT(DISTINCT n_recibo) under the exact pagado='SI' + ANULADA/RECTIFICADA +
// date filters; gastos: SUM(importe) bucketed by quarter x iva (10/21/0) plus a
// per-quarter row count over every rate. Quarter = (month + 2) / 3.
QuarterlyAccountingTotals annualAccountingByQuarter(QSqlDatabase &db, int year);

#endif // SQL_LITE_H
