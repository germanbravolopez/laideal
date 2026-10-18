#ifndef SQL_LITE_H
#define SQL_LITE_H

#include <QDate>
#include <QDateTime>
#include <QSet>
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
// garments), clamped to >= 0, rounded to cents. Quantity and size are parsed with
// comma->dot normalisation (Spanish input), matching how both are stored at save
// time - so a size like "2,6" is not silently read as 0 (the 9.0 comma-decimal bug).
double      garmentImporte(const QString &quantityText, const QString &sizeText, double unitPrice);
// A garment priced by its size (name contains "m2") whose size is not entered yet
// (empty, 0 or unreadable): it is priced 0,00 and cannot be charged until measured.
bool        garmentUnmeasured(const QString &garment, const QString &sizeText);
// Money is stored and sent to AEAT in cents, rounded half away from zero (28.405 ->
// 28.41): the tolerance absorbs binary error, so 28.405 (28.40499...) still rounds up.
double      roundToCents(double value);
// An importe as stored in `ingresos`: "28.41". Text input may use a decimal comma.
QString     moneyText(double value);
QString     moneyText(const QString &value);
// What the windows and printed tickets show: a decimal comma whatever the language
// ("28,41"), no thousands separator. Storage, the maths and AEAT keep the dot.
QString     displayMoney(double value);
QString     displayMoney(const QString &stored);
// A stored decimal such as a size ("2.5") shown with the comma ("2,5"); other text unchanged.
QString     displayDecimal(const QString &stored);
QString     selectFromWhereLike(QSqlDatabase &db, const QString &itemToGet, const QString &table,
                                const QString &columnToSearch, const QString &itemToSearch,
                                bool exactMatch, bool printMsg);
QString     searchItemFromClient(QSqlDatabase &db, const QString &item, const QString &client, bool printMsg);
// {tel_fijo, movil} for an exact client name, fetched in one query (both empty
// if the client is not found). Avoids two separate single-column lookups.
QStringList readClientPhones(QSqlDatabase &db, const QString &client);
// The stored AEAT record XML of one payment event (its paid rows), or empty.
QString verifactuEventRecordXml(QSqlDatabase &db, const QString &nRecibo, int seq);
// Records the direct AEAT client generated in `environment` ("pruebas" / "produccion")
// and has not had confirmed (table aeat_records, which only exists once the direct
// connection was used): 0 without it. The self-test's pre-production records never
// count for production.
int aeatPendingRecordCount(QSqlDatabase &db, const QString &environment);
// An AEAT outcome nobody waited for (direct client, recordSettled): applied to the
// paid rows of the payment event `invoiceId` names ("31277" or "31277-1") through
// updateTicketVerifactuFields, which never rewrites a settled row. Returns the rows
// updated, -1 when the id names no event. An accepted substitution rectificativa also
// marks its original RECTIFICADA.
int applySettledVerifactuResult(QSqlDatabase &db, const QString &invoiceId, const VerifactuResult &result);
// An accepted cancellation nobody waited for (e.g. the dialog had given up):
// markInvoiceSeqCancelled with `cancelDate`. 1 marked, 2 already marked (its dialog
// did), 0 not marked (refused or the update failed), -1 when the id names no event
// (the self-test's PRUEBA-...).
int applySettledVerifactuCancellation(QSqlDatabase &db, const QString &invoiceId, const QDate &cancelDate,
                                       const VerifactuResult &result);
// The ticket a rectificativa row corrects (verifactu_rectifies_n_recibo), or empty.
QString rectifiedTicketOf(QSqlDatabase &db, const QString &nRecibo);
// The number of a rectificativa of `original` AEAT refused (paid row in ERROR), or empty:
// redone, it is resent corrected under that same number (AEAT, 13-10-2026).
QString refusedRectificationOf(QSqlDatabase &db, const QString &original);
// The number of a rectificativa of `original` still waiting for AEAT (PENDIENTE), or
// empty: until it settles, the ticket is neither rectified nor cancelled again.
QString pendingRectificationOf(QSqlDatabase &db, const QString &original);
// Every rectificativa row still PENDIENTE (n_recibo).
QStringList pendingRectificationNumbers(QSqlDatabase &db);
// A PENDIENTE rectificativa nothing will send (no pending record): an ERROR to redo.
bool markRectificationUnsent(QSqlDatabase &db, const QString &number, const QString &reason);
// A refused rectificativa resent as it was: PENDIENTE again, nothing else changed.
bool markRectificationResent(QSqlDatabase &db, const QString &number);
// The next ticket number: above the database's highest and above `highestAtAeat` (our
// system's highest number at AEAT: a database restored from an older backup).
int nextTicketNumber(QSqlDatabase &db, int highestAtAeat);
// Invoices (payment events) AEAT refused that are still to be resent: paid rows in ERROR,
// paid on or after `floorIso` (yyyy-MM-dd, the recovery floor; empty = all). AEAT refuses
// any invoice dated in an earlier year, so a pre-Verifactu ticket sent by mistake can
// never be resent and must not keep the warning up.
int refusedInvoiceCount(QSqlDatabase &db, const QString &floorIso = QString());
// The same invoices, one per payment event, newest payment first: what the operator
// needs to find and resend each one.
struct RefusedInvoice {
    QString nRecibo;
    int     seq = 0;
    QString fechaPago;        // dd-MM-yyyy, the earliest of the event
    double  importe = 0.0;
    QString error;            // AEAT's reason
    bool    rectificativa = false;   // resent from Rectificar factura, not Recogida
};
QVector<RefusedInvoice> refusedInvoices(QSqlDatabase &db, const QString &floorIso = QString());
bool        updateItemToClient(QSqlDatabase &db, const QString &column, const QString &item, const QString &client);
bool        addNewClient(QSqlDatabase &db, const QString &client, const QString &telFijo,
                         const QString &direccion, const QString &movil);
bool        insertNewItemToTable(QSqlDatabase &db, const QStringList &items, const QString &table);
QString     genHash16();

// RecogPrendas DB-write seams. Each performs one parameterised UPDATE/INSERT on
// `ingresos` keyed by (n_recibo, hash) - the same shape RecogPrendas::updateDb
// used inline. Pure of any widget: the dialog reads the new values off its fields
// and hands them here, so the writes are unit-testable against a temp DB. The
// closed-quarter / Verifactu-submit business rules stay in the slot.

// PKU_YES / PKU_NO: set fecha_recogida + estado. PKU_NO passes an empty fechaRecogida.
bool        updateTicketPickup(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                               const QString &fechaRecogida, const QString &estado);
// "Recoger todo": mark every garment of a ticket as Recogido in one UPDATE.
// Anulado rows are excluded so a voided garment is never revived to Recogido.
bool        markTicketPickedUp(QSqlDatabase &db, const QString &nRecibo, const QString &fechaRecogida);
// OBSV: set observaciones.
bool        updateTicketObservations(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                     const QString &observaciones);
// The amount writers below (size+price, quantity, service) only touch a row whose
// amount is still open: unpaid, never sent to AEAT and not locked by Contabilidad. A paid row belongs to an
// invoice AEAT registered (or a legacy payment) and is refused (false, logged);
// its amounts change only through Anular factura / Rectificar factura. Each stores
// the importe through moneyText().
// SIZE_AND_PRICE: set size + importe.
bool        updateTicketSizeAndPrice(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                     const QString &size, const QString &importe);
// SEPARATE_GARM: move nGarm of the row's garments (1 <= nGarm < cantidad) to a new
// row that copies every other column, verifactu_* included, so the split-off
// garments stay in the same invoice and estado. The split-off part is the
// proportional importe in cents; the original keeps the exact remainder, so the
// sum never changes. A row locked by Contabilidad is
// refused. Returns the new row's hash, or an empty string when nothing was written.
QString     splitGarmentRow(QSqlDatabase &db, const QString &nRecibo, const QString &hash, int nGarm);
// QTY: set cantidad + importe.
bool        updateGarmentQtyAndImporte(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                       const QString &cantidad, const QString &importe);
// SERVICE: set servicio + importe (a service change re-prices the row).
bool        updateGarmentServiceAndImporte(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                           const QString &servicio, const QString &importe);

// True when a garment row can be voided locally (VoidGarmentsDialog) instead of
// via an AEAT anulacion: it must be unpaid (pagado != "SI") and never sent to
// AEAT (verifactu_estado SIN COBRAR/PENDIENTE/empty). A paid/ENVIADA row was registered at
// AEAT and must be cancelled through CancelInvoiceDialog, not voided in place.
bool        garmentIsLocallyVoidable(const QString &pagado, const QString &verifactuEstado);
// True when a garment row is left out of every total: verifactu_estado ANULADA
// (voided in place or cancelled at AEAT) or RECTIFICADA (superseded). Built from
// the same excluded-estado list as the Contabilidad income predicate.
bool        garmentExcludedFromTotals(const QString &verifactuEstado);
// Void one garment row in place: estado -> "Anulado", verifactu_estado -> "ANULADA",
// fecha_anulacion -> today, fecha_pago and fecha_recogida emptied (never paid nor
// collected). pagado is left untouched (stays "NO"); the caller is expected to have
// gated the row through garmentIsLocallyVoidable first.
bool        voidGarmentRow(QSqlDatabase &db, const QString &nRecibo, const QString &hash);
// True if the ticket has at least one paid garment (pagado = 'SI'). A paid ticket
// has been submitted to AEAT, so AddGarment refuses to append new garments to it
// (only unpaid, not-yet-submitted receipts may be altered locally).
bool        ticketHasPaidGarment(QSqlDatabase &db, const QString &nRecibo);
// True when EVERY garment of the ticket is pagado='SI' (and there is at least one).
// Drives the "IMPORTE PAGADO" marker on a reprinted recibo, which shows the whole
// ticket total - "any paid" would claim a partially-paid ticket was settled in full.
bool        ticketAllGarmentsPaid(QSqlDatabase &db, const QString &nRecibo);

// One `ingresos` garment line to insert (MainWindow saveTicket, AddGarment).
// `verifactuEstado` is the only verifactu_* column written; the rest start empty:
// "PENDIENTE" (NotSubmitted) when the row is paid and an AEAT submit is due - the
// async reply patches it - else "SIN COBRAR" (Unpaid), no invoice to send yet.
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
    QString hash;
    QString verifactuEstado;  // "SIN COBRAR" (unpaid) or "PENDIENTE" (paid)
};
bool        insertGarmentRow(QSqlDatabase &db, const IngresoGarmentRow &row);
// Inserts the rows in one transaction: all of them, or none when one fails.
bool        insertGarmentRows(QSqlDatabase &db, const QList<IngresoGarmentRow> &rows);

// Strip diacritics / non-Latin1 marks for accent-insensitive name matching:
// NFD-normalise, narrow to Latin-1 (combining marks become '?'), drop every '?'.
// Pure; used by MainWindow client-name matching. Does not change case.
QString     removeSpecialChars(const QString &str);

// Patch the rows of (n_recibo, verifactu_invoice_seq) with the AEAT reply
// (CSV, timestamp, estado, error, QR URL, signed XML, hash, invoice_id).
// seq=0 binds invoice_id=ticketNum (save-time submit format); seq>0 binds
// invoice_id="<ticketNum>-<seq>" (PayDialog format). The seq filter prevents
// a retry of the save-time submit from clobbering later PayDialog rows. A row
// already ENVIADA / ANULADA / RECTIFICADA is never rewritten. Returns the number
// of rows changed (0: the invoice was already settled), -1 on a DB failure.
int         updateTicketVerifactuFields(QSqlDatabase &db, const QString &ticketNum,
                                        const VerifactuResult &result, int seq = 0);

// Next free verifactu_invoice_seq for a ticket. Counts paid rows so a local-
// only PayDialog event (Verifactu disabled, no estado written) still
// increments the seq for the next event.
// `aeatEnvironment`: only the direct client's records of that environment count (empty: all).
int         nextVerifactuInvoiceSeq(QSqlDatabase &db, const QString &ticketNum, const QString &aeatEnvironment = QString());

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

// Adopt the CSV/QR that AEAT already holds for a payment event, turning a local
// ERROR / PENDIENTE row into ENVIADA. Returns the number of rows updated (0 when
// nothing was eligible). Refuses an empty csv, and never touches a row that is
// already ENVIADA, ANULADA or RECTIFICADA - re-stamping those would revive a
// deliberately superseded invoice. The CALLER must first have confirmed identity
// with verifactuRemoteMatches(): this function trusts the csv it is handed.
int reconcileVerifactuFromAeat(QSqlDatabase &db, const QString &nRecibo, int seq,
                               const QString &csv, const QString &validationUrl);

// One payment event's own submission data, for re-submitting it to AEAT. Scoped
// to the PAID rows of (nRecibo, seq) so a retry carries that event's total and
// its original fecha_pago, not the whole ticket. Empty nRecibo when no such
// paid event exists.
PendingVerifactuEvent verifactuEventFor(QSqlDatabase &db, const QString &nRecibo, int seq);

// One invoice of a ticket that was submitted to AEAT (Anular factura): the paid
// rows of one verifactu_invoice_seq with a verifactu_estado. Unpaid garments share
// seq 0 but were never part of the invoice.
struct SubmittedInvoiceEvent {
    int     seq = 0;
    QString invoiceId;    // literal AEAT InvoiceID, rebuilt for legacy rows without one
    double  importe = 0.0;
    QString csv;
    QString estado;
    QString fechaPago;    // dd-MM-yyyy, earliest over all paid rows of the seq (as verifactuEventFor)
};
// Ordered by seq. Returns false when the query fails.
bool submittedInvoiceEvents(QSqlDatabase &db, const QString &nRecibo,
                            QVector<SubmittedInvoiceEvent> &events);

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

// One invoice submitted to AEAT, as exported for Hacienda (Exportar registros AEAT).
struct AeatExportRecord {
    QString nRecibo;
    int     seq = 0;
    QString invoiceId;    // literal AEAT InvoiceID ("<n>" or "<n>-<seq>")
    QString fechaPago;    // dd-MM-yyyy, the date the invoice was issued under
    double  importe = 0.0; // the event's total, all its garments
    QString csv;
    QString estado;       // current local estado (ENVIADA / ANULADA / RECTIFICADA...)
    QString fechaAnulacion;    // dd-MM-yyyy when cancelled / substituted, else empty
    QString rectifiesNRecibo;  // a rectificativa: the ticket it corrects
    QString rectificationType; // a rectificativa: "S" (substitution) or "I" (differences)
    QString xml;          // the stored AEAT payload; empty when only the CSV is known
    QString cancelXml;    // AEAT's cancellation record, when cancelled and stored
};

// One record per paid payment event (n_recibo, verifactu_invoice_seq) that AEAT holds -
// a stored payload or a CSV (e.g. recovered with "Consultar en AEAT") - issued
// (fecha_pago) or cancelled (fecha_anulacion) between from and to inclusive, any
// estado. Ordered by issue date, then number. Returns false when the query fails, so
// a failure is never mistaken for "no records". `undatedEvents`, when given, receives
// the number of such events with no readable fecha_pago, which no period includes.
bool aeatExportRecords(QSqlDatabase &db, const QDate &from, const QDate &to,
                       QVector<AeatExportRecord> &records, int *undatedEvents = nullptr);

// One paid ticket behind the Contabilidad ingresos summary: its garment rows in
// the period aggregated by n_recibo (importe is IVA included).
struct IncomeTicketDetail {
    QString nRecibo, cliente, fechaPago;
    double  importe  = 0.0;
    int     garments = 0;
    int     invalidAmounts = 0;   // garment rows whose importe uses a comma decimal: listed, not summed
};
// One gastos row behind the Contabilidad gastos summary. iva is the stored rate
// (0 = sin IVA), or -1 when the column is NULL.
struct ExpenseDetail {
    QString nFactura, empresa, servicio, fecha;
    int     iva     = 0;
    double  importe = 0.0;
    bool    invalidAmount = false;  // importe uses a comma decimal: listed, not summed (importe = 0)
};

// A paid ticket later cancelled at AEAT (ANULADA) or superseded by a substitution
// rectificativa (RECTIFICADA), with its fecha_anulacion. Its payment period keeps
// it as income (a filed report never changes); the period containing
// fechaAnulacion subtracts it - in the same period the two net out. importe is
// IVA included.
struct RegularizationDetail {
    QString nRecibo, cliente, fechaPago, fechaAnulacion, verifactuEstado;
    double  importe  = 0.0;
    int     garments = 0;
    int     invalidAmounts = 0;
};

// Detail listings for the Contabilidad report over [startDate, endDate). They are
// the single source of every Contabilidad figure: the report sums these same rows,
// so a summary and its detail table cannot disagree. Income: one entry per paid
// n_recibo (kIngresosIncomeWhere: pagado = 'SI', not ANULADA / RECTIFICADA);
// expenses: every gastos row. Ordered by date, then ticket number / id. A
// comma-decimal importe is never summed: it is counted in invalidAmounts /
// invalidAmount so the report can flag it. A paid row cancelled / rectified with a
// fecha_anulacion still counts as income in its payment period, and is listed by
// regularizationsBetweenDates in the period of its fecha_anulacion. (Pre-10.12
// cancellations have no date and simply stay excluded.)
QVector<IncomeTicketDetail> incomeTicketsBetweenDates(QSqlDatabase &db, QDate startDate, QDate endDate);
QVector<ExpenseDetail>      expensesBetweenDates(QSqlDatabase &db, QDate startDate, QDate endDate);
// Regularisations whose fecha_anulacion falls in [startDate, endDate), one entry per
// n_recibo, ordered by that date.
QVector<RegularizationDetail> regularizationsBetweenDates(QSqlDatabase &db, QDate startDate, QDate endDate);

// Contabilidad's quarterly close, kept in accounting_closures (created by migrateDatabase):
// a quarter is closed while it has a closure not yet reopened. Closures are never
// deleted, so the table is also the quarter's close / reopen history. A row is locked
// when its date (ingresos fecha_pago, gastos fecha) is in a closed quarter.
struct QuarterClosure {
    QDateTime closedAt;
    QDateTime reopenedAt;     // invalid while the closure is open
    bool migrated = false;    // carried over from the old edit_lock flags: closedAt is the migration time
    QString reportFile;
};
bool quarterIsClosed(QSqlDatabase &db, int quarter, int year);
// The quarter containing date (a month without rows inside a closed quarter is closed);
// false for an invalid date.
bool quarterIsClosed(QSqlDatabase &db, QDate date);
// Closes the quarter with the report written for it; false if already closed or on error.
bool closeQuarter(QSqlDatabase &db, int quarter, int year, const QString &reportFile);
// Reopens the quarter's open closure (Revertir contabilidad); false if it was not closed.
bool reopenQuarter(QSqlDatabase &db, int quarter, int year);
// The quarter's closures, oldest first.
QVector<QuarterClosure> quarterClosures(QSqlDatabase &db, int quarter, int year);
// quarterKey() of every closed quarter, for views that check many rows at once.
QSet<int> closedQuarterKeys(QSqlDatabase &db);
inline int quarterKey(int quarter, int year) { return year * 10 + quarter; }
inline int quarterKey(QDate date) { return date.isValid() ? quarterKey((date.month() + 2) / 3, date.year()) : 0; }
// Any payment, cancellation (fecha_anulacion) or gasto dated in the quarter: something to report.
bool quarterHasMovements(QSqlDatabase &db, int quarter, int year);
// `preferred` when its quarter is open, else today, else the first day of the next
// open quarter: where a late change (a subtraction) may still be counted.
QDate openPeriodDate(QSqlDatabase &db, QDate preferred);

// Latest fecha_pago among the paid rows of a ticket; invalid QDate when none.
// A rectificativa may not be dated before it.
QDate ticketLastPaymentDate(QSqlDatabase &db, const QString &nRecibo);

// Marks one payment event (n_recibo + seq) ANULADA after an accepted AEAT
// cancellation and records fecha_anulacion = cancelDate (never overwriting one
// already set): the payment period keeps the income and the cancellation is
// accounted in the period it happens. Only paid rows - the ones the invoice
// covered - are marked; unpaid garments of the ticket (which share seq 0) stay
// chargeable and are invoiced on their own when paid. Returns false when the
// UPDATE fails or matches no paid row.
bool markInvoiceSeqCancelled(QSqlDatabase &db, const QString &nRecibo, int seq, QDate cancelDate,
                                    const QString &cancelXml = QString());
// Marks the paid rows of a ticket RECTIFICADA after an accepted substitution
// rectificativa and records fecha_anulacion = the rectificativa's date, the same
// period its replacement row is counted in. Unpaid rows stay chargeable.
bool markTicketRectified(QSqlDatabase &db, const QString &nRecibo, QDate rectificationDate);

// The same detail listings for a whole year, bucketed by quarter (index 0 = Q1),
// from one scan per table instead of one per quarter. Each bucket equals the
// period listing for that quarter's range.
struct QuarterlyDetails {
    QVector<IncomeTicketDetail>   income[4];
    QVector<ExpenseDetail>        expenses[4];
    QVector<RegularizationDetail> regularizations[4];   // bucketed by fecha_anulacion
};
QuarterlyDetails annualDetailsByQuarter(QSqlDatabase &db, int year);

#endif // SQL_LITE_H
