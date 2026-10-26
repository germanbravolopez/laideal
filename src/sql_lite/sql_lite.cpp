#include "sql_lite.h"
#include "ingresos_schema.h"

#include "verifactutypes.h"

#include <QDateTime>
#include <QHash>
#include <QDebug>
#include <QUuid>
#include <QSqlError>
#include <QSqlQuery>
#include <QRegularExpression>
#include <QSqlRecord>

#include <cmath>

// ---------------------------------------------------------------------------
// DB path - set once in main() before MainWindow is constructed
// ---------------------------------------------------------------------------

static QString s_dbPath;

void setDbPath(const QString &path) { s_dbPath = path; }
QString dbPath() { return s_dbPath; }

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

// A null QString binds as SQL NULL, which the shop's NOT NULL columns refuse (an unpaid
// AddGarment row has no fecha_pago): every text value an INSERT binds goes through here.
static QString notNull(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

// Guard used at the top of every function that opens the database.
// Returns true if the path is missing, so callers can early-return.
static bool dbNotConfigured(const QSqlDatabase &db, const char *caller)
{
    if (!db.databaseName().isEmpty())
        return false;
    qWarning() << caller << "- database path not configured, skipping query";
    return true;
}

// Zero-pad months 1 and 2 so LIKE '%01-2024' does not match month 10 or 11.
static QString monthStr(int month)
{
    return month < 10 ? QStringLiteral("0%1").arg(month) : QString::number(month);
}

// The quarter of a dd-MM-yyyy date column has an open closure in accounting_closures.
// An empty or NULL date is in no quarter, so it is never closed.
static QString closedQuarterSql(const QString &dateColumn)
{
    return "EXISTS (SELECT 1 FROM accounting_closures c WHERE c.reopened_at IS NULL"
           " AND c.year = CAST(substr(" + dateColumn + ", 7, 4) AS INTEGER)"
           " AND c.quarter = (CAST(substr(" + dateColumn + ", 4, 2) AS INTEGER) + 2) / 3)";
}

// Not locked by Contabilidad: the row's payment date is not in a closed quarter.
static const QString kUnlockedWhere = " AND NOT " + closedQuarterSql(QStringLiteral("ingresos.fecha_pago"));

// The row's amount is still open: unpaid, never sent to AEAT, not locked by Contabilidad.
static const QString kAmountEditableWhere =
    QStringLiteral(" AND COALESCE(pagado, '') != 'SI' "
                   " AND COALESCE(verifactu_estado, '') NOT IN ('ENVIADA', 'ERROR', 'ANULADA', 'RECTIFICADA') ")
    + kUnlockedWhere;

// Contabilidad's quarterly close. Append-only: closing inserts a row, reopening sets its
// reopened_at once, and the triggers refuse every other change, so the table is the
// close / reopen history. The partial index allows one open closure per quarter.
// origin is 'contabilidad', or 'migration' for a quarter closed before the table existed
// (its closed_at is the migration time, the real date was never stored).
static const char *const kAccountingClosuresSchema[] = {
    "CREATE TABLE IF NOT EXISTS accounting_closures ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " year INTEGER NOT NULL CHECK (year BETWEEN 2000 AND 2999),"
    " quarter INTEGER NOT NULL CHECK (quarter BETWEEN 1 AND 4),"
    " closed_at TEXT NOT NULL,"
    " reopened_at TEXT,"
    " origin TEXT NOT NULL CHECK (origin IN ('contabilidad', 'migration')),"
    " report_file TEXT NOT NULL DEFAULT '')",
    "CREATE UNIQUE INDEX IF NOT EXISTS accounting_closures_open "
    " ON accounting_closures (year, quarter) WHERE reopened_at IS NULL",
    "CREATE TRIGGER IF NOT EXISTS accounting_closures_no_delete BEFORE DELETE ON accounting_closures "
    "BEGIN SELECT RAISE(ABORT, 'accounting_closures is append-only'); END",
    "CREATE TRIGGER IF NOT EXISTS accounting_closures_reopen_only BEFORE UPDATE ON accounting_closures "
    "WHEN OLD.reopened_at IS NOT NULL OR NEW.reopened_at IS NULL OR NEW.id IS NOT OLD.id"
    " OR NEW.year IS NOT OLD.year OR NEW.quarter IS NOT OLD.quarter OR NEW.closed_at IS NOT OLD.closed_at"
    " OR NEW.origin IS NOT OLD.origin OR NEW.report_file IS NOT OLD.report_file "
    "BEGIN SELECT RAISE(ABORT, 'an accounting closure can only be reopened, once'); END",
};

static QString closureTimestamp()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

// Creates accounting_closures. The first time, in the same transaction, every quarter
// that the old per-row edit_lock flag closed (any ingresos / gastos row of it set to 1)
// gets a 'migration' closure; afterwards edit_lock is never read again, so a quarter
// reopened later is not closed again by its stale flags. On an open db.
// Req. 1 defence in depth, in the database itself: a row registered at AEAT is never
// deleted, and a paid row AEAT accepted (also once cancelled or rectified, still exported)
// keeps its invoice identity; recreated so an older build's ENVIADA-only version is replaced. Only an actual change of
// those columns aborts; estado, fecha_anulacion and the pickup move on as before, and the
// amount stays writable for Separar prendas (the two parts keep the invoice total).
static const char *const kIngresosGuards[] = {
    "DROP TRIGGER IF EXISTS ingresos_accepted_identity",
    "CREATE TRIGGER IF NOT EXISTS ingresos_registered_no_delete BEFORE DELETE ON ingresos "
    "WHEN COALESCE(OLD.verifactu_csv, '') != '' OR OLD.verifactu_estado IN ('ENVIADA', 'ANULADA', 'RECTIFICADA') "
    "BEGIN SELECT RAISE(ABORT, 'ingresos: a row registered at AEAT is never deleted'); END",
    "CREATE TRIGGER IF NOT EXISTS ingresos_accepted_identity BEFORE UPDATE OF n_recibo, fecha_pago, pagado, "
    "verifactu_csv, verifactu_xml, verifactu_hash, verifactu_invoice_id, verifactu_invoice_seq ON ingresos "
    "WHEN (OLD.verifactu_estado = 'ENVIADA' OR (OLD.verifactu_estado IN ('ANULADA', 'RECTIFICADA') AND OLD.pagado = 'SI')) "
    "  AND (NEW.n_recibo IS NOT OLD.n_recibo OR NEW.fecha_pago IS NOT OLD.fecha_pago "
    "  OR NEW.pagado IS NOT OLD.pagado OR NEW.verifactu_csv IS NOT OLD.verifactu_csv "
    "  OR NEW.verifactu_xml IS NOT OLD.verifactu_xml OR NEW.verifactu_hash IS NOT OLD.verifactu_hash "
    "  OR NEW.verifactu_invoice_id IS NOT OLD.verifactu_invoice_id OR NEW.verifactu_invoice_seq IS NOT OLD.verifactu_invoice_seq) "
    "BEGIN SELECT RAISE(ABORT, 'ingresos: a row AEAT accepted keeps its invoice data'); END",
};

static bool migrateAccountingClosures(QSqlDatabase &db)
{
    QSqlQuery q(db);
    const bool exists = q.exec("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'accounting_closures'")
                        && q.next();
    q.finish();
    if (exists) {
        bool ok = true;
        for (const char *sql : kAccountingClosuresSchema)   // index / triggers of an older build
            if (!q.exec(QString::fromLatin1(sql))) {
                ok = false;
                qWarning() << "migrateAccountingClosures: schema statement failed -" << q.lastError().text();
            }
        return ok;
    }
    if (!db.transaction()) {
        qCritical() << "migrateAccountingClosures: could not start a transaction -" << db.lastError().text();
        return false;
    }
    bool ok = true;
    for (const char *sql : kAccountingClosuresSchema)
        ok = ok && q.exec(QString::fromLatin1(sql));
    const auto quarterOf = [](const QString &col) {
        return "SELECT CAST(substr(" + col + ", 7, 4) AS INTEGER) AS y, "
               "(CAST(substr(" + col + ", 4, 2) AS INTEGER) + 2) / 3 AS qt";
    };
    ok = ok && q.prepare("INSERT INTO accounting_closures (year, quarter, closed_at, origin) "
                         "SELECT DISTINCT y, qt, :now, 'migration' FROM ("
                         + quarterOf("fecha_pago") + " FROM ingresos WHERE CAST(edit_lock AS INTEGER) = 1 "
                         "UNION " + quarterOf("fecha") + " FROM gastos WHERE CAST(edit_lock AS INTEGER) = 1) "
                         "WHERE y BETWEEN 2000 AND 2999 AND qt BETWEEN 1 AND 4");
    if (ok) {
        q.bindValue(":now", closureTimestamp());
        ok = q.exec();
    }
    const int migrated = ok ? q.numRowsAffected() : 0;
    if (ok && db.commit()) {
        qDebug() << "migrateAccountingClosures: created accounting_closures," << migrated
                 << "quarter(s) closed by edit_lock carried over";
        return true;
    } else {
        // Without the table every quarter reads as open: say it loudly, retried at the next start.
        qCritical() << "migrateAccountingClosures: migration failed, rolled back -" << q.lastError().text()
                    << db.lastError().text();
        db.rollback();
        return false;
    }
}

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------

// The tables an empty database file needs (a fresh install); the shop's are older and
// already have them, so these only ever create on a new file.
static const char *const kBaseSchema[] = {
    "CREATE TABLE IF NOT EXISTS ingresos (n_recibo TEXT, cliente TEXT, fecha_recepcion TEXT, fecha_pago TEXT, "
    " fecha_recogida TEXT, importe TEXT, pagado TEXT, estado TEXT, cantidad TEXT, prenda TEXT, size TEXT, "
    " servicio TEXT, observaciones TEXT, edit_lock TEXT NOT NULL DEFAULT '0', hash TEXT)",
    "CREATE TABLE IF NOT EXISTS gastos (id INTEGER PRIMARY KEY, n_factura TEXT, servicio TEXT, descripcion TEXT, "
    " empresa TEXT, fecha TEXT, iva TEXT, importe TEXT, edit_lock TEXT)",
    "CREATE TABLE IF NOT EXISTS clientes (nombre TEXT, tel_fijo TEXT, movil TEXT, direccion TEXT)",
    "CREATE TABLE IF NOT EXISTS prendas (nombre TEXT, precio_limpieza TEXT, precio_plancha TEXT)",
    "CREATE TABLE IF NOT EXISTS proveedores (nombre TEXT, cif TEXT, direccion TEXT, telefono TEXT)",
    "CREATE TABLE IF NOT EXISTS servicios (nombre TEXT)",
    // The "review manually" notices already logged: each is logged once, not at every start.
    "CREATE TABLE IF NOT EXISTS migration_notes (note TEXT PRIMARY KEY, first_seen TEXT NOT NULL)",
};

bool migrateDatabase(QSqlDatabase &db, bool rewriteData)
{
    if (dbNotConfigured(db, __func__)) return false;
    qDebug() << "migrateDatabase: ensuring verifactu_* columns exist on ingresos (idempotent ALTER TABLE)";
    ConnectionScope scope(db);
    if (!scope.isOpen()) {
        qCritical() << "migrateDatabase: the database cannot be opened -" << db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    bool baseOk = true;
    for (const char *sql : kBaseSchema)
        baseOk = q.exec(QString::fromLatin1(sql)) && baseOk;
    // True the first time this notice is seen (then logged); false afterwards.
    const auto firstNotice = [&db](const QString &note) {
        QSqlQuery n(db);
        n.prepare("INSERT OR IGNORE INTO migration_notes (note, first_seen) VALUES (:n, :t)");
        n.bindValue(":n", note);
        n.bindValue(":t", QDateTime::currentDateTime().toString(Qt::ISODate));
        return !n.exec() || n.numRowsAffected() == 1;   // a failed insert still logs
    };
    // Each exec() silently fails if the column already exists - safe to call repeatedly.
    q.exec("ALTER TABLE ingresos ADD COLUMN verifactu_csv TEXT");
    q.exec("ALTER TABLE ingresos ADD COLUMN verifactu_timestamp TEXT");
    q.exec("ALTER TABLE ingresos ADD COLUMN verifactu_estado TEXT");
    q.exec("ALTER TABLE ingresos ADD COLUMN verifactu_error TEXT");
    q.exec("ALTER TABLE ingresos ADD COLUMN verifactu_url_qr TEXT");
    q.exec("ALTER TABLE ingresos ADD COLUMN verifactu_xml TEXT");
    q.exec("ALTER TABLE ingresos ADD COLUMN verifactu_hash TEXT");
    // Rectificativa link: on a rectifying row, points back to the original n_recibo.
    // verifactu_rectification_type is "S" (sustitucion) or "I" (diferencias).
    q.exec("ALTER TABLE ingresos ADD COLUMN verifactu_rectifies_n_recibo TEXT");
    q.exec("ALTER TABLE ingresos ADD COLUMN verifactu_rectification_type TEXT");
    // Partial-payment sequence (8.5+). Each payment event for a given n_recibo
    // submits as InvoiceID "<n_recibo>-<seq>" so multiple partial payments do
    // not collide at AEAT. Legacy rows (8.0-8.4) leave it 0 - those tickets
    // were submitted as "<n_recibo>" (no seq), which is its own distinct ID.
    q.exec("ALTER TABLE ingresos ADD COLUMN verifactu_invoice_seq INTEGER DEFAULT 0");
    // Literal AEAT InvoiceID submitted for the row (8.5+). MainWindow save-time
    // submission writes the bare "<n_recibo>"; PayDialog partial-pay writes
    // "<n_recibo>-<seq>"; rectificativa writes its own new "<n_recibo>". Reads
    // authoritatively for reprint / QR regen so we never have to guess from
    // seq=0 whether the original AEAT format was bare or "-0".
    q.exec("ALTER TABLE ingresos ADD COLUMN verifactu_invoice_id TEXT");
    // Date a garment was cancelled (10.12+): voided in place (Anular prendas), cancelled
    // at AEAT, or superseded by a substitution rectificativa. Empty otherwise.
    q.exec("ALTER TABLE ingresos ADD COLUMN fecha_anulacion TEXT");
    // AEAT's cancellation record (Return.Xml of the /Cancel reply), for the Hacienda export.
    q.exec("ALTER TABLE ingresos ADD COLUMN verifactu_cancel_xml TEXT");
    // The ALTERs fail on purpose when the column exists: check the result instead.
    bool ok = baseOk;
    const auto fail = [&ok]() { ok = false; return qWarning(); };   // a step that failed
    if (!baseOk)
        fail() << "migrateDatabase: creating the base tables failed -" << q.lastError().text();
    if (db.record("ingresos").count() <= INGRESOS_COL_VERIFACTU_CANCEL_XML)
        fail() << "migrateDatabase: adding the verifactu_* columns failed";

    // Before kAmountEditableWhere is used below: it reads accounting_closures.
    ok = migrateAccountingClosures(db) && ok;
    for (const char *sql : kIngresosGuards)
        if (!q.exec(QString::fromLatin1(sql)))
            fail() << "migrateDatabase: an ingresos guard trigger failed -" << q.lastError().text();

    // Everything below rewrites rows: never without a copy of the database as it was.
    if (!rewriteData) {
        qWarning() << "migrateDatabase: schema only - the data repairs wait until a backup succeeds";
        q.finish();
        return ok;
    }

    // 10.9 backfill: before the Unpaid/NotSubmitted split, saveTicket stamped every
    // row PENDIENTE regardless of payment, so unpaid garments claimed to be awaiting
    // an AEAT reply. Re-label those as SIN COBRAR. Idempotent (the filter excludes
    // rows it already rewrote) and scoped to the literal 'PENDIENTE' on purpose:
    // NULL/'' rows are legacy split-off garments that several queries detect via
    // `verifactu_estado != ''`, so making them non-empty would change print/cancel
    // behaviour. See docs/modules/verifactu/README.md.
    if (!q.exec("UPDATE ingresos SET verifactu_estado = 'SIN COBRAR' "
                "WHERE verifactu_estado = 'PENDIENTE' "
                "  AND (pagado != 'SI' OR fecha_pago IS NULL OR fecha_pago = '')"))
        fail() << "migrateDatabase: SIN COBRAR backfill failed -" << q.lastError().text();
    else if (q.numRowsAffected() > 0)
        qDebug() << "migrateDatabase: re-labelled" << q.numRowsAffected()
                 << "unpaid PENDIENTE rows as SIN COBRAR";

    // 10.12 backfill: garments voided in place used to carry the void date in
    // fecha_pago and fecha_recogida although they were never paid nor collected.
    // Move it to fecha_anulacion and empty both. Idempotent (only rows without a
    // fecha_anulacion), and scoped to local voids: pagado != 'SI'.
    if (!q.exec("UPDATE ingresos SET "
                "fecha_anulacion = COALESCE(NULLIF(fecha_pago, ''), NULLIF(fecha_recogida, '')), "
                "fecha_pago = '', fecha_recogida = '' "
                "WHERE estado = '" INGRESOS_ESTADO_ANULADO "' AND verifactu_estado IN ('ANULADA', 'RECTIFICADA') "
                "  AND (pagado IS NULL OR pagado != 'SI') "
                "  AND (fecha_anulacion IS NULL OR fecha_anulacion = '') "
                "  AND COALESCE(NULLIF(fecha_pago, ''), NULLIF(fecha_recogida, '')) IS NOT NULL"))
        fail() << "migrateDatabase: void-date backfill failed -" << q.lastError().text();
    else if (q.numRowsAffected() > 0)
        qDebug() << "migrateDatabase: moved the void date to fecha_anulacion on"
                 << q.numRowsAffected() << "voided garments";

    // 10.12 repair: before 10.12 an AEAT cancellation also marked the ticket's UNPAID
    // garments (they share seq 0) ANULADA, which left them neither chargeable nor
    // counted. A cancelled invoice only ever covered the paid rows, so relabel them
    // SIN COBRAR so they can be charged. Local voids (estado Anulado) are genuinely
    // cancelled and untouched. Idempotent.
    // RECTIFICADA is deliberately NOT repaired: an older substitution could have been
    // submitted with the unpaid garments' amounts included, so re-opening them could
    // invoice them twice. Those rows are only logged for manual review.
    {
        QSqlQuery review(db);
        if (review.exec("SELECT DISTINCT n_recibo FROM ingresos "
                        "WHERE (pagado IS NULL OR pagado != 'SI') AND verifactu_estado = 'RECTIFICADA' "
                        "  AND (estado IS NULL OR estado != '" INGRESOS_ESTADO_ANULADO "')"))
            while (review.next())
                if (firstNotice("rectificada-unpaid:" + review.value(0).toString()))
                    qWarning() << "migrateDatabase: ticket" << review.value(0).toString()
                               << "has unpaid garments marked RECTIFICADA - review manually before charging them";
    }
    if (!q.exec("UPDATE ingresos SET verifactu_estado = 'SIN COBRAR' "
                "WHERE (pagado IS NULL OR pagado != 'SI') "
                "  AND verifactu_estado = 'ANULADA' "
                "  AND (estado IS NULL OR estado != '" INGRESOS_ESTADO_ANULADO "')"))
        fail() << "migrateDatabase: unpaid-remainder repair failed -" << q.lastError().text();
    else if (q.numRowsAffected() > 0)
        qDebug() << "migrateDatabase: relabelled" << q.numRowsAffected()
                 << "unpaid garments wrongly marked ANULADA by a cancellation as SIN COBRAR";

    // 12.2 repair: before 10.10 the AEAT answer of a ticket's first payment was also
    // written onto its unpaid garments (same seq 0), which then carry a paid sibling's
    // ENVIADA + CSV for an invoice that never covered them; since 12.0 the guard trigger
    // refuses to charge them. Clear the copy: SIN COBRAR, or ANULADA when voided in
    // place. The estado moves first, so the trigger no longer guards the row. Idempotent.
    {
        const QString stamped = QStringLiteral(
            "SELECT u.rowid FROM ingresos u WHERE (u.pagado IS NULL OR u.pagado != 'SI') "
            "  AND u.verifactu_estado = 'ENVIADA' AND COALESCE(u.verifactu_csv, '') != '' "
            "  AND EXISTS (SELECT 1 FROM ingresos p WHERE p.n_recibo = u.n_recibo AND p.pagado = 'SI' "
            "              AND p.verifactu_csv = u.verifactu_csv)");
        QVector<qlonglong> rows;
        QSqlQuery find(db);
        if (!find.exec(stamped))
            fail() << "migrateDatabase: stamped-unpaid search failed -" << find.lastError().text();
        while (find.next())
            rows.append(find.value(0).toLongLong());
        find.finish();
        QSqlQuery relabel(db), clear(db);
        relabel.prepare("UPDATE ingresos SET verifactu_estado = CASE WHEN estado = '" INGRESOS_ESTADO_ANULADO
                        "' THEN 'ANULADA' ELSE 'SIN COBRAR' END WHERE rowid = :r");
        clear.prepare("UPDATE ingresos SET verifactu_csv = NULL, verifactu_timestamp = NULL, "
                      "verifactu_error = NULL, verifactu_url_qr = NULL, verifactu_xml = NULL, "
                      "verifactu_hash = NULL, verifactu_invoice_id = NULL, verifactu_invoice_seq = 0 "
                      "WHERE rowid = :r");
        db.transaction();   // every row or none
        bool cleared = true;
        for (qlonglong r : rows) {
            relabel.bindValue(":r", r);
            clear.bindValue(":r", r);
            if (!relabel.exec() || !clear.exec()) {
                cleared = false;
                fail() << "migrateDatabase: stamped-unpaid repair failed -"
                       << relabel.lastError().text() << clear.lastError().text();
                break;
            }
        }
        if (cleared) db.commit(); else db.rollback();
        if (cleared && !rows.isEmpty())
            qDebug() << "migrateDatabase: cleared a paid sibling's AEAT invoice data from"
                     << rows.size() << "unpaid garment(s)";
    }

    // 11.0 repair: Separar prendas used to insert the split-off garments with empty
    // verifactu_* columns and seq 0, so a split of a paid row fell out of the invoice
    // AEAT registered for it (export total, cancellation, reprint). Such a row is paid
    // with an empty estado at seq 0 and has, on the same ticket, rows of exactly one
    // AEAT invoice with the same fecha_pago, prenda and servicio: copy that invoice's
    // columns. Only ENVIADA invoices are relinked, which leaves every Contabilidad
    // figure as it was; a match with a cancelled, rectified or second invoice would move
    // income between periods, so it is only logged. One join (SQLite builds a temporary
    // index for it) keeps this cheap enough to run at every start.
    {
        QSqlQuery match(db);
        QVector<QPair<qlonglong, qlonglong>> relinks;   // (split row, invoice row)
        if (!match.exec("SELECT c.rowid, c.n_recibo, c.hash, c.importe, c.fecha_pago, "
                        "  COUNT(DISTINCT s.verifactu_invoice_seq), SUM(s.verifactu_estado != 'ENVIADA'), MIN(s.rowid) "
                        "FROM ingresos c JOIN ingresos s ON s.n_recibo = c.n_recibo AND s.pagado = 'SI' "
                        "  AND s.fecha_pago = c.fecha_pago AND s.prenda = c.prenda AND s.servicio = c.servicio "
                        "  AND COALESCE(s.verifactu_csv, '') != '' "
                        "WHERE c.pagado = 'SI' AND COALESCE(c.verifactu_estado, '') = '' "
                        "  AND COALESCE(c.verifactu_invoice_seq, 0) = 0 "
                        "GROUP BY c.rowid"))
            fail() << "migrateDatabase: split-row search failed -" << match.lastError().text();
        while (match.next()) {
            const QString ticket = match.value(1).toString();
            if (match.value(5).toInt() != 1 || match.value(6).toInt() != 0) {
                if (firstNotice("split-ambiguous:" + match.value(2).toString()))
                    qWarning() << "migrateDatabase: ticket" << ticket
                               << "has a split-off paid garment of a cancelled / rectified or ambiguous invoice"
                                  " - review manually";
                continue;
            }
            // A payment made with Verifactu off by a version before 8.5 on the same day,
            // garment and service matches the same way, so every relink is logged.
            qWarning() << "migrateDatabase: relinking paid garment" << match.value(2).toString()
                       << "of ticket" << ticket << "(" << match.value(3).toString()
                       << "paid" << match.value(4).toString()
                       << ") to its AEAT invoice as a split-off part - check it if it was a separate payment";
            relinks.append({ match.value(0).toLongLong(), match.value(7).toLongLong() });
        }
        match.finish();
        QSqlQuery upd(db);
        upd.prepare("UPDATE ingresos SET (verifactu_csv, verifactu_timestamp, verifactu_estado, "
                    "  verifactu_error, verifactu_url_qr, verifactu_xml, verifactu_hash, "
                    "  verifactu_rectifies_n_recibo, verifactu_rectification_type, "
                    "  verifactu_invoice_seq, verifactu_invoice_id, fecha_anulacion, verifactu_cancel_xml) = "
                    "(SELECT s.verifactu_csv, s.verifactu_timestamp, s.verifactu_estado, "
                    "  s.verifactu_error, s.verifactu_url_qr, s.verifactu_xml, s.verifactu_hash, "
                    "  s.verifactu_rectifies_n_recibo, s.verifactu_rectification_type, "
                    "  s.verifactu_invoice_seq, s.verifactu_invoice_id, s.fecha_anulacion, "
                    "  s.verifactu_cancel_xml FROM ingresos s WHERE s.rowid = :src) "
                    "WHERE rowid = :dst AND pagado = 'SI' AND COALESCE(verifactu_estado, '') = ''");
        db.transaction();   // every relink or none
        bool relinked = true;
        for (const auto &r : relinks) {
            upd.bindValue(":src", r.second);
            upd.bindValue(":dst", r.first);
            if (!upd.exec()) {
                relinked = false;
                fail() << "migrateDatabase: split-row relink failed -" << upd.lastError().text();
                break;
            }
        }
        if (relinked) db.commit(); else db.rollback();
        if (!relinks.isEmpty())
            qDebug() << "migrateDatabase: relinked" << relinks.size()
                     << "split-off paid garment(s) to their AEAT invoice";
    }

    // 12.0: a row is found by (n_recibo, hash), so two rows of one ticket sharing a hash
    // (the rand() era) or an empty hash were hit together by every write. Before the
    // rounding below, which writes by that key too, each such row gets a new hash -
    // replacing the old "Crear hash en ingresos" action. A row with a CSV keeps its own
    // and is only logged; within a group the row kept is one with a CSV if there is one.
    {
        QSqlQuery sel(db);
        if (!sel.exec("SELECT i.rowid, i.n_recibo, COALESCE(i.hash, ''), COALESCE(i.verifactu_csv, '') != '' "
                      "FROM ingresos i JOIN (SELECT n_recibo, COALESCE(hash, '') AS h FROM ingresos "
                      "      GROUP BY n_recibo, COALESCE(hash, '') HAVING COUNT(*) > 1 OR h = '') d "
                      "  ON d.n_recibo = i.n_recibo AND d.h = COALESCE(i.hash, '') "
                      "ORDER BY i.n_recibo, COALESCE(i.hash, ''), COALESCE(i.verifactu_csv, '') = '', i.rowid"))
            fail() << "migrateDatabase: duplicate-hash search failed -" << sel.lastError().text();
        struct Change { qlonglong rowid; QString ticket, oldHash; };
        QVector<Change> changes;
        QString groupTicket, groupHash;
        bool keptOne = false;
        while (sel.next()) {
            const QString ticket = sel.value(1).toString(), hash = sel.value(2).toString();
            const bool sent = sel.value(3).toBool();
            if (ticket != groupTicket || hash != groupHash) {
                groupTicket = ticket;
                groupHash = hash;
                keptOne = false;
            }
            if (!hash.isEmpty() && !keptOne) {   // sorted: a row with a CSV comes first
                keptOne = true;
                continue;
            }
            if (sent) {
                if (firstNotice("sent-hash:" + sel.value(0).toString()))
                    qWarning() << "migrateDatabase: ticket" << ticket << "has a row sent to AEAT whose hash"
                               << hash << "is empty or shared - review manually";
            }
            else
                changes.append({ sel.value(0).toLongLong(), ticket, hash });
        }
        sel.finish();
        QSqlQuery upd(db);
        upd.prepare("UPDATE ingresos SET hash = :h WHERE rowid = :rid");
        db.transaction();   // every new hash or none
        bool rehashed = true;
        for (const Change &c : changes) {
            const QString newHash = genHash16();
            upd.bindValue(":h",   newHash);
            upd.bindValue(":rid", c.rowid);
            if (upd.exec())
                qDebug() << "migrateDatabase: ticket" << c.ticket << "rowid" << c.rowid
                         << "hash" << c.oldHash << "->" << newHash;
            else {
                rehashed = false;
                fail() << "migrateDatabase: new hash for rowid" << c.rowid << "failed -" << upd.lastError().text();
                break;
            }
        }
        if (rehashed) db.commit(); else db.rollback();
        // Every write is keyed by (n_recibo, hash): from now on the database keeps it unique.
        // A sent row left sharing its hash (logged above) keeps the index from being built.
        if (!QSqlQuery(db).exec("CREATE UNIQUE INDEX IF NOT EXISTS ingresos_ticket_hash ON ingresos (n_recibo, hash)"))
            qWarning() << "migrateDatabase: (n_recibo, hash) is not unique yet - the index waits for the review";
    }

    // 10.14: amounts are stored in cents. Rows still open (unpaid, never sent) that an
    // older Recogida m2 edit stored with more decimals are rounded the way they will be
    // charged; no invoice covers them yet. Paid rows keep what was stored.
    {
        QSqlQuery sel(db);
        QVector<QPair<QString, QString>> rows;   // (n_recibo, hash)
        QStringList values;
        if (sel.exec("SELECT n_recibo, hash, importe FROM ingresos "
                     "WHERE importe GLOB '*.[0-9][0-9][0-9]*'" + kAmountEditableWhere)) {
            while (sel.next()) {
                rows.append({ sel.value(0).toString(), sel.value(1).toString() });
                values << moneyText(sel.value(2).toString());
            }
        }
        sel.finish();
        QSqlQuery upd(db);
        upd.prepare("UPDATE ingresos SET importe = :imp WHERE n_recibo = :n AND hash = :h" + kAmountEditableWhere);
        db.transaction();   // every rounding or none
        bool rounded = true;
        for (int i = 0; i < rows.size(); ++i) {
            upd.bindValue(":imp", values[i]);
            upd.bindValue(":n",   rows[i].first);
            upd.bindValue(":h",   rows[i].second);
            if (!upd.exec()) {
                rounded = false;
                fail() << "migrateDatabase: rounding importe of" << rows[i].first << "failed -"
                           << upd.lastError().text();
                break;
            }
        }
        if (rounded) db.commit(); else db.rollback();
        if (!rows.isEmpty())
            qDebug() << "migrateDatabase: rounded" << rows.size() << "unpaid amount(s) to cents";
    }

    // 12.0: comma decimals ("5,50") in the non-invoice amounts, which nothing sums or
    // prices, become dots (the Listado editors now store a dot); one with a dot as well
    // ("1.234,50") is ambiguous and left as it is. A gasto of a closed
    // quarter keeps its value: its report was filed without it. Paid ingresos are
    // invoices and are never rewritten.
    {
        const QStringList fixes = {
            "UPDATE gastos SET importe = REPLACE(TRIM(importe), ',', '.') "
            "WHERE importe LIKE '%,%' AND importe NOT LIKE '%.%' AND NOT " + closedQuarterSql("gastos.fecha"),
            "UPDATE prendas SET precio_limpieza = REPLACE(TRIM(precio_limpieza), ',', '.') WHERE precio_limpieza LIKE '%,%' AND precio_limpieza NOT LIKE '%.%'",
            "UPDATE prendas SET precio_plancha = REPLACE(TRIM(precio_plancha), ',', '.') WHERE precio_plancha LIKE '%,%' AND precio_plancha NOT LIKE '%.%'",
        };
        for (const QString &sql : fixes) {
            if (!q.exec(sql))
                fail() << "migrateDatabase: comma-decimal fix failed -" << q.lastError().text();
            else if (q.numRowsAffected() > 0)
                qDebug() << "migrateDatabase: replaced the decimal comma in" << q.numRowsAffected() << "amount(s) -" << sql.left(30);
        }
    }

    // Canonical casing. PendingSubmitsDialog used to write a literal 'Error',
    // which verifactuEstadoFromString() does not recognise (it fell through to
    // NotSubmitted, so those rows stopped offering "Reintentar"). Every canonical
    // estado is upper-case, and the SQL filters elsewhere compare case-sensitively,
    // so the stored value - not just the C++ read - has to be normalised.
    if (!q.exec("UPDATE ingresos SET verifactu_estado = UPPER(verifactu_estado) "
                "WHERE verifactu_estado IS NOT NULL AND verifactu_estado != '' "
                "  AND verifactu_estado != UPPER(verifactu_estado)"))
        fail() << "migrateDatabase: estado case normalisation failed -" << q.lastError().text();
    else if (q.numRowsAffected() > 0)
        qDebug() << "migrateDatabase: normalised casing on" << q.numRowsAffected()
                 << "verifactu_estado value(s)";

    q.finish();
    return ok;
}

int readMaxValueInColumnFromTable(QSqlDatabase &db, const QString &column, const QString &table, bool *ok)
{
    if (ok) *ok = false;
    if (dbNotConfigured(db, __func__)) return 0;

    int maxValue = 0;
    ConnectionScope scope(db);
    QSqlQuery q(db);
    // Numeric MAX: n_recibo is TEXT, and a text MAX would rank "99999" above "100000"
    // (the next ticket would repeat a number) or "9999" above "31163".
    q.exec("SELECT MAX(CAST(" + column + " AS INTEGER)) FROM " + table);
    if (q.isSelect()) {
        if (q.first()) {
            maxValue = q.value(0).toInt();
            if (ok) *ok = true;
        } else {
            qWarning() << "readMaxValueInColumnFromTable: empty result for column" << column << "in" << table;
        }
    } else {
        // Reported by the caller (*ok false), in its own window.
        qCritical() << "readMaxValueInColumnFromTable: query error for column" << column << "in" << table
                    << "-" << q.lastError().text();
    }
    return maxValue;
}

int readMaxNMinYearInColumnFromTable(QSqlDatabase &db, bool maxNMin, const QString &column, const QString &table)
{
    if (dbNotConfigured(db, __func__)) return 0;

    // Only four-digit years count: an empty date would make MIN() 0.
    const QString year = "substr(" + column + ",7,4)";
    QString query = QString(maxNMin ? "SELECT MAX(%1) FROM " : "SELECT MIN(%1) FROM ").arg(year) + table
                    + " WHERE " + year + " GLOB '[0-9][0-9][0-9][0-9]'";

    int value = 0;
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.exec(query);
    if (q.isSelect()) {
        if (q.first())
            value = q.value(0).toInt();
        else {
            qWarning() << "readMaxNMinYearInColumnFromTable: empty result for column" << column << "in" << table;
        }
    } else {
        qCritical() << "readMaxNMinYearInColumnFromTable: query error for column" << column << "in" << table;
    }
    return value;
}

QStringList readColumnFromTable(QSqlDatabase &db, const QString &column, const QString &table,
                                const QString &orderByColumn)
{
    if (dbNotConfigured(db, __func__)) return {};

    QStringList list;
    ConnectionScope scope(db);
    QSqlQuery q(db);
    if (orderByColumn.isEmpty())
        q.exec("SELECT " + column + " FROM " + table + ";");
    else
        q.exec("SELECT " + column + " FROM " + table + " ORDER BY " + orderByColumn + " ASC;");
    if (q.isSelect()) {
        while (q.next())
            list.append(q.value(0).toString());
    } else {
        qCritical() << "readColumnFromTable: query error for column" << column << "in" << table;
    }
    return list;
}

float readGarmentPrice(QSqlDatabase &db, const QString &garment, const QString &service)
{
    if (dbNotConfigured(db, __func__)) return 0.0f;

    const QString column = service == "Limp." ? QStringLiteral("precio_limpieza")
                         : service == "Plan." ? QStringLiteral("precio_plancha") : QString();
    if (column.isEmpty()) {
        qWarning() << "readGarmentPrice: unknown service" << service;
        return 0.0f;
    }

    float price = 0.0f;
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("SELECT " + column + " FROM prendas WHERE nombre = :garment COLLATE NOCASE");
    q.bindValue(":garment", garment);
    q.exec();
    if (q.isSelect()) {
        if (q.first()) {
            if (!q.value(0).toString().contains(',')) {
                price = q.value(0).toFloat();
            } else {
                qCritical() << "readGarmentPrice: comma decimal found in prendas for garment" << garment;
                price = -1.0f;
            }
        } else {
            qWarning() << "readGarmentPrice: garment not found:" << garment << "service:" << service;
            price = -1.0f;
        }
    } else {
        qCritical() << "readGarmentPrice: query error for garment" << garment << "service" << service;
    }
    return price;
}

QString selectFromWhereLike(QSqlDatabase &db, const QString &itemToGet, const QString &table,
                            const QString &columnToSearch, const QString &itemToSearch,
                            bool exactMatch, bool printMsg)
{
    if (dbNotConfigured(db, __func__)) return {};

    QString result;
    ConnectionScope scope(db);
    QSqlQuery q(db);
    // Exact: "="; otherwise a prefix with % and _ taken literally.
    q.prepare("SELECT " + itemToGet + " FROM " + table + " WHERE " + columnToSearch
              + (exactMatch ? QStringLiteral(" = :item") : QStringLiteral(" LIKE :item ESCAPE '\\'")));
    q.bindValue(":item", exactMatch ? itemToSearch
                                    : QString(itemToSearch).replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_") + "%");
    q.exec();
    if (q.isSelect()) {
        if (q.first()) {
            result = q.value(0).toString();
        } else if (printMsg) {
            qWarning() << "selectFromWhereLike: item not found:" << itemToSearch << "in column" << columnToSearch;
        }
    } else if (printMsg) {
        qCritical() << "selectFromWhereLike: query error for" << itemToSearch << "in column" << columnToSearch;
    }
    return result;
}

QString searchItemFromClient(QSqlDatabase &db, const QString &item, const QString &client, bool printMsg)
{
    // Exact: a prefix filled the form with another client's phone and address ("Ana" got
    // "Ana Maria"'s), which the save then wrote onto the typed client.
    return selectFromWhereLike(db, item, "clientes", "nombre", client, true, printMsg);
}

bool updateItemToClient(QSqlDatabase &db, const QString &column, const QString &item, const QString &client)
{
    if (dbNotConfigured(db, __func__)) return false;

    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("UPDATE clientes SET " + column + " = :item WHERE nombre = :client");
    q.bindValue(":item", item);
    q.bindValue(":client", client);
    bool ok = q.exec();
    if (!ok)
        qWarning() << "updateItemToClient: failed to update column" << column << "-" << q.lastError().text();
    return ok;
}

bool addNewClient(QSqlDatabase &db, const QString &client, const QString &telFijo,
                  const QString &direccion, const QString &movil)
{
    if (dbNotConfigured(db, __func__)) return false;

    // No name, phones nor address in the log: it is sent to support.
    qDebug() << "addNewClient: INSERT INTO clientes";
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("INSERT INTO clientes (nombre, tel_fijo, direccion, movil) "
              "VALUES (:nombre, :tel_fijo, :direccion, :movil)");
    q.bindValue(":nombre",    notNull(client));
    q.bindValue(":tel_fijo",  notNull(telFijo));
    q.bindValue(":direccion", notNull(direccion));
    q.bindValue(":movil",     notNull(movil));
    bool ok = q.exec();
    if (!ok)
        qWarning() << "addNewClient: failed to insert the client -" << q.lastError().text();
    return ok;
}

bool updateTicketPickup(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                        const QString &fechaRecogida, const QString &estado)
{
    if (dbNotConfigured(db, __func__)) return false;

    ConnectionScope scope(db);
    QSqlQuery q(db);
    // A voided row is never revived (same rule as markTicketPickedUp).
    q.prepare("UPDATE ingresos SET fecha_recogida = :fr, estado = :est "
              "WHERE n_recibo = :n AND hash = :h AND COALESCE(estado, '') != :anulado");
    q.bindValue(":fr",  fechaRecogida);
    q.bindValue(":est", estado);
    q.bindValue(":n",   nRecibo);
    q.bindValue(":h",   hash);
    q.bindValue(":anulado", QStringLiteral(INGRESOS_ESTADO_ANULADO));
    bool ok = q.exec();
    if (!ok)
        qWarning() << "updateTicketPickup: UPDATE failed -" << q.lastError().text();
    return ok;
}

bool markTicketPickedUp(QSqlDatabase &db, const QString &nRecibo, const QString &fechaRecogida)
{
    if (dbNotConfigured(db, __func__)) return false;

    ConnectionScope scope(db);
    QSqlQuery q(db);
    // Anulado rows stay Anulado - a voided garment is never revived to Recogido.
    // (estado IS NULL OR ...) so legacy rows with a NULL estado are still picked
    // up - SQLite treats `NULL != 'Anulado'` as NULL (excluded), not true.
    q.prepare("UPDATE ingresos SET fecha_recogida = :fr, estado = 'Recogido' "
              "WHERE n_recibo = :n AND (estado IS NULL OR estado != :anulado)");
    q.bindValue(":fr",      fechaRecogida);
    q.bindValue(":n",       nRecibo);
    q.bindValue(":anulado", QStringLiteral(INGRESOS_ESTADO_ANULADO));
    bool ok = q.exec();
    if (!ok)
        qWarning() << "markTicketPickedUp: UPDATE failed -" << q.lastError().text();
    return ok;
}

bool updateTicketObservations(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                              const QString &observaciones)
{
    if (dbNotConfigured(db, __func__)) return false;

    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET observaciones = :obs "
              "WHERE n_recibo = :n AND hash = :h");
    q.bindValue(":obs", observaciones);
    q.bindValue(":n",   nRecibo);
    q.bindValue(":h",   hash);
    bool ok = q.exec();
    if (!ok)
        qWarning() << "updateTicketObservations: UPDATE failed -" << q.lastError().text();
    return ok;
}

// Runs one amount UPDATE; false (and logged) when the row is invoiced or missing.
static bool execAmountUpdate(QSqlQuery &q, const char *caller, const QString &nRecibo, const QString &hash)
{
    if (!q.exec()) {
        qWarning() << caller << ": UPDATE failed -" << q.lastError().text();
        return false;
    }
    if (q.numRowsAffected() != 1) {
        qWarning() << caller << ": refused - ticket" << nRecibo << "row" << hash
                   << "is paid / sent to AEAT (or missing); its amount is frozen";
        return false;
    }
    return true;
}

bool updateTicketSizeAndPrice(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                              const QString &size, const QString &importe)
{
    if (dbNotConfigured(db, __func__)) return false;
    if (double amount = 0.0; !moneyParse(importe, &amount) || amount < 0.0) {
        qWarning() << __func__ << ": refused an unreadable or negative importe" << importe << "for" << nRecibo << hash;
        return false;
    }

    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET size = :sz, importe = :imp "
              "WHERE n_recibo = :n AND hash = :h" + kAmountEditableWhere);
    q.bindValue(":sz",  QString(size).trimmed().replace(',', '.'));
    q.bindValue(":imp", moneyText(importe));
    q.bindValue(":n",   nRecibo);
    q.bindValue(":h",   hash);
    const bool ok = execAmountUpdate(q, __func__, nRecibo, hash);
    return ok;
}

bool updateGarmentQtyAndImporte(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                const QString &cantidad, const QString &importe)
{
    if (dbNotConfigured(db, __func__)) return false;
    if (double amount = 0.0; !moneyParse(importe, &amount) || amount < 0.0) {
        qWarning() << __func__ << ": refused an unreadable or negative importe" << importe << "for" << nRecibo << hash;
        return false;
    }

    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET cantidad = :cant, importe = :imp "
              "WHERE n_recibo = :n AND hash = :h" + kAmountEditableWhere);
    q.bindValue(":cant", cantidad);
    q.bindValue(":imp",  moneyText(importe));
    q.bindValue(":n",    nRecibo);
    q.bindValue(":h",    hash);
    const bool ok = execAmountUpdate(q, __func__, nRecibo, hash);
    return ok;
}

QString splitGarmentRow(QSqlDatabase &db, const QString &nRecibo, const QString &hash, int nGarm)
{
    if (dbNotConfigured(db, __func__)) return QString();

    ConnectionScope scope(db);
    // Both writes or none: without a transaction a failed INSERT would leave the
    // original row reduced and the split-off garments lost. The row is read inside
    // it, so the amounts split below are the ones the UPDATE then guards.
    if (!db.transaction()) {
        qWarning() << "splitGarmentRow: could not start a transaction -" << db.lastError().text();
        return QString();
    }
    const auto abort = [&db]() {
        db.rollback();
        return QString();
    };
    QSqlQuery q(db);
    q.prepare("SELECT cantidad, importe, " + closedQuarterSql("ingresos.fecha_pago")
              + " FROM ingresos WHERE n_recibo = :n AND hash = :h");
    q.bindValue(":n", nRecibo);
    q.bindValue(":h", hash);
    if (!q.exec() || !q.first()) {
        qWarning() << "splitGarmentRow: row not found -" << nRecibo << hash << q.lastError().text();
        return abort();
    }
    const int qty = q.value(0).toInt();
    const QString oldImporte = q.value(1).toString();
    if (q.value(2).toBool()) {
        qWarning() << "splitGarmentRow: row is locked by Contabilidad -" << nRecibo << hash;
        return abort();
    }
    if (nGarm < 1 || nGarm >= qty) {
        qWarning() << "splitGarmentRow: cannot split" << nGarm << "of" << qty << "on" << nRecibo << hash;
        return abort();
    }
    // The split-off part in cents; the original keeps the exact remainder (in 1/10000),
    // so the two rows add up to the stored importe and an older paid amount stored
    // with more decimals is never re-rounded.
    const double total = QString(oldImporte).replace(',', '.').toDouble();
    const qint64 total4 = qRound64(total * 10000.0);
    const qint64 split4 = qRound64(roundToCents(total * nGarm / qty) * 100.0) * 100;
    const auto money = [](qint64 v4) {
        QString text = QString::number(v4 / 10000.0, 'f', 4);
        while (text.endsWith('0') && text.indexOf('.') < text.size() - 3)
            text.chop(1);
        return text;
    };
    const QString newHash = genHash16();
    q.finish();

    QSqlQuery u(db);
    // Same row as read above: still unlocked, with the same quantity and importe.
    u.prepare("UPDATE ingresos SET cantidad = :cant, importe = :imp WHERE n_recibo = :n AND hash = :h "
              "AND cantidad = :oldCant AND importe = :oldImp" + kUnlockedWhere);
    u.bindValue(":cant", QString::number(qty - nGarm));
    u.bindValue(":imp",  money(total4 - split4));
    u.bindValue(":n",    nRecibo);
    u.bindValue(":h",    hash);
    u.bindValue(":oldCant", QString::number(qty));
    u.bindValue(":oldImp",  oldImporte);
    bool ok = u.exec() && u.numRowsAffected() == 1;
    // Every other column is copied, verifactu_* included: the split-off garments stay
    // in the invoice (and the estado) of the row they come from. edit_lock: always '0' (see kUnlockedWhere).
    QSqlQuery i(db);
    i.prepare("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, "
              "  importe, pagado, estado, cantidad, prenda, size, servicio, observaciones, edit_lock, "
              "  hash, verifactu_csv, verifactu_timestamp, verifactu_estado, verifactu_error, "
              "  verifactu_url_qr, verifactu_xml, verifactu_hash, verifactu_rectifies_n_recibo, "
              "  verifactu_rectification_type, verifactu_invoice_seq, verifactu_invoice_id, "
              "  fecha_anulacion, verifactu_cancel_xml) "
              "SELECT n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, "
              "  :imp, pagado, estado, :cant, prenda, size, servicio, observaciones, '0', "
              "  :newHash, verifactu_csv, verifactu_timestamp, verifactu_estado, verifactu_error, "
              "  verifactu_url_qr, verifactu_xml, verifactu_hash, verifactu_rectifies_n_recibo, "
              "  verifactu_rectification_type, verifactu_invoice_seq, verifactu_invoice_id, "
              "  fecha_anulacion, verifactu_cancel_xml "
              "FROM ingresos WHERE n_recibo = :n AND hash = :h");
    i.bindValue(":imp",     money(split4));
    i.bindValue(":cant",    QString::number(nGarm));
    i.bindValue(":newHash", newHash);
    i.bindValue(":n",       nRecibo);
    i.bindValue(":h",       hash);
    ok = ok && i.exec() && i.numRowsAffected() == 1;
    if (ok && !db.commit()) {
        qWarning() << "splitGarmentRow: commit failed on" << nRecibo << hash << "-" << db.lastError().text();
        ok = false;
    } else if (!ok) {
        qWarning() << "splitGarmentRow: failed on" << nRecibo << hash << "-"
                   << u.lastError().text() << i.lastError().text();
    }
    if (!ok)
        db.rollback();
    return ok ? newHash : QString();
}

bool updateGarmentServiceAndImporte(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                    const QString &servicio, const QString &importe)
{
    if (dbNotConfigured(db, __func__)) return false;
    if (double amount = 0.0; !moneyParse(importe, &amount) || amount < 0.0) {
        qWarning() << __func__ << ": refused an unreadable or negative importe" << importe << "for" << nRecibo << hash;
        return false;
    }

    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET servicio = :serv, importe = :imp "
              "WHERE n_recibo = :n AND hash = :h" + kAmountEditableWhere);
    q.bindValue(":serv", servicio);
    q.bindValue(":imp",  moneyText(importe));
    q.bindValue(":n",    nRecibo);
    q.bindValue(":h",    hash);
    const bool ok = execAmountUpdate(q, __func__, nRecibo, hash);
    return ok;
}

bool garmentIsLocallyVoidable(const QString &pagado, const QString &verifactuEstado)
{
    if (pagado == QLatin1String("SI"))
        return false;
    return verifactuEstadoIsUnsubmitted(verifactuEstadoFromString(verifactuEstado));
}

// verifactu_estado values left out of every total: ANULADA (voided in place or
// cancelled at AEAT) and RECTIFICADA (superseded by a substitution rectificativa).
// Single source for garmentExcludedFromTotals and the kIngresosIncomeWhere SQL.
static const QStringList kTotalsExcludedEstados = { QStringLiteral("ANULADA"), QStringLiteral("RECTIFICADA") };

bool garmentExcludedFromTotals(const QString &verifactuEstado)
{
    return kTotalsExcludedEstados.contains(verifactuEstado);
}

bool ticketAllGarmentsPaid(QSqlDatabase &db, const QString &nRecibo)
{
    if (dbNotConfigured(db, __func__)) return false;

    ConnectionScope scope(db);
    QSqlQuery q(db);
    // Strict pagado='SI' (not "!= NO"): a blank must not read as paid. A ticket
    // with no rows is not "all paid" either, hence the total > 0 guard - otherwise
    // an empty result would print IMPORTE PAGADO on nothing. Voided rows keep
    // pagado='NO' and are not on the printed ticket, so they do not count.
    q.prepare("SELECT COUNT(*), SUM(CASE WHEN pagado = 'SI' THEN 1 ELSE 0 END) "
              "FROM ingresos WHERE n_recibo = :n AND (estado IS NULL OR estado != :anulado)");
    q.bindValue(":n", nRecibo);
    q.bindValue(":anulado", QStringLiteral(INGRESOS_ESTADO_ANULADO));
    bool allPaid = false;
    if (q.exec() && q.first()) {
        const int total = q.value(0).toInt();
        allPaid = total > 0 && q.value(1).toInt() == total;
    }
    return allPaid;
}

bool voidGarmentRow(QSqlDatabase &db, const QString &nRecibo, const QString &hash)
{
    if (dbNotConfigured(db, __func__)) return false;

    ConnectionScope scope(db);
    QSqlQuery q(db);
    // The void date goes to fecha_anulacion; fecha_pago / fecha_recogida stay empty
    // because the garment was never paid nor collected. pagado stays "NO", so no
    // accounting query ever counts it.
    q.prepare("UPDATE ingresos SET estado = :est, verifactu_estado = :vest, "
              "fecha_anulacion = :fecha, fecha_pago = '', fecha_recogida = '' "
              "WHERE n_recibo = :n AND hash = :h" + kAmountEditableWhere);
    q.bindValue(":est",   QStringLiteral(INGRESOS_ESTADO_ANULADO));
    q.bindValue(":vest",  verifactuEstadoToString(VerifactuEstado::Anulada));
    q.bindValue(":fecha", QDate::currentDate().toString("dd-MM-yyyy"));
    q.bindValue(":n",     nRecibo);
    q.bindValue(":h",     hash);
    // Only an open garment is voided here: never a paid, sent, cancelled or locked one.
    bool ok = q.exec();
    if (!ok)
        qWarning() << "voidGarmentRow: UPDATE failed -" << q.lastError().text();
    else if (q.numRowsAffected() != 1) {
        qWarning() << "voidGarmentRow: refused for" << nRecibo << hash << "(paid, sent, voided or locked)";
        ok = false;
    }
    return ok;
}

bool ticketHasPaidGarment(QSqlDatabase &db, const QString &nRecibo)
{
    if (dbNotConfigured(db, __func__)) return false;

    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM ingresos WHERE n_recibo = :n AND pagado = 'SI'");
    q.bindValue(":n", nRecibo);
    const bool paid = q.exec() && q.first() && q.value(0).toInt() > 0;
    return paid;
}

// Runs the INSERT on an already open db; the callers open, close and own the transaction.
static bool execGarmentInsert(QSqlDatabase &db, const IngresoGarmentRow &row)
{
    if (double amount = 0.0; !moneyParse(row.importe, &amount) || amount < 0.0) {
        qWarning() << "insertGarmentRow: refused an unreadable or negative importe" << row.importe
                   << "for" << row.nRecibo << row.hash;
        return false;
    }
    QSqlQuery q(db);
    q.prepare("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, "
              "fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, "
              "observaciones, edit_lock, hash, verifactu_estado, verifactu_invoice_seq) "
              "VALUES (:n_recibo, :cliente, :fecha_recepcion, :fecha_pago, :fecha_recogida, "
              ":importe, :pagado, :estado, :cantidad, :prenda, :size, :servicio, "
              ":observaciones, '0', :hash, :verifactu_estado, :verifactu_invoice_seq)");
    q.bindValue(":n_recibo",         notNull(row.nRecibo));
    q.bindValue(":cliente",          notNull(row.cliente));
    q.bindValue(":fecha_recepcion",  notNull(row.fechaRecepcion));
    q.bindValue(":fecha_pago",       notNull(row.fechaPago));
    q.bindValue(":fecha_recogida",   notNull(row.fechaRecogida));
    q.bindValue(":importe",          notNull(moneyText(row.importe)));
    q.bindValue(":pagado",           notNull(row.pagado));
    q.bindValue(":estado",           notNull(row.estado));
    q.bindValue(":cantidad",         notNull(row.cantidad));
    q.bindValue(":prenda",           notNull(row.prenda));
    q.bindValue(":size",             notNull(row.size));
    q.bindValue(":servicio",         notNull(row.servicio));
    q.bindValue(":observaciones",    notNull(row.observaciones));
    q.bindValue(":hash",             notNull(row.hash));
    q.bindValue(":verifactu_estado", notNull(row.verifactuEstado));
    q.bindValue(":verifactu_invoice_seq", row.verifactuInvoiceSeq);
    const bool ok = q.exec();
    if (!ok)
        qWarning() << "insertGarmentRow: INSERT failed for" << row.nRecibo << row.hash << "-" << q.lastError().text();
    return ok;
}

bool insertGarmentRow(QSqlDatabase &db, const IngresoGarmentRow &row)
{
    if (dbNotConfigured(db, __func__)) return false;

    ConnectionScope scope(db);
    const bool ok = execGarmentInsert(db, row);
    return ok;
}

bool insertGarmentRows(QSqlDatabase &db, const QList<IngresoGarmentRow> &rows)
{
    if (dbNotConfigured(db, __func__)) return false;

    ConnectionScope scope(db);
    // All the garments of a ticket or none: a partial ticket would be re-saved under
    // the same number, or offered to AEAT for only part of its amount.
    if (!db.transaction()) {
        qWarning() << "insertGarmentRows: could not start a transaction -" << db.lastError().text();
        return false;
    }
    for (const IngresoGarmentRow &row : rows) {
        if (!execGarmentInsert(db, row)) {
            db.rollback();
            return false;
        }
    }
    const bool ok = db.commit();
    if (!ok) {
        qWarning() << "insertGarmentRows: commit failed -" << db.lastError().text();
        db.rollback();
    }
    return ok;
}

// Period predicates of the Contabilidad detail listings, which are the single source
// of every Contabilidad figure (totals, counts and detail tables).
// Both bind :start / :end (yyyy-MM-dd) as the half-open range [start, end).
static const QString kFechaPagoIso =
    QStringLiteral("date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2))");
static const QString kFechaGastoIso =
    QStringLiteral("date(substr(fecha,7,4)||'-'||substr(fecha,4,2)||'-'||substr(fecha,1,2))");
// Earliest fecha_pago of a group, by date (MIN over the dd-MM-yyyy text would compare
// day first). Rows paid on different days in one seq-0 invoice predate 10.9: AEAT
// registered it under the first payment.
static const QString kEarliestFechaPago = "strftime('%d-%m-%Y', MIN(" + kFechaPagoIso + "))";
static const QString kFechaAnulacionIso =
    QStringLiteral("date(substr(fecha_anulacion,7,4)||'-'||substr(fecha_anulacion,4,2)||'-'||substr(fecha_anulacion,1,2))");
static const QString kLatestFechaAnulacion = "strftime('%d-%m-%Y', MAX(" + kFechaAnulacionIso + "))";
static const QString kExcludedEstadosSql =
    "('" + kTotalsExcludedEstados.join(QStringLiteral("','")) + "')";
// Paid ingresos, excluding kTotalsExcludedEstados - except a row cancelled /
// rectified with a fecha_anulacion, which its payment period keeps (the subtraction
// lands in the period of fecha_anulacion, see kRegularizationWhere).
// Legacy rows without verifactu_estado count as normal.
static const QString kIngresosIncomeWhere =
    QStringLiteral("(pagado = 'SI') AND "
                   "(verifactu_estado IS NULL OR verifactu_estado = '' OR verifactu_estado NOT IN ")
    + kExcludedEstadosSql + " OR (fecha_anulacion IS NOT NULL AND fecha_anulacion != '')) AND "
    + "(" + kFechaPagoIso + " >= date(:start)) AND (" + kFechaPagoIso + " < date(:end))";
// Regularisations accounted in the period of their fecha_anulacion.
static const QString kRegularizationWhere =
    "(pagado = 'SI') AND verifactu_estado IN " + kExcludedEstadosSql
    + " AND (" + kFechaAnulacionIso + " >= date(:start)) AND (" + kFechaAnulacionIso + " < date(:end))";
static const QString kGastosPeriodWhere =
    "(" + kFechaGastoIso + " >= date(:start)) AND (" + kFechaGastoIso + " < date(:end))";



bool quarterIsClosed(QSqlDatabase &db, int quarter, int year)
{
    if (dbNotConfigured(db, __func__)) return false;
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM accounting_closures WHERE year = :y AND quarter = :q AND reopened_at IS NULL");
    q.bindValue(":y", year);
    q.bindValue(":q", quarter);
    // Fails closed, like the SQL guards: an unreadable accounting_closures never opens a quarter.
    bool closed = true;
    if (q.exec() && q.first())
        closed = q.value(0).toInt() > 0;
    else
        qCritical() << "quarterIsClosed: query failed for quarter" << quarter << year
                    << "- treated as closed -" << q.lastError().text();
    q.finish();
    return closed;
}

// Appends a closure or reopens the open one; refused (false) when the quarter is
// already in the wanted state, which the unique index / the WHERE also enforce.
static bool writeClosure(QSqlDatabase &db, bool close, int quarter, int year, const QString &reportFile)
{
    if (dbNotConfigured(db, __func__)) return false;
    if (quarter < 1 || quarter > 4) {
        qWarning() << "writeClosure: invalid quarter" << quarter << year;
        return false;
    }
    ConnectionScope scope(db);
    QSqlQuery q(db);
    if (close) {
        q.prepare("INSERT INTO accounting_closures (year, quarter, closed_at, origin, report_file) "
                  "SELECT :y, :q, :now, 'contabilidad', :file WHERE NOT EXISTS (SELECT 1 FROM accounting_closures "
                  "  WHERE year = :y2 AND quarter = :q2 AND reopened_at IS NULL)");
        q.bindValue(":file", reportFile);
    } else {
        q.prepare("UPDATE accounting_closures SET reopened_at = :now "
                  "WHERE year = :y AND quarter = :q AND reopened_at IS NULL");
    }
    q.bindValue(":y", year);
    q.bindValue(":q", quarter);
    q.bindValue(":now", closureTimestamp());
    if (close) {
        q.bindValue(":y2", year);
        q.bindValue(":q2", quarter);
    }
    const bool ok = q.exec() && q.numRowsAffected() == 1;
    if (ok)
        qDebug() << "writeClosure: quarter" << quarter << year << (close ? "closed" : "reopened");
    else
        qWarning() << "writeClosure: quarter" << quarter << year << "not" << (close ? "closed" : "reopened")
                   << "(already so, or failed) -" << q.lastError().text();
    q.finish();
    return ok;
}

bool closeQuarter(QSqlDatabase &db, int quarter, int year, const QString &reportFile)
{
    return writeClosure(db, true, quarter, year, reportFile);
}

bool reopenQuarter(QSqlDatabase &db, int quarter, int year)
{
    return writeClosure(db, false, quarter, year, QString());
}

QVector<QuarterClosure> quarterClosures(QSqlDatabase &db, int quarter, int year)
{
    QVector<QuarterClosure> closures;
    if (dbNotConfigured(db, __func__)) return closures;
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("SELECT closed_at, reopened_at, origin, report_file FROM accounting_closures "
              "WHERE year = :y AND quarter = :q ORDER BY id");
    q.bindValue(":y", year);
    q.bindValue(":q", quarter);
    if (!q.exec())
        qWarning() << "quarterClosures: query failed for quarter" << quarter << year << "-" << q.lastError().text();
    while (q.next()) {
        QuarterClosure c;
        c.closedAt   = QDateTime::fromString(q.value(0).toString(), Qt::ISODate);
        c.reopenedAt = QDateTime::fromString(q.value(1).toString(), Qt::ISODate);
        c.migrated   = q.value(2).toString() == QLatin1String("migration");
        c.reportFile = q.value(3).toString();
        closures.append(c);
    }
    q.finish();
    return closures;
}

QSet<int> closedQuarterKeys(QSqlDatabase &db)
{
    QSet<int> keys;
    if (dbNotConfigured(db, __func__)) return keys;
    ConnectionScope scope(db);
    QSqlQuery q(db);
    if (!q.exec("SELECT year, quarter FROM accounting_closures WHERE reopened_at IS NULL"))
        qWarning() << "closedQuarterKeys: query failed -" << q.lastError().text();
    while (q.next())
        keys.insert(quarterKey(q.value(1).toInt(), q.value(0).toInt()));
    q.finish();
    return keys;
}

bool quarterHasMovements(QSqlDatabase &db, int quarter, int year)
{
    if (dbNotConfigured(db, __func__)) return false;
    // dd-MM-yyyy: substr(col, 4, 2) is MM and substr(col, 7, 4) is yyyy. One set of
    // placeholders per column, each bound below.
    const QStringList columns = { "fecha_pago", "fecha_anulacion", "fecha" };
    const auto inQuarter = [&columns](int i) {
        const QString n = QString::number(i);
        return "substr(" + columns[i] + ", 4, 2) IN (:a" + n + ", :b" + n + ", :c" + n + ")"
               " AND substr(" + columns[i] + ", 7, 4) = :y" + n;
    };
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("SELECT EXISTS (SELECT 1 FROM ingresos WHERE " + inQuarter(0) + ")"
              " OR EXISTS (SELECT 1 FROM ingresos WHERE " + inQuarter(1) + ")"
              " OR EXISTS (SELECT 1 FROM gastos WHERE " + inQuarter(2) + ")");
    for (int i = 0; i < columns.size(); ++i) {
        const QString n = QString::number(i);
        q.bindValue(":a" + n, monthStr((quarter - 1) * 3 + 1));
        q.bindValue(":b" + n, monthStr((quarter - 1) * 3 + 2));
        q.bindValue(":c" + n, monthStr((quarter - 1) * 3 + 3));
        q.bindValue(":y" + n, QString::number(year));
    }
    bool any = false;
    if (q.exec() && q.first())
        any = q.value(0).toBool();
    else
        qWarning() << "quarterHasMovements: query failed for quarter" << quarter << year << "-" << q.lastError().text();
    q.finish();
    return any;
}

QString verifactuEventRecordXml(QSqlDatabase &db, const QString &nRecibo, int seq)
{
    if (dbNotConfigured(db, __func__)) return {};
    QString xml;
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("SELECT verifactu_xml FROM ingresos WHERE n_recibo = :n AND verifactu_invoice_seq = :s "
              "AND pagado = 'SI' AND COALESCE(verifactu_xml, '') != '' LIMIT 1");
    q.bindValue(":n", nRecibo);
    q.bindValue(":s", seq);
    if (q.exec() && q.next())
        xml = q.value(0).toString();
    return xml;
}

// The only reads of aeat_records, a table AeatStore owns (src/verifactu/aeatstore.cpp):
// sql_lite cannot link verifactu, so a column change there must be repeated here.
// test_aeat_store runs both against the real table. A failed query (the table exists only
// once the direct client was used) reads as nothing.
static int pendingAeatRecordCount(QSqlDatabase &db, const QString &environment)
{
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM aeat_records WHERE state = 'PENDIENTE' AND environment = :e");
    q.bindValue(":e", environment);
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}

static QStringList aeatInvoiceNumbersOfTicket(QSqlDatabase &db, const QString &ticketNum, const QString &environment)
{
    QStringList numbers;
    QSqlQuery q(db);
    q.prepare("SELECT invoice_number FROM aeat_records WHERE kind = 'alta' "
              "AND (invoice_number = :n OR invoice_number LIKE :prefix) AND (:e = '' OR environment = :e)");
    q.bindValue(":n", ticketNum);
    q.bindValue(":e", environment);
    q.bindValue(":prefix", ticketNum + QStringLiteral("-%"));
    if (q.exec())
        while (q.next())
            numbers << q.value(0).toString();
    return numbers;
}

int aeatPendingRecordCount(QSqlDatabase &db, const QString &environment)
{
    if (dbNotConfigured(db, __func__)) return 0;
    ConnectionScope scope(db);
    return pendingAeatRecordCount(db, environment);
}

// Numeric order of n_recibo: tickets by number, then the rectificativas' own "R-<n>"
// series above them by n (a text order would rank "99999" above "100000").
#define N_RECIBO_ORDER "CASE WHEN n_recibo LIKE 'R-%' THEN 1000000000 + CAST(substr(n_recibo, 3) AS INTEGER) ELSE CAST(n_recibo AS INTEGER) END"

// "31277" -> (31277, 0), "31277-1" -> (31277, 1); a rectificativa "R-5" is its own number (seq 0).
static void splitInvoiceId(const QString &invoiceId, QString *nRecibo, int *seq)
{
    *nRecibo = invoiceId.trimmed();
    *seq = 0;
    const int dash = nRecibo->lastIndexOf(QLatin1Char('-'));
    if (dash > 0 && !(nRecibo->startsWith(QLatin1String("R-")) && dash == 1)) {
        bool numeric = false;
        const int parsed = nRecibo->mid(dash + 1).toInt(&numeric);
        if (numeric) {
            *seq = parsed;
            *nRecibo = nRecibo->left(dash);
        }
    }
}

int applySettledVerifactuResult(QSqlDatabase &db, const QString &invoiceId, const VerifactuResult &result)
{
    QString nRecibo;
    int seq = 0;
    splitInvoiceId(invoiceId, &nRecibo, &seq);
    if (verifactuEventFor(db, nRecibo, seq).nRecibo.isEmpty())
        return -1;
    const int changed = updateTicketVerifactuFields(db, nRecibo, result, seq);
    // A substitution rectificativa AEAT accepted after its dialog closed: the original
    // stops counting as income, as the dialog does when the reply reaches it.
    if (changed > 0 && result.isSuccess()) {
        const QString original = rectifiedTicketOf(db, nRecibo);
        if (!original.isEmpty()) {
            ConnectionScope scope(db);
            QSqlQuery q(db);
            q.prepare("SELECT MIN(verifactu_rectification_type), " + kEarliestFechaPago + " FROM ingresos "
                      "WHERE n_recibo = :n AND pagado = 'SI'");
            q.bindValue(":n", nRecibo);
            const bool read = q.exec() && q.next();
            const QString type = read ? q.value(0).toString() : QString();
            const QDate date = read ? QDate::fromString(q.value(1).toString(), "dd-MM-yyyy") : QDate();
            q.finish();
            // Into an open period: a quarter closed meanwhile must not change.
            const QDate when = openPeriodDate(db, date);
            if (type == QLatin1String("S") && !markTicketRectified(db, original, when))
                qWarning() << "applySettledVerifactuResult: rectificativa" << nRecibo << "accepted, original" << original
                           << "not marked RECTIFICADA";
        }
    }
    return changed;
}

int nextTicketNumber(QSqlDatabase &db, int highestAtAeat, bool *ok)
{
    bool read = false;
    const int highest = readMaxValueInColumnFromTable(db, "n_recibo", "ingresos", &read);
    if (ok) *ok = read;
    if (!read)
        qCritical() << "nextTicketNumber: the highest ticket number could not be read - no number given";
    return read ? qMax(highest, highestAtAeat) + 1 : 0;
}

QString nextRectificationNumber(QSqlDatabase &db, int highestAtAeat, bool *ok)
{
    if (ok) *ok = false;
    if (dbNotConfigured(db, __func__)) return QString();
    ConnectionScope scope(db);
    QSqlQuery q(db);
    if (!q.exec("SELECT COALESCE(MAX(CAST(substr(n_recibo, 3) AS INTEGER)), 0) FROM ingresos "
                "WHERE n_recibo LIKE 'R-%'") || !q.first()) {
        qCritical() << "nextRectificationNumber: the highest rectificativa number could not be read -"
                    << q.lastError().text();
        return QString();
    }
    if (ok) *ok = true;
    return QStringLiteral("R-%1").arg(qMax(q.value(0).toInt(), highestAtAeat) + 1);
}

QVector<RefusedInvoice> refusedInvoices(QSqlDatabase &db, const QString &floorIso)
{
    QVector<RefusedInvoice> invoices;
    if (dbNotConfigured(db, __func__)) return invoices;
    ConnectionScope scope(db);
    if (!scope.isOpen()) return invoices;
    QSqlQuery q(db);
    q.prepare("SELECT n_recibo, verifactu_invoice_seq, " + kEarliestFechaPago + ", SUM(" INGRESOS_IMPORTE_VALUE "), "
              "MAX(COALESCE(verifactu_error, '')), MAX(COALESCE(verifactu_rectifies_n_recibo, '') != '') "
              "FROM ingresos WHERE pagado = 'SI' AND verifactu_estado = 'ERROR' "
              "GROUP BY n_recibo, verifactu_invoice_seq "
              // The invoice's own date (its earliest payment) against the floor; an unreadable
              // floor or payment date never hides a refusal.
              "HAVING date(:floor) IS NULL OR MIN(" + kFechaPagoIso + ") IS NULL "
              "    OR MIN(" + kFechaPagoIso + ") >= date(:floor2) "
              "ORDER BY MIN(" + kFechaPagoIso + ") DESC, " N_RECIBO_ORDER " DESC");
    q.bindValue(":floor", floorIso);
    q.bindValue(":floor2", floorIso);
    if (!q.exec())
        qWarning() << "refusedInvoices: query failed -" << q.lastError().text();
    while (q.next()) {
        RefusedInvoice r;
        r.nRecibo = q.value(0).toString();
        r.seq = q.value(1).toInt();
        r.fechaPago = q.value(2).toString();
        r.importe = roundToCents(q.value(3).toDouble());
        r.error = q.value(4).toString();
        r.rectificativa = q.value(5).toBool();
        invoices.append(r);
    }
    q.finish();
    return invoices;
}

int refusedInvoiceCount(QSqlDatabase &db, const QString &floorIso)
{
    return refusedInvoices(db, floorIso).size();
}

QStringList pendingRectificationNumbers(QSqlDatabase &db)
{
    QStringList out;
    if (dbNotConfigured(db, __func__)) return out;
    ConnectionScope scope(db);
    if (!scope.isOpen()) return out;
    QSqlQuery q(db);
    if (q.exec("SELECT DISTINCT n_recibo FROM ingresos WHERE COALESCE(verifactu_rectifies_n_recibo, '') != '' "
               "AND pagado = 'SI' AND verifactu_estado = 'PENDIENTE'"))
        while (q.next())
            out << q.value(0).toString();
    q.finish();
    return out;
}

bool markRectificationUnsent(QSqlDatabase &db, const QString &number, const QString &reason)
{
    if (dbNotConfigured(db, __func__)) return false;
    ConnectionScope scope(db);
    if (!scope.isOpen()) return false;
    QSqlQuery q(db);
    // Only a placeholder with no answer at all: one AEAT answered without its CSV (the
    // gateway's PENDING, its reason stored) may be held by AEAT and stays PENDIENTE.
    q.prepare("UPDATE ingresos SET verifactu_estado = 'ERROR', verifactu_error = :r WHERE n_recibo = :n "
              "AND COALESCE(verifactu_rectifies_n_recibo, '') != '' AND verifactu_estado = 'PENDIENTE' "
              "AND COALESCE(verifactu_error, '') = ''");
    q.bindValue(":r", reason);
    q.bindValue(":n", number);
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (!ok)
        qWarning() << "markRectificationUnsent: rectificativa" << number << "not updated -" << q.lastError().text();
    return ok;
}

bool markRectificationResent(QSqlDatabase &db, const QString &number)
{
    if (dbNotConfigured(db, __func__)) return false;
    ConnectionScope scope(db);
    if (!scope.isOpen()) return false;
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET verifactu_estado = 'PENDIENTE', verifactu_error = '' WHERE n_recibo = :n "
              "AND COALESCE(verifactu_rectifies_n_recibo, '') != '' AND verifactu_estado = 'ERROR'");
    q.bindValue(":n", number);
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (!ok)
        qWarning() << "markRectificationResent: rectificativa" << number << "not updated -" << q.lastError().text();
    return ok;
}

QString pendingRectificationOf(QSqlDatabase &db, const QString &original)
{
    if (dbNotConfigured(db, __func__)) return QString();
    ConnectionScope scope(db);
    if (!scope.isOpen()) return QString();
    QSqlQuery q(db);
    q.prepare("SELECT n_recibo FROM ingresos WHERE verifactu_rectifies_n_recibo = :o AND pagado = 'SI' "
              "AND verifactu_estado = 'PENDIENTE' LIMIT 1");
    q.bindValue(":o", original);
    const QString number = q.exec() && q.next() ? q.value(0).toString() : QString();
    q.finish();
    return number;
}

QString refusedRectificationOf(QSqlDatabase &db, const QString &original)
{
    if (dbNotConfigured(db, __func__)) return QString();
    ConnectionScope scope(db);
    if (!scope.isOpen()) return QString();
    QSqlQuery q(db);
    q.prepare("SELECT n_recibo FROM ingresos WHERE verifactu_rectifies_n_recibo = :o AND pagado = 'SI' "
              "AND verifactu_estado = 'ERROR' ORDER BY " N_RECIBO_ORDER " DESC LIMIT 1");
    q.bindValue(":o", original);
    const QString number = q.exec() && q.next() ? q.value(0).toString() : QString();
    q.finish();
    return number;
}

bool liveRectificationOf(QSqlDatabase &db, const QString &ticket, QString *number)
{
    number->clear();
    if (dbNotConfigured(db, __func__)) return false;
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("WITH RECURSIVE chain(n) AS ("
              "  SELECT n_recibo FROM ingresos WHERE verifactu_rectifies_n_recibo = :t AND pagado = 'SI' "
              "  UNION SELECT i.n_recibo FROM ingresos i JOIN chain c ON i.verifactu_rectifies_n_recibo = c.n "
              "    WHERE i.pagado = 'SI') "
              "SELECT n_recibo FROM ingresos WHERE n_recibo IN (SELECT n FROM chain) AND pagado = 'SI' "
              "  AND verifactu_estado = 'ENVIADA' ORDER BY " N_RECIBO_ORDER " DESC LIMIT 1");
    q.bindValue(":t", ticket);
    if (!q.exec()) {
        qWarning() << "liveRectificationOf: lookup failed for" << ticket << "-" << q.lastError().text();
        return false;
    }
    if (q.next())
        *number = q.value(0).toString();
    return true;
}

double rectifiedDifferences(QSqlDatabase &db, const QString &original, bool *ok)
{
    if (ok) *ok = false;
    if (dbNotConfigured(db, __func__)) return 0.0;
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("SELECT COALESCE(SUM(" INGRESOS_IMPORTE_VALUE "), 0) FROM ingresos "
              "WHERE verifactu_rectifies_n_recibo = :o AND verifactu_rectification_type = 'I' AND pagado = 'SI' "
              "AND COALESCE(verifactu_estado, '') NOT IN ('ERROR', 'ANULADA')");
    q.bindValue(":o", original);
    if (!q.exec() || !q.next()) {
        qWarning() << "rectifiedDifferences: read failed for" << original << "-" << q.lastError().text();
        return 0.0;
    }
    if (ok) *ok = true;
    return roundToCents(q.value(0).toDouble());
}

int differenceRectificationCount(QSqlDatabase &db, const QString &original, bool *ok)
{
    if (ok) *ok = false;
    if (dbNotConfigured(db, __func__)) return 0;
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(DISTINCT n_recibo) FROM ingresos "
              "WHERE verifactu_rectifies_n_recibo = :o AND verifactu_rectification_type = 'I' AND pagado = 'SI' "
              "AND COALESCE(verifactu_estado, '') NOT IN ('ERROR', 'ANULADA')");
    q.bindValue(":o", original);
    if (!q.exec() || !q.next()) {
        qWarning() << "differenceRectificationCount: read failed for" << original << "-" << q.lastError().text();
        return 0;
    }
    if (ok) *ok = true;
    return q.value(0).toInt();
}

QString rectifiedTicketOf(QSqlDatabase &db, const QString &nRecibo)
{
    if (dbNotConfigured(db, __func__)) return QString();
    ConnectionScope scope(db);
    if (!scope.isOpen()) return QString();
    QSqlQuery q(db);
    q.prepare("SELECT MIN(verifactu_rectifies_n_recibo) FROM ingresos "
              "WHERE n_recibo = :n AND COALESCE(verifactu_rectifies_n_recibo, '') != ''");
    q.bindValue(":n", nRecibo);
    const QString original = q.exec() && q.next() ? q.value(0).toString() : QString();
    q.finish();
    return original;
}

bool storeChargedGarments(QSqlDatabase &db, const QString &nRecibo, const QHash<QString, QString> &amountByHash,
                          const QDate &paymentDate, int seq, const QString &invoiceId)
{
    if (dbNotConfigured(db, __func__) || amountByHash.isEmpty()) return false;
    if (quarterIsClosed(db, paymentDate)) {
        qWarning() << "storeChargedGarments: the payment date of" << invoiceId << "is in a closed quarter";
        return false;
    }
    ConnectionScope scope(db);
    if (!scope.isOpen() || !db.transaction()) {
        qCritical() << "storeChargedGarments: no transaction -" << db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    // importe = the amount shown and sent (cents), so an older row stored with more
    // decimals is invoiced and stored with the same figure.
    q.prepare("UPDATE ingresos SET pagado = 'SI', fecha_pago = :fp, verifactu_invoice_seq = :seq, "
              "importe = :imp, verifactu_estado = :estado, verifactu_invoice_id = :id, verifactu_error = '' "
              "WHERE n_recibo = :n AND hash = :h AND COALESCE(pagado, '') != 'SI'");
    bool ok = true;
    for (auto it = amountByHash.cbegin(); ok && it != amountByHash.cend(); ++it) {
        q.bindValue(":fp",     paymentDate.toString("dd-MM-yyyy"));
        q.bindValue(":seq",    seq);
        q.bindValue(":imp",    moneyText(it.value()));
        q.bindValue(":estado", verifactuEstadoToString(VerifactuEstado::NotSubmitted));
        q.bindValue(":id",     invoiceId);
        q.bindValue(":n",      nRecibo);
        q.bindValue(":h",      it.key());
        if (!q.exec() || q.numRowsAffected() != 1) {
            qWarning() << "storeChargedGarments: garment" << it.key() << "of ticket" << nRecibo
                       << "not stored as paid (paid meanwhile?) -" << q.lastError().text();
            ok = false;
        }
    }
    q.finish();
    ok = ok && db.commit();
    if (!ok)
        db.rollback();
    return ok;
}

bool markPendingEventUnreconciled(QSqlDatabase &db, const QString &nRecibo, int seq)
{
    if (dbNotConfigured(db, __func__)) return false;
    ConnectionScope scope(db);
    if (!scope.isOpen()) return false;
    QSqlQuery q(db);
    // Scoped by seq: a sibling event of the same ticket still PENDIENTE is left alone.
    q.prepare("UPDATE ingresos SET verifactu_estado = :estado, "
              "verifactu_error = 'Pendiente sin reconciliar tras cierre - revisar en sede AEAT' "
              "WHERE n_recibo = :n AND verifactu_invoice_seq = :seq "
              "  AND COALESCE(verifactu_estado, '') IN ('', 'PENDIENTE')");
    q.bindValue(":estado", verifactuEstadoToString(VerifactuEstado::Error));
    q.bindValue(":n",      nRecibo);
    q.bindValue(":seq",    seq);
    const bool ok = q.exec();
    if (!ok)
        qWarning() << "markPendingEventUnreconciled: UPDATE failed for ticket" << nRecibo << "-" << q.lastError().text();
    q.finish();
    return ok;
}

bool insertRectificativaRow(QSqlDatabase &db, const RectificativaRow &row)
{
    if (dbNotConfigured(db, __func__)) return false;
    if (quarterIsClosed(db, row.date)) {
        qWarning() << "insertRectificativaRow: the date of" << row.number << "is in a closed quarter";
        return false;
    }
    ConnectionScope scope(db);
    if (!scope.isOpen()) return false;
    const QString date = row.date.toString("dd-MM-yyyy");
    QSqlQuery q(db);
    // A rectificativa row carries the verifactu_* set IngresoGarmentRow does not; AEAT's
    // metadata is written by applyRectificativaResult on the reply.
    q.prepare("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, "
              "  pagado, estado, cantidad, prenda, size, servicio, observaciones, edit_lock, hash, "
              "  verifactu_csv, verifactu_timestamp, verifactu_estado, verifactu_error, verifactu_url_qr, "
              "  verifactu_xml, verifactu_hash, verifactu_rectifies_n_recibo, verifactu_rectification_type) "
              "VALUES (:n, :cliente, :f, :f, '', :imp, 'SI', 'Rectificativa', '1', :prenda, '', '', :obs, '0', :hash, "
              "  '', '', :estado, '', '', '', '', :orig, :type)");
    q.bindValue(":n",       row.number);
    q.bindValue(":cliente", notNull(row.client));
    q.bindValue(":f",       date);                  // a rectificativa always reflects a paid operation
    q.bindValue(":imp",     moneyText(QString::number(row.amount, 'f', 2)));
    q.bindValue(":prenda",  QStringLiteral("Rectificativa de ticket %1").arg(row.original));
    q.bindValue(":obs",     QStringLiteral("Rectificativa %1 (%2)").arg(row.label, row.type));
    q.bindValue(":hash",    genHash16());
    q.bindValue(":estado",  verifactuEstadoToString(VerifactuEstado::NotSubmitted));
    q.bindValue(":orig",    row.original);
    q.bindValue(":type",    row.type);
    const bool ok = q.exec();
    if (!ok)
        qWarning() << "insertRectificativaRow: INSERT failed for" << row.number << "-" << q.lastError().text();
    q.finish();
    return ok;
}

bool resetRefusedRectificativa(QSqlDatabase &db, const RectificativaRow &row)
{
    if (dbNotConfigured(db, __func__)) return false;
    if (quarterIsClosed(db, row.date)) {
        qWarning() << "resetRefusedRectificativa: the new date of" << row.number << "is in a closed quarter";
        return false;
    }
    ConnectionScope scope(db);
    if (!scope.isOpen()) return false;
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET fecha_recepcion = :f, fecha_pago = :f, importe = :imp, "
              "verifactu_rectification_type = :t, verifactu_estado = :pending, verifactu_error = '' "
              "WHERE n_recibo = :n AND verifactu_rectifies_n_recibo = :o AND verifactu_estado = :refused");
    q.bindValue(":f",       row.date.toString("dd-MM-yyyy"));
    q.bindValue(":imp",     moneyText(QString::number(row.amount, 'f', 2)));
    q.bindValue(":t",       row.type);
    q.bindValue(":pending", verifactuEstadoToString(VerifactuEstado::NotSubmitted));
    q.bindValue(":n",       row.number);
    q.bindValue(":o",       row.original);
    q.bindValue(":refused", verifactuEstadoToString(VerifactuEstado::Error));
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (!ok)
        qWarning() << "resetRefusedRectificativa: no refused row" << row.number << "of" << row.original
                   << "-" << q.lastError().text();
    q.finish();
    return ok;
}

int applyRectificativaResult(QSqlDatabase &db, const QString &number, const QString &original,
                             const VerifactuResult &result, VerifactuEstado estado)
{
    if (dbNotConfigured(db, __func__)) return -1;
    ConnectionScope scope(db);
    if (!scope.isOpen()) return -1;
    const bool accepted = result.isSuccess();
    QSqlQuery q(db);
    // A refusal or an unknown outcome never rewrites an accepted row (a late duplicate).
    q.prepare("UPDATE ingresos SET verifactu_csv = :csv, verifactu_timestamp = :ts, "
              "verifactu_estado = :estado, verifactu_error = :error, verifactu_url_qr = :url, "
              "verifactu_xml = :xml, verifactu_hash = :hash, verifactu_invoice_id = :id "
              "WHERE n_recibo = :n AND verifactu_rectifies_n_recibo = :o"
              + QString(accepted ? "" : " AND COALESCE(verifactu_estado, '') != 'ENVIADA'"));
    q.bindValue(":csv",    accepted ? result.csv : QString(""));
    q.bindValue(":ts",     QDateTime::currentDateTime().toString(Qt::ISODate));
    q.bindValue(":estado", verifactuEstadoToString(accepted ? VerifactuEstado::Enviada : estado));
    q.bindValue(":error",  accepted ? QString("") : result.errorDescription);
    q.bindValue(":url",    accepted ? result.validationUrl : QString(""));
    q.bindValue(":xml",    accepted ? result.rawXml : QString(""));
    q.bindValue(":hash",   accepted ? result.rawHash : QString(""));
    // The literal InvoiceID AEAT holds it under, read by Imprimir's QR and header.
    q.bindValue(":id",     accepted ? number : QString(""));
    q.bindValue(":n",      number);
    q.bindValue(":o",      original);
    const int changed = q.exec() ? q.numRowsAffected() : -1;
    if (changed < 0)
        qWarning() << "applyRectificativaResult: UPDATE failed for" << number << "-" << q.lastError().text();
    else if (changed == 0 && !accepted)
        qWarning() << "applyRectificativaResult:" << number << "is already accepted - the late answer is ignored";
    q.finish();
    return changed;
}

bool markRectificativaUnanswered(QSqlDatabase &db, const QString &number, const QString &original,
                                 const QString &error)
{
    if (dbNotConfigured(db, __func__)) return false;
    ConnectionScope scope(db);
    if (!scope.isOpen()) return false;
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET verifactu_estado = :e, verifactu_error = :err "
              "WHERE n_recibo = :n AND verifactu_rectifies_n_recibo = :o AND verifactu_estado = :pending");
    q.bindValue(":e",       verifactuEstadoToString(VerifactuEstado::Error));
    q.bindValue(":err",     error);
    q.bindValue(":n",       number);
    q.bindValue(":o",       original);
    q.bindValue(":pending", verifactuEstadoToString(VerifactuEstado::NotSubmitted));
    const bool ok = q.exec();
    if (!ok)
        qWarning() << "markRectificativaUnanswered: UPDATE failed for" << number << "-" << q.lastError().text();
    q.finish();
    return ok;
}

int applySettledVerifactuCancellation(QSqlDatabase &db, const QString &invoiceId, const QDate &cancelDate,
                                      const VerifactuResult &result)
{
    QString nRecibo;
    int seq = 0;
    splitInvoiceId(invoiceId, &nRecibo, &seq);
    if (dbNotConfigured(db, __func__))
        return 0;
    // Into an open period, like applySettledVerifactuResult: a quarter closed meanwhile keeps its figures.
    const QDate stampDate = openPeriodDate(db, cancelDate);
    if (stampDate != cancelDate)
        qWarning() << "applySettledVerifactuCancellation:" << invoiceId << "dated" << stampDate
                   << "instead of" << cancelDate << "(closed quarter)";
    // -1 only when the database answers that no paid row carries the InvoiceID: a read
    // failure is 0, so an accepted cancellation of a real ticket is never passed over.
    ConnectionScope scope(db);
    if (!scope.isOpen())
        return 0;
    int outcome = 0;   // 0 = mark it (if accepted)
    {
        QSqlQuery q(db);
        q.prepare("SELECT COUNT(*), SUM(COALESCE(verifactu_estado, '') != 'ANULADA') FROM ingresos "
                  "WHERE n_recibo = :n AND verifactu_invoice_seq = :seq AND pagado = 'SI'");
        q.bindValue(":n",   nRecibo);
        q.bindValue(":seq", seq);
        if (!q.exec() || !q.next()) {
            qWarning() << "applySettledVerifactuCancellation: lookup failed for" << invoiceId << "-" << q.lastError().text();
            outcome = 3;                                            // read failure: not marked
        } else if (q.value(0).toInt() == 0) {
            outcome = -1;
        } else if (result.isSuccess() && q.value(1).toInt() == 0) {
            outcome = 2;                                            // already marked (its dialog did)
        }
    }
    if (outcome == -1 || outcome == 2)
        return outcome;
    if (outcome == 3 || !result.isSuccess())
        return 0;
    return markInvoiceSeqCancelled(db, nRecibo, seq, stampDate, result.rawXml) ? 1 : 0;
}

QStringList readClientPhones(QSqlDatabase &db, const QString &client)
{
    QStringList phones = { QString(), QString() }; // {tel_fijo, movil}
    if (dbNotConfigured(db, __func__)) return phones;

    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("SELECT tel_fijo, movil FROM clientes WHERE nombre = :n");
    q.bindValue(":n", client);
    if (q.exec() && q.first()) {
        phones[0] = q.value(0).toString();
        phones[1] = q.value(1).toString();
    } else if (q.lastError().isValid()) {
        qWarning() << "readClientPhones: query error -" << q.lastError().text();
    }
    return phones;
}

int expenseIvaRate(const QVariant &stored)
{
    if (stored.isNull())
        return -1;
    bool ok = false;
    const double rate = stored.toString().trimmed().replace(',', '.').toDouble(&ok);
    return ok && rate >= 0.0 && rate == std::floor(rate) ? int(rate) : -1;
}

double garmentImporte(const QString &quantityText, const QString &sizeText, double unitPrice)
{
    const double quantity = quantityText.trimmed().replace(',', '.').toDouble();
    // The price list is in cents; readGarmentPrice hands a float, so re-round first.
    double price = quantity * roundToCents(unitPrice);
    if (price < 0.0)
        return 0.0;
    const double size = sizeText.trimmed().replace(',', '.').toDouble();
    return qMax(0.0, roundToCents((size != 0.0) ? size * price : price));   // a negative size too
}

bool garmentUnmeasured(const QString &garment, const QString &sizeText)
{
    return garment.contains(QLatin1String("m2"))
           && QString(sizeText).trimmed().replace(',', '.').toDouble() <= 0.0;
}

double roundToCents(double value)
{
    const double cents = value * 100.0;
    return std::round(cents + (cents >= 0.0 ? 1e-6 : -1e-6)) / 100.0;
}

QString moneyText(double value)
{
    return QString::number(roundToCents(value), 'f', 2);
}

QString moneyText(const QString &value)
{
    return moneyText(QString(value).trimmed().replace(',', '.').toDouble());
}

bool moneyParse(const QString &text, double *value)
{
    static const QRegularExpression rx(QStringLiteral(R"(^[+-]?\d+([.,]\d*)?$)"));
    const QString t = text.trimmed();
    if (!rx.match(t).hasMatch())
        return false;
    bool ok = false;
    const double v = QString(t).replace(',', '.').toDouble(&ok);
    if (ok && value)
        *value = v;
    return ok;
}

QString displayMoney(double value)
{
    return moneyText(value).replace(QLatin1Char('.'), QLatin1Char(','));
}

QString displayMoney(const QString &stored)
{
    return displayMoney(moneyText(stored).toDouble());
}

QString displayDecimal(const QString &stored)
{
    return QString(stored).replace(QLatin1Char('.'), QLatin1Char(','));
}

bool insertNewItemToTable(QSqlDatabase &db, const QStringList &items, const QString &table)
{
    if (dbNotConfigured(db, __func__)) return false;

    // Whitelisted tables with explicit columns: an added column cannot shift the values.
    static const QHash<QString, QStringList> kColumns = {
        {QStringLiteral("clientes"),    {"nombre", "tel_fijo", "movil", "direccion"}},
        {QStringLiteral("prendas"),     {"nombre", "precio_limpieza", "precio_plancha"}},
        {QStringLiteral("proveedores"), {"nombre", "cif", "direccion", "telefono"}},
        {QStringLiteral("servicios"),   {"nombre"}},
    };
    const QStringList columns = kColumns.value(table);
    if (columns.isEmpty() || columns.count() != items.count()) {
        qWarning() << "insertNewItemToTable: unknown table or wrong value count for" << table << "-" << items.count() << "values";
        return false;
    }

    QStringList placeholders;
    for (int i = 0; i < items.count(); ++i)
        placeholders << QStringLiteral("?");

    qDebug() << "insertNewItemToTable: INSERT INTO" << table << "(" << items.count() << "values )";
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("INSERT INTO " + table + " (" + columns.join(", ") + ") VALUES (" + placeholders.join(", ") + ")");
    for (const QString &item : items)
        q.addBindValue(notNull(item));
    const bool ok = q.exec();
    if (!ok)
        qWarning() << "insertNewItemToTable: insert into" << table << "failed -" << q.lastError().text();
    return ok;
}

bool insertExpenseInvoice(QSqlDatabase &db, const QString &nFactura, const QString &servicio,
                          const QString &descripcion, const QString &empresa, const QDate &date,
                          const QString &iva, const QString &importe)
{
    if (dbNotConfigured(db, __func__)) return false;

    ConnectionScope scope(db);
    if (!scope.isOpen())
        return false;
    QSqlQuery q(db);
    // The one-row subquery carries the next id and the date, so the closed-quarter test
    // filters the row to insert (an aggregate over gastos itself would always yield one).
    q.prepare("INSERT INTO gastos (id, n_factura, servicio, descripcion, empresa, fecha, iva, importe) "
              "SELECT g.id, :n_factura, :servicio, :descripcion, :empresa, g.fecha, :iva, :importe FROM "
              "(SELECT COALESCE(MAX(CAST(id AS INTEGER)), 0) + 1 AS id, :fecha AS fecha FROM gastos) AS g "
              "WHERE NOT " + closedQuarterSql(QStringLiteral("g.fecha")));
    q.bindValue(":n_factura", nFactura);
    q.bindValue(":servicio", servicio);
    q.bindValue(":descripcion", descripcion);
    q.bindValue(":empresa", empresa);
    q.bindValue(":fecha", date.toString("dd-MM-yyyy"));
    q.bindValue(":iva", iva);
    q.bindValue(":importe", moneyText(importe));
    if (!q.exec()) {
        qWarning() << "insertExpenseInvoice: insert failed for" << nFactura << "-" << q.lastError().text();
        return false;
    }
    if (q.numRowsAffected() != 1) {
        qWarning() << "insertExpenseInvoice: refused for" << nFactura << "- its quarter is closed";
        return false;
    }
    return true;
}

QString verifactuInvoiceId(const QString &nRecibo, int seq)
{
    return seq == 0 ? nRecibo : QStringLiteral("%1-%2").arg(nRecibo).arg(seq);
}

QString verifactuDisplayInvoiceId(const QStringList &invoiceIds, const QString &fallback)
{
    for (const QString &id : invoiceIds) {
        if (!id.isEmpty())
            return id;
    }
    return fallback;
}

int reconcileVerifactuFromAeat(QSqlDatabase &db, const QString &nRecibo, int seq,
                               const QString &csv, const QString &validationUrl)
{
    if (dbNotConfigured(db, __func__)) return 0;
    if (csv.isEmpty()) {
        qWarning() << "reconcileVerifactuFromAeat: refusing to reconcile"
                   << verifactuInvoiceId(nRecibo, seq) << "without a CSV";
        return 0;
    }

    ConnectionScope scope(db);
    QSqlQuery q(db);
    // Only a row that is paid and NOT already settled may be reconciled. The
    // estado guard is the important one: ENVIADA already has its own CSV, and
    // ANULADA / RECTIFICADA were deliberately superseded - re-stamping either
    // from a query would silently revive a cancelled invoice.
    q.prepare("UPDATE ingresos SET verifactu_estado = :estado, verifactu_csv = :csv, "
              "verifactu_url_qr = :url, verifactu_timestamp = :ts, "
              "verifactu_error = '', verifactu_invoice_id = :id "
              "WHERE n_recibo = :n AND verifactu_invoice_seq = :seq AND pagado = 'SI' "
              "  AND COALESCE(verifactu_estado, '') NOT IN ('ENVIADA', 'ANULADA', 'RECTIFICADA')");
    q.bindValue(":estado", verifactuEstadoToString(VerifactuEstado::Enviada));
    q.bindValue(":csv",    csv);
    q.bindValue(":url",    validationUrl);
    q.bindValue(":ts",     QDateTime::currentDateTime().toString(Qt::ISODate));
    q.bindValue(":id",     verifactuInvoiceId(nRecibo, seq));
    q.bindValue(":n",      nRecibo);
    q.bindValue(":seq",    seq);
    if (!q.exec()) {
        qWarning() << "reconcileVerifactuFromAeat: UPDATE failed for"
                   << verifactuInvoiceId(nRecibo, seq) << "-" << q.lastError().text();
        return 0;
    }
    const int rows = q.numRowsAffected();
    qDebug() << "reconcileVerifactuFromAeat: reconciled" << rows << "row(s) of"
             << verifactuInvoiceId(nRecibo, seq) << "from AEAT, CSV:" << csv;
    return rows;
}

PendingVerifactuEvent verifactuEventFor(QSqlDatabase &db, const QString &nRecibo, int seq)
{
    PendingVerifactuEvent e;
    if (dbNotConfigured(db, __func__)) return e;
    ConnectionScope scope(db);
    if (!scope.isOpen()) {
        qWarning() << "verifactuEventFor: db.open() failed -" << db.lastError().text();
        return e;
    }
    // Restricted to pagado='SI': a retry re-submits ONE payment event, so it must
    // carry that event's own total, not the whole ticket (the unpaid remainder was
    // never part of the invoice). fecha_pago is the date AEAT keyed the original
    // submission on - reusing it is what makes a duplicate register as a duplicate
    // instead of silently creating a second invoice.
    QSqlQuery q(db);
    q.prepare("SELECT " + kEarliestFechaPago + ", MIN(cliente), SUM(" INGRESOS_IMPORTE_VALUE "), COUNT(*) "
              "FROM ingresos "
              "WHERE n_recibo = :n AND verifactu_invoice_seq = :seq AND pagado = 'SI'");
    q.bindValue(":n",   nRecibo);
    q.bindValue(":seq", seq);
    if (q.exec() && q.next() && q.value(3).toInt() > 0) {
        e.nRecibo   = nRecibo;
        e.seq       = seq;
        e.fechaPago = q.value(0).toString();
        e.cliente   = q.value(1).toString();
        e.importe   = roundToCents(q.value(2).toDouble());
    }
    return e;
}

bool submittedInvoiceEvents(QSqlDatabase &db, const QString &nRecibo,
                            QVector<SubmittedInvoiceEvent> &events)
{
    events.clear();
    if (dbNotConfigured(db, __func__)) return false;
    ConnectionScope scope(db);
    if (!scope.isOpen()) {
        qWarning() << "submittedInvoiceEvents: db.open() failed -" << db.lastError().text();
        return false;
    }
    // Rows of one event share its CSV, estado and invoice id, so MAX picks them. The
    // date is over every paid row of the seq, as verifactuEventFor (retry, query) takes it.
    QSqlQuery q(db);
    q.prepare("SELECT verifactu_invoice_seq, COALESCE(MAX(verifactu_invoice_id), ''), SUM(" INGRESOS_IMPORTE_VALUE "), "
              "       COALESCE(MAX(verifactu_csv), ''), COALESCE(MAX(verifactu_estado), ''), "
              "       COALESCE((SELECT " + kEarliestFechaPago + " FROM ingresos s "
              "                 WHERE s.n_recibo = ingresos.n_recibo AND s.pagado = 'SI' "
              "                   AND s.verifactu_invoice_seq = ingresos.verifactu_invoice_seq), '') "
              "FROM ingresos "
              "WHERE n_recibo = :n AND pagado = 'SI' "
              "  AND verifactu_estado IS NOT NULL AND verifactu_estado != '' "
              "GROUP BY verifactu_invoice_seq "
              "ORDER BY verifactu_invoice_seq");
    q.bindValue(":n", nRecibo);
    if (!q.exec()) {
        qWarning() << "submittedInvoiceEvents: SELECT failed for ticket" << nRecibo
                   << "-" << q.lastError().text();
        return false;
    }
    while (q.next()) {
        SubmittedInvoiceEvent e;
        e.seq       = q.value(0).toInt();
        e.invoiceId = q.value(1).toString();
        e.importe   = roundToCents(q.value(2).toDouble());
        e.csv       = q.value(3).toString();
        e.estado    = q.value(4).toString();
        e.fechaPago = q.value(5).toString();
        // Legacy 8.0-8.4 rows and pre-Phase-G PayDialog rows have no stored id.
        if (e.invoiceId.isEmpty())
            e.invoiceId = verifactuInvoiceId(nRecibo, e.seq);
        events.append(e);
    }
    return true;
}

QVector<PendingVerifactuEvent> pendingVerifactuEvents(QSqlDatabase &db, const QString &floorIso)
{
    QVector<PendingVerifactuEvent> events;
    if (dbNotConfigured(db, __func__)) return events;
    ConnectionScope scope(db);
    if (!scope.isOpen()) {
        qWarning() << "pendingVerifactuEvents: db.open() failed -" << db.lastError().text();
        return events;
    }

    // One entry per (n_recibo, verifactu_invoice_seq) whose ANY row is still
    // PENDIENTE. Aggregate uses MIN over fecha_pago/client (constants across the
    // rows of one event) and SUM(importe) for that event's own total. The estado
    // filter covers legacy empty strings and the canonical "PENDIENTE".
    //
    // The payment gate is belt-and-braces since the SIN COBRAR split (10.9): an
    // unpaid row now carries its own estado and the filter above already drops it.
    // Kept because it also covers legacy NULL/'' rows, which the backfill leaves
    // alone on purpose (issue #43).
    //
    // Grouping by seq (not only n_recibo) is what makes partial-pay recovery
    // possible: a PayDialog event (seq>0, InvoiceID "<n_recibo>-<seq>") left
    // PENDIENTE by a timeout surfaces as its own row and is re-submitted with
    // its own SUM(importe) under the right InvoiceID, instead of being excluded
    // (the old query filtered verifactu_invoice_seq = 0).
    //
    // fechaPago = the date the invoice was/should be submitted under. AEAT keys
    // an invoice on (emisor, InvoiceID, FechaExpedicionFactura), so a retry MUST
    // reuse the original submission date or AEAT treats it as a new invoice and
    // the duplicate-InvoiceID guard never fires. That date is fecha_pago: a
    // PayDialog event (seq>0) submitted with its payment date, a save-time event
    // (seq 0) submitted with the reception date which equals fecha_pago for a
    // paid-at-save row. A row with no fecha_pago (unpaid) yields an invalid date
    // the dialog refuses to retry - correct, it has no AEAT invoice to recover.
    //
    // The recovery FLOOR gates on the invoice's own date (its earliest fecha_pago), like
    // refusedInvoices: a legacy ticket paid before Verifactu is left out, while a ticket
    // received before the floor and paid after it is an invoice and must be listed.
    QSqlQuery q(db);
    q.prepare(
        "SELECT n_recibo, verifactu_invoice_seq, " + kEarliestFechaPago + ", "
        "       MIN(cliente), SUM(" INGRESOS_IMPORTE_VALUE ") "
        "FROM ingresos "
        "WHERE (verifactu_estado IS NULL OR verifactu_estado = '' "
        "       OR verifactu_estado = 'PENDIENTE') "
        "  AND pagado = 'SI' "
        "  AND fecha_pago IS NOT NULL AND fecha_pago != '' "
        "  AND COALESCE(verifactu_rectifies_n_recibo, '') = '' "      // never resent as a plain invoice
        "GROUP BY n_recibo, verifactu_invoice_seq "
        "HAVING date(:floor) IS NULL OR MIN(" + kFechaPagoIso + ") IS NULL "
        "    OR MIN(" + kFechaPagoIso + ") >= date(:floor2) "
        "ORDER BY " N_RECIBO_ORDER " DESC, verifactu_invoice_seq");
    q.bindValue(":floor", floorIso);
    q.bindValue(":floor2", floorIso);
    if (!q.exec()) {
        qWarning() << "pendingVerifactuEvents: SELECT failed -" << q.lastError().text();
        return events;
    }
    while (q.next()) {
        PendingVerifactuEvent e;
        e.nRecibo   = q.value(0).toString();
        e.seq       = q.value(1).toInt();
        e.fechaPago = q.value(2).toString();
        e.cliente   = q.value(3).toString();
        e.importe   = roundToCents(q.value(4).toDouble());
        events.append(e);
    }
    return events;
}

bool aeatExportRecords(QSqlDatabase &db, const QDate &from, const QDate &to,
                       QVector<AeatExportRecord> &records, int *undatedEvents)
{
    records.clear();
    if (undatedEvents)
        *undatedEvents = 0;
    if (dbNotConfigured(db, __func__)) return false;
    ConnectionScope scope(db);
    if (!scope.isOpen()) {
        qWarning() << "aeatExportRecords: db.open() failed -" << db.lastError().text();
        return false;
    }
    // Rows of one event share its dates, CSV, payload and ids, so MAX picks them.
    // Grouping also by CSV keeps two submissions apart should old data ever share a seq.
    QSqlQuery q(db);
    q.prepare("SELECT n_recibo, verifactu_invoice_seq, " + kEarliestFechaPago + ", SUM(" INGRESOS_IMPORTE_VALUE "), "
              "       COALESCE(MAX(verifactu_invoice_id), ''), COALESCE(verifactu_csv, ''), "
              "       COALESCE(MAX(verifactu_estado), ''), COALESCE(" + kLatestFechaAnulacion + ", ''), "
              "       COALESCE(MAX(verifactu_rectifies_n_recibo), ''), "
              "       COALESCE(MAX(verifactu_rectification_type), ''), COALESCE(MAX(verifactu_xml), ''), "
              "       COALESCE(MAX(verifactu_cancel_xml), '') "
              "FROM ingresos "
              "WHERE pagado = 'SI' "
              "  AND ((verifactu_xml IS NOT NULL AND verifactu_xml != '') "
              "       OR (verifactu_csv IS NOT NULL AND verifactu_csv != '')) "
              "GROUP BY n_recibo, verifactu_invoice_seq, COALESCE(verifactu_csv, '') "
              // On the invoice's own dates, not row by row: a legacy invoice paid over
              // several days is one record, in the period it was issued.
              "HAVING (MIN(" + kFechaPagoIso + ") >= date(:from) AND MIN(" + kFechaPagoIso + ") <= date(:to)) "
              "    OR (MAX(" + kFechaAnulacionIso + ") >= date(:from2) AND MAX(" + kFechaAnulacionIso + ") <= date(:to2)) "
              "ORDER BY MIN(" + kFechaPagoIso + "), " N_RECIBO_ORDER ", verifactu_invoice_seq");
    q.bindValue(":from",  from.toString(Qt::ISODate));
    q.bindValue(":to",    to.toString(Qt::ISODate));
    q.bindValue(":from2", from.toString(Qt::ISODate));
    q.bindValue(":to2",   to.toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "aeatExportRecords: SELECT failed -" << q.lastError().text();
        return false;
    }
    while (q.next()) {
        AeatExportRecord r;
        r.nRecibo           = q.value(0).toString();
        r.seq               = q.value(1).toInt();
        r.fechaPago         = q.value(2).toString();
        r.importe           = roundToCents(q.value(3).toDouble());
        r.invoiceId         = q.value(4).toString();
        r.csv               = q.value(5).toString();
        r.estado            = q.value(6).toString();
        r.fechaAnulacion    = q.value(7).toString();
        r.rectifiesNRecibo  = q.value(8).toString();
        r.rectificationType = q.value(9).toString();
        r.xml               = q.value(10).toString();
        r.cancelXml         = q.value(11).toString();
        // Legacy rows predate the stored id; they were sent under the same rule.
        if (r.invoiceId.isEmpty())
            r.invoiceId = verifactuInvoiceId(r.nRecibo, r.seq);
        records.append(r);
    }
    if (undatedEvents) {
        // No readable payment date puts an invoice in no period at all; no writer
        // produces one, so it can only be damaged or hand-edited data.
        if (q.exec("SELECT COUNT(*) FROM (SELECT 1 FROM ingresos "
                   "  WHERE pagado = 'SI' "
                   "    AND ((verifactu_xml IS NOT NULL AND verifactu_xml != '') "
                   "         OR (verifactu_csv IS NOT NULL AND verifactu_csv != '')) "
                   "  GROUP BY n_recibo, verifactu_invoice_seq, COALESCE(verifactu_csv, '') "
                   "  HAVING MIN(" + kFechaPagoIso + ") IS NULL)") && q.first())
            *undatedEvents = q.value(0).toInt();
        else
            qWarning() << "aeatExportRecords: undated count failed -" << q.lastError().text();
        if (*undatedEvents > 0)
            qWarning() << "aeatExportRecords:" << *undatedEvents
                       << "AEAT invoice(s) without a readable fecha_pago left out of every period";
    }
    return true;
}


int updateTicketVerifactuFields(QSqlDatabase &db, const QString &ticketNum,
                                const VerifactuResult &result, int seq)
{
    if (dbNotConfigured(db, __func__)) return -1;

    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    const VerifactuEstado estadoEnum = verifactuEstadoForResult(result.status);
    const QString estado    = verifactuEstadoToString(estadoEnum);
    // Unknown outcome (timeout / transport failure): keep the InvoiceID so a
    // retry reuses the identity AEAT may already hold, and keep the error text
    // as a diagnostic. Only a definitive rejection clears the identity.
    const bool outcomeUnknown = estadoEnum == VerifactuEstado::NotSubmitted;
    // seq=0 = save-time / retry submit (bare n_recibo); seq>0 = PayDialog event
    // (<n_recibo>-<seq>). The WHERE clause always scopes by seq so a retry of
    // save-time never clobbers PayDialog rows.
    //
    // pagado='SI' is load-bearing, not defensive: the FIRST partial payment of an
    // unpaid ticket gets seq 0 (nextVerifactuInvoiceSeq counts paid rows, of which
    // there are none yet), and the still-unpaid siblings are also seq 0 - so
    // scoping by seq alone would stamp them ENVIADA + CSV for an invoice that only
    // covered the paid subset. An AEAT result can only ever belong to a paid row.
    const QString invoiceId = verifactuInvoiceId(ticketNum, seq);
    qDebug() << "updateTicketVerifactuFields: ticket" << ticketNum << "seq=" << seq
             << "estado=" << estado
             << "csv=" << (result.isSuccess() ? result.csv : QString())
             << "xml_len=" << (result.isSuccess() ? result.rawXml.size() : 0)
             << "error=" << (result.isSuccess() ? QString() : result.errorDescription);
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET verifactu_csv = :csv, verifactu_timestamp = :ts, "
              "verifactu_estado = :estado, verifactu_error = :error, verifactu_url_qr = :url, "
              "verifactu_xml = :xml, verifactu_hash = :hash, verifactu_invoice_id = :id "
              "WHERE n_recibo = :n_recibo AND verifactu_invoice_seq = :seq "
              "  AND pagado = 'SI' "
              // A settled invoice is never rewritten by a later reply (e.g. a duplicate
              // rejection), or its CSV / payload / Huella would be lost.
              "  AND COALESCE(verifactu_estado, '') NOT IN ('ENVIADA', 'ANULADA', 'RECTIFICADA')");
    if (result.isSuccess()) {
        q.bindValue(":csv",    result.csv);
        q.bindValue(":ts",     timestamp);
        q.bindValue(":estado", estado);
        q.bindValue(":error",  "");
        q.bindValue(":url",    result.validationUrl);
        q.bindValue(":xml",    result.rawXml);
        q.bindValue(":hash",   result.rawHash);
        q.bindValue(":id",     invoiceId);
    } else {
        q.bindValue(":csv",    "");
        q.bindValue(":ts",     timestamp);
        q.bindValue(":estado", estado);
        q.bindValue(":error",  result.errorDescription);
        q.bindValue(":url",    "");
        q.bindValue(":xml",    "");
        q.bindValue(":hash",   "");
        q.bindValue(":id",     outcomeUnknown ? invoiceId : QString());
    }
    q.bindValue(":n_recibo", ticketNum);
    q.bindValue(":seq",      seq);
    int changed = -1;
    if (!q.exec())
        qWarning() << "updateTicketVerifactuFields UPDATE failed for ticket" << ticketNum
                   << "seq" << seq << "-" << q.lastError().text();
    else if ((changed = q.numRowsAffected()) == 0)
        qDebug() << "updateTicketVerifactuFields: no row changed for" << invoiceId
                 << "- already settled (ENVIADA / ANULADA / RECTIFICADA) or not paid";
    return changed;
}

int nextVerifactuInvoiceSeq(QSqlDatabase &db, const QString &ticketNum, const QString &aeatEnvironment)
{
    if (dbNotConfigured(db, __func__)) return 0;
    int next = 0;
    ConnectionScope scope(db);
    QSqlQuery q(db);
    // Count paid rows so a local-only event (Verifactu disabled, estado='')
    // still increments seq for the next event - otherwise two disabled-AEAT
    // partial pays would both land on seq=0 and collide.
    q.prepare("SELECT COALESCE(MAX(verifactu_invoice_seq), -1) + 1 FROM ingresos "
              "WHERE n_recibo = :n AND pagado = 'SI'");
    q.bindValue(":n", ticketNum);
    if (q.exec() && q.first())
        next = q.value(0).toInt();
    else if (q.lastError().isValid())
        qWarning() << "nextVerifactuInvoiceSeq: SELECT failed for" << ticketNum
                   << "-" << q.lastError().text();
    // Nor a number the direct client already generated a record for (a payment whose
    // garments could not be stored as paid left no paid row behind): an InvoiceID names
    // one invoice. The table exists only once the direct connection was used.
    for (const QString &id : aeatInvoiceNumbersOfTicket(db, ticketNum,
                                                       aeatEnvironment.isNull() ? QStringLiteral("") : aeatEnvironment)) {
        bool numeric = true;
        const int seq = id == ticketNum ? 0 : id.mid(ticketNum.size() + 1).toInt(&numeric);
        if (numeric)
            next = qMax(next, seq + 1);
    }
    return next;
}

QString genHash16()
{
    // QUuid::Id128 is the 32-char hex form without braces/dashes. Truncating
    // to 16 leaves 64 bits of cryptographic entropy - 2^32 row birthday-
    // collision is around one chance in 4 billion, far below any plausible
    // ingresos size. Replaces a homegrown alphanum loop on QRandomGenerator
    // that had a legacy rand()-era predecessor, whose duplicate hashes
    // migrateDatabase repairs.
    return QUuid::createUuid().toString(QUuid::Id128).left(16);
}

QString removeSpecialChars(const QString &str)
{
    // NFD then narrow to Latin-1: accented letters decompose to base + combining
    // mark, and the marks (not representable in Latin-1) become '?'. Dropping
    // every '?' leaves the unaccented base letters; case is preserved.
    QString out = str.normalized(QString::NormalizationForm_D).toLatin1();
    out.remove(QChar('?'));
    return out;
}

// Bucket selectors for the detail collectors: a single period listing, or the
// four quarters of a year keyed on the dd-MM-yyyy month.
static int singleBucket(const QString &) { return 0; }
static int quarterBucket(const QString &ddMMyyyy) { return (ddMMyyyy.mid(3, 2).toInt() + 2) / 3 - 1; }

// Runs the shared income predicate over [start, end) and aggregates the rows by
// n_recibo into buckets[bucketOf(fecha_pago)], so each bucket matches
// COUNT(DISTINCT n_recibo) for its range. A ticket paid in several events within
// a bucket keeps its first payment date. A comma-decimal importe is not summed;
// it is counted in invalidAmounts so the report can flag it, like any amount that is not a plain number.
static bool readPlainAmount(const QString &text, double *value)
{
    bool ok = false;
    const double parsed = text.contains(QLatin1Char(',')) ? 0.0 : text.toDouble(&ok);
    ok = ok && std::isfinite(parsed);
    *value = ok ? parsed : 0.0;
    return ok;
}

static bool collectIncomeTickets(QSqlDatabase &db, QDate start, QDate end,
                                 QVector<IncomeTicketDetail> *buckets, int bucketCount,
                                 int (*bucketOf)(const QString &))
{
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("SELECT n_recibo, cliente, fecha_pago, importe, COALESCE(verifactu_invoice_seq, 0) FROM ingresos WHERE " + kIngresosIncomeWhere
              + " ORDER BY " + kFechaPagoIso + ", " N_RECIBO_ORDER);
    q.bindValue(":start", start.toString("yyyy-MM-dd"));
    q.bindValue(":end",   end.toString("yyyy-MM-dd"));
    if (!q.exec()) {
        qWarning() << "collectIncomeTickets: query failed -" << q.lastError().text();
        return false;
    }
    QVector<QHash<QString, int>> indexByTicket(bucketCount);
    while (q.next()) {
        const int bucket = bucketOf(q.value(2).toString());
        if (bucket < 0 || bucket >= bucketCount) continue;
        QVector<IncomeTicketDetail> &tickets = buckets[bucket];
        const QString nRecibo = q.value(0).toString();
        auto it = indexByTicket[bucket].find(nRecibo);
        if (it == indexByTicket[bucket].end()) {
            it = indexByTicket[bucket].insert(nRecibo, tickets.size());
            IncomeTicketDetail t;
            t.nRecibo   = nRecibo;
            t.cliente   = q.value(1).toString();
            t.fechaPago = q.value(2).toString();
            tickets.append(t);
        }
        IncomeTicketDetail &t = tickets[it.value()];
        double amount = 0.0;
        if (!readPlainAmount(q.value(3).toString(), &amount)) {
            t.invalidAmounts++;
        } else {
            t.importe += amount;
            t.invoices[q.value(4).toInt()] += amount;
        }
        t.garments++;
    }
    return true;
}

// Every gastos row in [start, end) into buckets[bucketOf(fecha)]. A NULL iva is
// kept as -1 so it is not mistaken for sin IVA (0).
static bool collectExpenses(QSqlDatabase &db, QDate start, QDate end,
                            QVector<ExpenseDetail> *buckets, int bucketCount,
                            int (*bucketOf)(const QString &))
{
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("SELECT n_factura, empresa, servicio, fecha, iva, importe FROM gastos WHERE " + kGastosPeriodWhere
              + " ORDER BY " + kFechaGastoIso + ", id");
    q.bindValue(":start", start.toString("yyyy-MM-dd"));
    q.bindValue(":end",   end.toString("yyyy-MM-dd"));
    if (!q.exec()) {
        qWarning() << "collectExpenses: query failed -" << q.lastError().text();
        return false;
    }
    while (q.next()) {
        const int bucket = bucketOf(q.value(3).toString());
        if (bucket < 0 || bucket >= bucketCount) continue;
        ExpenseDetail e;
        e.nFactura = q.value(0).toString();
        e.empresa  = q.value(1).toString();
        e.servicio = q.value(2).toString();
        e.fecha    = q.value(3).toString();
        e.iva      = expenseIvaRate(q.value(4));
        e.invalidAmount = !readPlainAmount(q.value(5).toString(), &e.importe);
        buckets[bucket].append(e);
    }
    return true;
}

// Regularised rows in [start, end) of their fecha_anulacion, aggregated by n_recibo
// into buckets[bucketOf(fecha_anulacion)].
static bool collectRegularizations(QSqlDatabase &db, QDate start, QDate end,
                                   QVector<RegularizationDetail> *buckets, int bucketCount,
                                   int (*bucketOf)(const QString &))
{
    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("SELECT n_recibo, cliente, fecha_pago, fecha_anulacion, verifactu_estado, importe, "
              "COALESCE(verifactu_invoice_seq, 0) FROM ingresos WHERE " + kRegularizationWhere
              + " ORDER BY " + kFechaAnulacionIso + ", " N_RECIBO_ORDER);
    q.bindValue(":start", start.toString("yyyy-MM-dd"));
    q.bindValue(":end",   end.toString("yyyy-MM-dd"));
    if (!q.exec()) {
        qWarning() << "collectRegularizations: query failed -" << q.lastError().text();
        return false;
    }
    QVector<QHash<QString, int>> indexByTicket(bucketCount);
    while (q.next()) {
        const int bucket = bucketOf(q.value(3).toString());
        if (bucket < 0 || bucket >= bucketCount) continue;
        QVector<RegularizationDetail> &regs = buckets[bucket];
        const QString nRecibo = q.value(0).toString();
        auto it = indexByTicket[bucket].find(nRecibo);
        if (it == indexByTicket[bucket].end()) {
            it = indexByTicket[bucket].insert(nRecibo, regs.size());
            RegularizationDetail r;
            r.nRecibo         = nRecibo;
            r.cliente         = q.value(1).toString();
            r.fechaPago       = q.value(2).toString();
            r.fechaAnulacion  = q.value(3).toString();
            r.verifactuEstado = q.value(4).toString();
            regs.append(r);
        }
        RegularizationDetail &r = regs[it.value()];
        // Several cancelled payment events merge into one entry: keep the LATEST payment,
        // which is in this period exactly when any of them is (they precede the cancellation).
        const QDate rowPago = QDate::fromString(q.value(2).toString(), "dd-MM-yyyy");
        if (rowPago.isValid() && rowPago > QDate::fromString(r.fechaPago, "dd-MM-yyyy"))
            r.fechaPago = q.value(2).toString();
        double amount = 0.0;
        if (!readPlainAmount(q.value(5).toString(), &amount)) {
            r.invalidAmounts++;
        } else {
            r.importe += amount;
            r.invoices[q.value(6).toInt()] += amount;
        }
        r.garments++;
    }
    return true;
}

QVector<IncomeTicketDetail> incomeTicketsBetweenDates(QSqlDatabase &db, QDate startDate, QDate endDate, bool *ok)
{
    QVector<IncomeTicketDetail> tickets;
    const bool read = !dbNotConfigured(db, __func__)
                      && collectIncomeTickets(db, startDate, endDate, &tickets, 1, singleBucket);
    if (ok) *ok = read;
    return tickets;
}

QVector<ExpenseDetail> expensesBetweenDates(QSqlDatabase &db, QDate startDate, QDate endDate, bool *ok)
{
    QVector<ExpenseDetail> expenses;
    const bool read = !dbNotConfigured(db, __func__)
                      && collectExpenses(db, startDate, endDate, &expenses, 1, singleBucket);
    if (ok) *ok = read;
    return expenses;
}

QVector<RegularizationDetail> regularizationsBetweenDates(QSqlDatabase &db, QDate startDate, QDate endDate, bool *ok)
{
    QVector<RegularizationDetail> regs;
    const bool read = !dbNotConfigured(db, __func__)
                      && collectRegularizations(db, startDate, endDate, &regs, 1, singleBucket);
    if (ok) *ok = read;
    return regs;
}

QuarterlyDetails annualDetailsByQuarter(QSqlDatabase &db, int year, bool *ok)
{
    QuarterlyDetails d;
    const QDate start(year, 1, 1), end(year + 1, 1, 1);
    const bool read = !dbNotConfigured(db, __func__)
                      && collectIncomeTickets(db, start, end, d.income, 4, quarterBucket)
                      && collectExpenses(db, start, end, d.expenses, 4, quarterBucket)
                      && collectRegularizations(db, start, end, d.regularizations, 4, quarterBucket);
    if (ok) *ok = read;
    return d;
}

bool quarterIsClosed(QSqlDatabase &db, QDate date)
{
    return date.isValid() && quarterIsClosed(db, (date.month() + 2) / 3, date.year());
}

QDate openPeriodDate(QSqlDatabase &db, QDate preferred)
{
    if (preferred.isValid() && !quarterIsClosed(db, preferred))
        return preferred;
    QDate day = QDate::currentDate();
    for (int quarters = 0; quarters < 8 && quarterIsClosed(db, day); ++quarters)
        day = QDate(day.year(), ((day.month() - 1) / 3) * 3 + 1, 1).addMonths(3);
    if (quarterIsClosed(db, day))
        qWarning() << "openPeriodDate: no open quarter in two years from today - using" << day;
    return day;
}

QDate ticketLastPaymentDate(QSqlDatabase &db, const QString &nRecibo)
{
    if (dbNotConfigured(db, __func__)) return QDate();

    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("SELECT MAX(" + kFechaPagoIso + ") FROM ingresos WHERE n_recibo = :n AND pagado = 'SI'");
    q.bindValue(":n", nRecibo);
    QDate last;
    if (q.exec() && q.first())
        last = QDate::fromString(q.value(0).toString(), Qt::ISODate);
    else
        qWarning() << "ticketLastPaymentDate: query failed -" << q.lastError().text();
    return last;
}

bool markInvoiceSeqCancelled(QSqlDatabase &db, const QString &nRecibo, int seq, QDate cancelDate,
                             const QString &cancelXml)
{
    if (dbNotConfigured(db, __func__)) return false;

    ConnectionScope scope(db);
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET verifactu_estado = :estado, "
              "fecha_anulacion = COALESCE(NULLIF(fecha_anulacion, ''), :fecha), "
              "verifactu_cancel_xml = COALESCE(NULLIF(:cxml, ''), verifactu_cancel_xml) "
              "WHERE n_recibo = :num AND verifactu_invoice_seq = :seq AND pagado = 'SI'");
    q.bindValue(":estado", verifactuEstadoToString(VerifactuEstado::Anulada));
    q.bindValue(":fecha",  cancelDate.toString("dd-MM-yyyy"));
    q.bindValue(":cxml",   cancelXml);
    q.bindValue(":num",    nRecibo);
    q.bindValue(":seq",    seq);
    const bool ok = q.exec() && q.numRowsAffected() > 0;   // no paid row matched = nothing marked
    if (!ok)
        qWarning() << "markInvoiceSeqCancelled: UPDATE failed or matched no paid row for ticket" << nRecibo
                   << "seq" << seq << "-" << q.lastError().text();
    return ok;
}

bool markTicketRectified(QSqlDatabase &db, const QString &nRecibo, QDate rectificationDate)
{
    if (dbNotConfigured(db, __func__)) return false;

    ConnectionScope scope(db);
    QSqlQuery q(db);
    // Never over a cancelled or already rectified invoice (a late acceptance).
    q.prepare("UPDATE ingresos SET verifactu_estado = :estado, "
              "fecha_anulacion = COALESCE(NULLIF(fecha_anulacion, ''), :fecha) "
              "WHERE n_recibo = :num AND pagado = 'SI' "
              "AND COALESCE(verifactu_estado, '') NOT IN ('ANULADA', 'RECTIFICADA')");
    q.bindValue(":estado", verifactuEstadoToString(VerifactuEstado::Rectificada));
    q.bindValue(":fecha",  rectificationDate.toString("dd-MM-yyyy"));
    q.bindValue(":num",    nRecibo);
    const bool ok = q.exec() && q.numRowsAffected() > 0;   // no paid row matched = nothing marked
    if (!ok)
        qWarning() << "markTicketRectified: UPDATE failed or matched no paid row for ticket" << nRecibo << "-" << q.lastError().text();
    return ok;
}
