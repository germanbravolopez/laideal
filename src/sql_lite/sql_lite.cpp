#include "sql_lite.h"

#include "../verifactu/verifactutypes.h"

#include <QDateTime>
#include <QHash>
#include <QDebug>
#include <QMessageBox>
#include <QUuid>
#include <QSqlError>
#include <QSqlQuery>

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

// The shop's ingresos.edit_lock is NOT NULL without a default, so every INSERT still
// writes the legacy lock flag as '0'; it is never read (accounting_closures holds the lock).

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
static void migrateAccountingClosures(QSqlDatabase &db)
{
    QSqlQuery q(db);
    const bool exists = q.exec("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'accounting_closures'")
                        && q.next();
    q.finish();
    if (exists) {
        for (const char *sql : kAccountingClosuresSchema)   // index / triggers of an older build
            if (!q.exec(QString::fromLatin1(sql)))
                qWarning() << "migrateAccountingClosures: schema statement failed -" << q.lastError().text();
        return;
    }
    if (!db.transaction()) {
        qCritical() << "migrateAccountingClosures: could not start a transaction -" << db.lastError().text();
        return;
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
    } else {
        // Without the table every quarter reads as open: say it loudly, retried at the next start.
        qCritical() << "migrateAccountingClosures: migration failed, rolled back -" << q.lastError().text()
                    << db.lastError().text();
        db.rollback();
    }
}

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------

void migrateDatabase(QSqlDatabase &db)
{
    if (dbNotConfigured(db, __func__)) return;
    qDebug() << "migrateDatabase: ensuring verifactu_* columns exist on ingresos (idempotent ALTER TABLE)";
    db.open();
    QSqlQuery q(db);
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

    // Before kAmountEditableWhere is used below: it reads accounting_closures.
    migrateAccountingClosures(db);

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
        qWarning() << "migrateDatabase: SIN COBRAR backfill failed -" << q.lastError().text();
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
                "  AND (fecha_anulacion IS NULL OR fecha_anulacion = '')"))
        qWarning() << "migrateDatabase: void-date backfill failed -" << q.lastError().text();
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
                qWarning() << "migrateDatabase: ticket" << review.value(0).toString()
                           << "has unpaid garments marked RECTIFICADA - review manually before charging them";
    }
    if (!q.exec("UPDATE ingresos SET verifactu_estado = 'SIN COBRAR' "
                "WHERE (pagado IS NULL OR pagado != 'SI') "
                "  AND verifactu_estado = 'ANULADA' "
                "  AND (estado IS NULL OR estado != '" INGRESOS_ESTADO_ANULADO "')"))
        qWarning() << "migrateDatabase: unpaid-remainder repair failed -" << q.lastError().text();
    else if (q.numRowsAffected() > 0)
        qDebug() << "migrateDatabase: relabelled" << q.numRowsAffected()
                 << "unpaid garments wrongly marked ANULADA by a cancellation as SIN COBRAR";

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
            qWarning() << "migrateDatabase: split-row search failed -" << match.lastError().text();
        while (match.next()) {
            const QString ticket = match.value(1).toString();
            if (match.value(5).toInt() != 1 || match.value(6).toInt() != 0) {
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
        for (const auto &r : relinks) {
            upd.bindValue(":src", r.second);
            upd.bindValue(":dst", r.first);
            if (!upd.exec())
                qWarning() << "migrateDatabase: split-row relink failed -" << upd.lastError().text();
        }
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
            qWarning() << "migrateDatabase: duplicate-hash search failed -" << sel.lastError().text();
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
            if (sent)
                qWarning() << "migrateDatabase: ticket" << ticket << "has a row sent to AEAT whose hash"
                           << hash << "is empty or shared - review manually";
            else
                changes.append({ sel.value(0).toLongLong(), ticket, hash });
        }
        sel.finish();
        QSqlQuery upd(db);
        upd.prepare("UPDATE ingresos SET hash = :h WHERE rowid = :rid");
        for (const Change &c : changes) {
            const QString newHash = genHash16();
            upd.bindValue(":h",   newHash);
            upd.bindValue(":rid", c.rowid);
            if (upd.exec())
                qDebug() << "migrateDatabase: ticket" << c.ticket << "rowid" << c.rowid
                         << "hash" << c.oldHash << "->" << newHash;
            else
                qWarning() << "migrateDatabase: new hash for rowid" << c.rowid << "failed -" << upd.lastError().text();
        }
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
        for (int i = 0; i < rows.size(); ++i) {
            upd.bindValue(":imp", values[i]);
            upd.bindValue(":n",   rows[i].first);
            upd.bindValue(":h",   rows[i].second);
            if (!upd.exec())
                qWarning() << "migrateDatabase: rounding importe of" << rows[i].first << "failed -"
                           << upd.lastError().text();
        }
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
                qWarning() << "migrateDatabase: comma-decimal fix failed -" << q.lastError().text();
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
        qWarning() << "migrateDatabase: estado case normalisation failed -" << q.lastError().text();
    else if (q.numRowsAffected() > 0)
        qDebug() << "migrateDatabase: normalised casing on" << q.numRowsAffected()
                 << "verifactu_estado value(s)";

    db.close();
}

int readMaxValueInColumnFromTable(QSqlDatabase &db, const QString &column, const QString &table)
{
    if (dbNotConfigured(db, __func__)) return 0;

    int maxValue = 0;
    db.open();
    QSqlQuery q(db);
    // Numeric MAX: n_recibo is TEXT, and a text MAX would rank "99999" above "100000"
    // (the next ticket would repeat a number) or "9999" above "31163".
    q.exec("SELECT MAX(CAST(" + column + " AS INTEGER)) FROM " + table);
    if (q.isSelect()) {
        if (q.first())
            maxValue = q.value(0).toInt();
        else {
            qWarning() << "readMaxValueInColumnFromTable: empty result for column" << column << "in" << table;
            QMessageBox::warning(nullptr, "Error base de datos",
                                 "Búsqueda vacía al usar readMaxValueInColumnFromTable.",
                                 QMessageBox::Ok, QMessageBox::Ok);
        }
    } else {
        qCritical() << "readMaxValueInColumnFromTable: query error for column" << column << "in" << table;
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar readMaxValueInColumnFromTable.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }
    db.close();
    return maxValue;
}

int readMaxNMinYearInColumnFromTable(QSqlDatabase &db, bool maxNMin, const QString &column, const QString &table)
{
    if (dbNotConfigured(db, __func__)) return 0;

    QString query = maxNMin
        ? "SELECT MAX(substr(" + column + ",7,4)) FROM " + table
        : "SELECT MIN(substr(" + column + ",7,4)) FROM " + table;

    int value = 0;
    db.open();
    QSqlQuery q(db);
    q.exec(query);
    if (q.isSelect()) {
        if (q.first())
            value = q.value(0).toInt();
        else {
            qWarning() << "readMaxNMinYearInColumnFromTable: empty result for column" << column << "in" << table;
            QMessageBox::warning(nullptr, "Error base de datos",
                                 "Búsqueda vacía al usar readMaxNMinYearInColumnFromTable.",
                                 QMessageBox::Ok, QMessageBox::Ok);
        }
    } else {
        qCritical() << "readMaxNMinYearInColumnFromTable: query error for column" << column << "in" << table;
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar readMaxNMinYearInColumnFromTable.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }
    db.close();
    return value;
}

QStringList readColumnFromTable(QSqlDatabase &db, const QString &column, const QString &table,
                                const QString &orderByColumn)
{
    if (dbNotConfigured(db, __func__)) return {};

    QStringList list;
    db.open();
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
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar readColumnFromTable.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }
    db.close();
    return list;
}

float readGarmentPrice(QSqlDatabase &db, const QString &garment, const QString &service)
{
    if (dbNotConfigured(db, __func__)) return 0.0f;

    float price = 0.0f;
    db.open();
    QSqlQuery q(db);
    if (service == "Limp.") {
        q.prepare("SELECT precio_limpieza FROM prendas WHERE nombre LIKE :garment");
        q.bindValue(":garment", garment);
        q.exec();
    } else if (service == "Plan.") {
        q.prepare("SELECT precio_plancha FROM prendas WHERE nombre LIKE :garment");
        q.bindValue(":garment", garment);
        q.exec();
    }
    if (q.isSelect()) {
        if (q.first()) {
            if (!q.value(0).toString().contains(',')) {
                price = q.value(0).toFloat();
            } else {
                qCritical() << "readGarmentPrice: comma decimal found in prendas for garment" << garment;
                QMessageBox::critical(nullptr, "Error en la base de datos",
                                      "El precio de esta prenda está guardado con ',' como separador decimal. "
                                      "Corríjalo en el listado de prendas.",
                                      QMessageBox::Ok, QMessageBox::Ok);
                price = -1.0f;
            }
        } else {
            qWarning() << "readGarmentPrice: garment not found:" << garment << "service:" << service;
            QMessageBox::warning(nullptr, "Error base de datos",
                                 "No se ha encontrado la prenda '" + garment +
                                 "' al usar readGarmentPrice para el servicio '" + service + "'.\n"
                                 "Añadir un precio en el listado de prendas antes de continuar con esta acción.",
                                 QMessageBox::Ok, QMessageBox::Ok);
            price = -1.0f;
        }
    } else {
        qCritical() << "readGarmentPrice: query error for garment" << garment << "service" << service;
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar readGarmentPrice.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }
    db.close();
    return price;
}

QString selectFromWhereLike(QSqlDatabase &db, const QString &itemToGet, const QString &table,
                            const QString &columnToSearch, const QString &itemToSearch,
                            bool exactMatch, bool printMsg)
{
    if (dbNotConfigured(db, __func__)) return {};

    QString result;
    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT " + itemToGet + " FROM " + table + " WHERE " + columnToSearch + " LIKE :item");
    q.bindValue(":item", exactMatch ? itemToSearch : itemToSearch + "%");
    q.exec();
    if (q.isSelect()) {
        if (q.first()) {
            result = q.value(0).toString();
        } else if (printMsg) {
            qWarning() << "selectFromWhereLike: item not found:" << itemToSearch << "in column" << columnToSearch;
            QMessageBox::warning(nullptr, "Búsqueda vacía",
                                 "El elemento '" + itemToSearch + "' no se ha encontrado en la base de datos para '"
                                 + columnToSearch + "'.",
                                 QMessageBox::Ok, QMessageBox::Ok);
        }
    } else if (printMsg) {
        qCritical() << "selectFromWhereLike: query error for" << itemToSearch << "in column" << columnToSearch;
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar selectFromWhereLike.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }
    db.close();
    return result;
}

QString searchItemFromClient(QSqlDatabase &db, const QString &item, const QString &client, bool printMsg)
{
    return selectFromWhereLike(db, item, "clientes", "nombre", client, false, printMsg);
}

bool updateItemToClient(QSqlDatabase &db, const QString &column, const QString &item, const QString &client)
{
    if (dbNotConfigured(db, __func__)) return false;

    qDebug() << "updateItemToClient: UPDATE clientes SET" << column << "= ? WHERE nombre =" << client;
    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE clientes SET " + column + " = :item WHERE nombre = :client");
    q.bindValue(":item", item);
    q.bindValue(":client", client);
    bool ok = q.exec();
    if (!ok)
        qWarning() << "updateItemToClient: failed to update column" << column << "for client" << client << "-" << q.lastError().text();
    db.close();
    return ok;
}

bool addNewClient(QSqlDatabase &db, const QString &client, const QString &telFijo,
                  const QString &direccion, const QString &movil)
{
    if (dbNotConfigured(db, __func__)) return false;

    qDebug() << "addNewClient: INSERT INTO clientes nombre=" << client << "tel_fijo=" << telFijo
             << "movil=" << movil;
    db.open();
    QSqlQuery q(db);
    q.prepare("INSERT INTO clientes (nombre, tel_fijo, direccion, movil) "
              "VALUES (:nombre, :tel_fijo, :direccion, :movil)");
    q.bindValue(":nombre",    client);
    q.bindValue(":tel_fijo",  telFijo);
    q.bindValue(":direccion", direccion);
    q.bindValue(":movil",     movil);
    bool ok = q.exec();
    if (!ok)
        qWarning() << "addNewClient: failed to insert client" << client << "-" << q.lastError().text();
    db.close();
    return ok;
}

bool updateTicketPickup(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                        const QString &fechaRecogida, const QString &estado)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET fecha_recogida = :fr, estado = :est "
              "WHERE n_recibo = :n AND hash = :h");
    q.bindValue(":fr",  fechaRecogida);
    q.bindValue(":est", estado);
    q.bindValue(":n",   nRecibo);
    q.bindValue(":h",   hash);
    bool ok = q.exec();
    if (!ok)
        qWarning() << "updateTicketPickup: UPDATE failed -" << q.lastError().text();
    db.close();
    return ok;
}

bool markTicketPickedUp(QSqlDatabase &db, const QString &nRecibo, const QString &fechaRecogida)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
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
    db.close();
    return ok;
}

bool updateTicketObservations(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                              const QString &observaciones)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET observaciones = :obs "
              "WHERE n_recibo = :n AND hash = :h");
    q.bindValue(":obs", observaciones);
    q.bindValue(":n",   nRecibo);
    q.bindValue(":h",   hash);
    bool ok = q.exec();
    if (!ok)
        qWarning() << "updateTicketObservations: UPDATE failed -" << q.lastError().text();
    db.close();
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
    if (moneyText(importe).toDouble() < 0.0) {
        qWarning() << __func__ << ": refused a negative importe" << importe << "for" << nRecibo << hash;
        return false;
    }

    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET size = :sz, importe = :imp "
              "WHERE n_recibo = :n AND hash = :h" + kAmountEditableWhere);
    q.bindValue(":sz",  size);
    q.bindValue(":imp", moneyText(importe));
    q.bindValue(":n",   nRecibo);
    q.bindValue(":h",   hash);
    const bool ok = execAmountUpdate(q, __func__, nRecibo, hash);
    db.close();
    return ok;
}

bool updateGarmentQtyAndImporte(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                const QString &cantidad, const QString &importe)
{
    if (dbNotConfigured(db, __func__)) return false;
    if (moneyText(importe).toDouble() < 0.0) {
        qWarning() << __func__ << ": refused a negative importe" << importe << "for" << nRecibo << hash;
        return false;
    }

    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET cantidad = :cant, importe = :imp "
              "WHERE n_recibo = :n AND hash = :h" + kAmountEditableWhere);
    q.bindValue(":cant", cantidad);
    q.bindValue(":imp",  moneyText(importe));
    q.bindValue(":n",    nRecibo);
    q.bindValue(":h",    hash);
    const bool ok = execAmountUpdate(q, __func__, nRecibo, hash);
    db.close();
    return ok;
}

QString splitGarmentRow(QSqlDatabase &db, const QString &nRecibo, const QString &hash, int nGarm)
{
    if (dbNotConfigured(db, __func__)) return QString();

    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT cantidad, importe, " + closedQuarterSql("ingresos.fecha_pago")
              + " FROM ingresos WHERE n_recibo = :n AND hash = :h");
    q.bindValue(":n", nRecibo);
    q.bindValue(":h", hash);
    if (!q.exec() || !q.first()) {
        qWarning() << "splitGarmentRow: row not found -" << nRecibo << hash << q.lastError().text();
        db.close();
        return QString();
    }
    const int qty = q.value(0).toInt();
    if (q.value(2).toBool()) {
        qWarning() << "splitGarmentRow: row is locked by Contabilidad -" << nRecibo << hash;
        db.close();
        return QString();
    }
    if (nGarm < 1 || nGarm >= qty) {
        qWarning() << "splitGarmentRow: cannot split" << nGarm << "of" << qty << "on" << nRecibo << hash;
        db.close();
        return QString();
    }
    // The split-off part in cents; the original keeps the exact remainder (in 1/10000),
    // so the two rows add up to the stored importe and an older paid amount stored
    // with more decimals is never re-rounded.
    const double total = q.value(1).toString().replace(',', '.').toDouble();
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

    // Both writes or none: without a transaction a failed INSERT would leave the
    // original row reduced and the split-off garments lost.
    if (!db.transaction()) {
        qWarning() << "splitGarmentRow: could not start a transaction -" << db.lastError().text();
        db.close();
        return QString();
    }
    QSqlQuery u(db);
    // Same row as read above: still unlocked and with the same quantity.
    u.prepare("UPDATE ingresos SET cantidad = :cant, importe = :imp WHERE n_recibo = :n AND hash = :h "
              "AND cantidad = :oldCant" + kUnlockedWhere);
    u.bindValue(":cant", QString::number(qty - nGarm));
    u.bindValue(":imp",  money(total4 - split4));
    u.bindValue(":n",    nRecibo);
    u.bindValue(":h",    hash);
    u.bindValue(":oldCant", QString::number(qty));
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
    if (ok) {
        db.commit();
    } else {
        qWarning() << "splitGarmentRow: failed on" << nRecibo << hash << "-"
                   << u.lastError().text() << i.lastError().text();
        db.rollback();
    }
    db.close();
    return ok ? newHash : QString();
}

bool updateGarmentServiceAndImporte(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                    const QString &servicio, const QString &importe)
{
    if (dbNotConfigured(db, __func__)) return false;
    if (moneyText(importe).toDouble() < 0.0) {
        qWarning() << __func__ << ": refused a negative importe" << importe << "for" << nRecibo << hash;
        return false;
    }

    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET servicio = :serv, importe = :imp "
              "WHERE n_recibo = :n AND hash = :h" + kAmountEditableWhere);
    q.bindValue(":serv", servicio);
    q.bindValue(":imp",  moneyText(importe));
    q.bindValue(":n",    nRecibo);
    q.bindValue(":h",    hash);
    const bool ok = execAmountUpdate(q, __func__, nRecibo, hash);
    db.close();
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

    db.open();
    QSqlQuery q(db);
    // Strict pagado='SI' (not "!= NO"): a blank must not read as paid. A ticket
    // with no rows is not "all paid" either, hence the total > 0 guard - otherwise
    // an empty result would print IMPORTE PAGADO on nothing.
    q.prepare("SELECT COUNT(*), SUM(CASE WHEN pagado = 'SI' THEN 1 ELSE 0 END) "
              "FROM ingresos WHERE n_recibo = :n");
    q.bindValue(":n", nRecibo);
    bool allPaid = false;
    if (q.exec() && q.first()) {
        const int total = q.value(0).toInt();
        allPaid = total > 0 && q.value(1).toInt() == total;
    }
    db.close();
    return allPaid;
}

bool voidGarmentRow(QSqlDatabase &db, const QString &nRecibo, const QString &hash)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
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
    db.close();
    return ok;
}

bool ticketHasPaidGarment(QSqlDatabase &db, const QString &nRecibo)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM ingresos WHERE n_recibo = :n AND pagado = 'SI'");
    q.bindValue(":n", nRecibo);
    const bool paid = q.exec() && q.first() && q.value(0).toInt() > 0;
    db.close();
    return paid;
}

// Runs the INSERT on an already open db; the callers open, close and own the transaction.
static bool execGarmentInsert(QSqlDatabase &db, const IngresoGarmentRow &row)
{
    QSqlQuery q(db);
    q.prepare("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, "
              "fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, "
              "observaciones, edit_lock, hash, verifactu_estado) "
              "VALUES (:n_recibo, :cliente, :fecha_recepcion, :fecha_pago, :fecha_recogida, "
              ":importe, :pagado, :estado, :cantidad, :prenda, :size, :servicio, "
              ":observaciones, '0', :hash, :verifactu_estado)");
    q.bindValue(":n_recibo",         row.nRecibo);
    q.bindValue(":cliente",          row.cliente);
    q.bindValue(":fecha_recepcion",  row.fechaRecepcion);
    q.bindValue(":fecha_pago",       row.fechaPago);
    q.bindValue(":fecha_recogida",   row.fechaRecogida);
    q.bindValue(":importe",          moneyText(row.importe));
    q.bindValue(":pagado",           row.pagado);
    q.bindValue(":estado",           row.estado);
    q.bindValue(":cantidad",         row.cantidad);
    q.bindValue(":prenda",           row.prenda);
    q.bindValue(":size",             row.size);
    q.bindValue(":servicio",         row.servicio);
    q.bindValue(":observaciones",    row.observaciones);
    q.bindValue(":hash",             row.hash);
    q.bindValue(":verifactu_estado", row.verifactuEstado);
    const bool ok = q.exec();
    if (!ok)
        qWarning() << "insertGarmentRow: INSERT failed for" << row.nRecibo << row.hash << "-" << q.lastError().text();
    return ok;
}

bool insertGarmentRow(QSqlDatabase &db, const IngresoGarmentRow &row)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
    const bool ok = execGarmentInsert(db, row);
    db.close();
    return ok;
}

bool insertGarmentRows(QSqlDatabase &db, const QList<IngresoGarmentRow> &rows)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
    // All the garments of a ticket or none: a partial ticket would be re-saved under
    // the same number, or offered to AEAT for only part of its amount.
    if (!db.transaction()) {
        qWarning() << "insertGarmentRows: could not start a transaction -" << db.lastError().text();
        db.close();
        return false;
    }
    for (const IngresoGarmentRow &row : rows) {
        if (!execGarmentInsert(db, row)) {
            db.rollback();
            db.close();
            return false;
        }
    }
    const bool ok = db.commit();
    if (!ok) {
        qWarning() << "insertGarmentRows: commit failed -" << db.lastError().text();
        db.rollback();
    }
    db.close();
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
    // An open connection stays open: Listado asks while its table model reads through it.
    const bool wasOpen = db.isOpen();
    db.open();
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
    if (!wasOpen)
        db.close();
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
    const bool wasOpen = db.isOpen();
    db.open();
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
    if (!wasOpen)
        db.close();
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
    const bool wasOpen = db.isOpen();
    db.open();
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
    if (!wasOpen)
        db.close();
    return closures;
}

QSet<int> closedQuarterKeys(QSqlDatabase &db)
{
    QSet<int> keys;
    if (dbNotConfigured(db, __func__)) return keys;
    const bool wasOpen = db.isOpen();
    db.open();
    QSqlQuery q(db);
    if (!q.exec("SELECT year, quarter FROM accounting_closures WHERE reopened_at IS NULL"))
        qWarning() << "closedQuarterKeys: query failed -" << q.lastError().text();
    while (q.next())
        keys.insert(quarterKey(q.value(1).toInt(), q.value(0).toInt()));
    q.finish();
    if (!wasOpen)
        db.close();
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
    const bool wasOpen = db.isOpen();
    db.open();
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
    if (!wasOpen)
        db.close();
    return any;
}

QString verifactuEventRecordXml(QSqlDatabase &db, const QString &nRecibo, int seq)
{
    if (dbNotConfigured(db, __func__)) return {};
    QString xml;
    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT verifactu_xml FROM ingresos WHERE n_recibo = :n AND verifactu_invoice_seq = :s "
              "AND pagado = 'SI' AND COALESCE(verifactu_xml, '') != '' LIMIT 1");
    q.bindValue(":n", nRecibo);
    q.bindValue(":s", seq);
    if (q.exec() && q.next())
        xml = q.value(0).toString();
    db.close();
    return xml;
}

int aeatPendingRecordCount(QSqlDatabase &db, const QString &environment)
{
    if (dbNotConfigured(db, __func__)) return 0;
    int count = 0;
    db.open();
    QSqlQuery q(db);
    // The table is the direct client's; a failed query just means it was never used.
    q.prepare("SELECT COUNT(*) FROM aeat_records WHERE state = 'PENDIENTE' AND environment = :e");
    q.bindValue(":e", environment);
    if (q.exec() && q.next())
        count = q.value(0).toInt();
    db.close();
    return count;
}

// "31277" -> (31277, 0), "31277-1" -> (31277, 1).
static void splitInvoiceId(const QString &invoiceId, QString *nRecibo, int *seq)
{
    *nRecibo = invoiceId.trimmed();
    *seq = 0;
    const int dash = nRecibo->lastIndexOf(QLatin1Char('-'));
    if (dash > 0) {
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
            db.open();
            QSqlQuery q(db);
            q.prepare("SELECT MIN(verifactu_rectification_type), MIN(fecha_pago) FROM ingresos "
                      "WHERE n_recibo = :n AND pagado = 'SI'");
            q.bindValue(":n", nRecibo);
            const bool read = q.exec() && q.next();
            const QString type = read ? q.value(0).toString() : QString();
            const QDate date = read ? QDate::fromString(q.value(1).toString(), "dd-MM-yyyy") : QDate();
            q.finish();
            db.close();
            // Into an open period: a quarter closed meanwhile must not change.
            const QDate when = openPeriodDate(db, date);
            if (type == QLatin1String("S") && !markTicketRectified(db, original, when))
                qWarning() << "applySettledVerifactuResult: rectificativa" << nRecibo << "accepted, original" << original
                           << "not marked RECTIFICADA";
        }
    }
    return changed;
}

int nextTicketNumber(QSqlDatabase &db, int highestAtAeat)
{
    return qMax(readMaxValueInColumnFromTable(db, "n_recibo", "ingresos"), highestAtAeat) + 1;
}

int refusedInvoiceCount(QSqlDatabase &db)
{
    if (dbNotConfigured(db, __func__) || !db.open())
        return 0;
    QSqlQuery q(db);
    const int count = q.exec("SELECT COUNT(DISTINCT n_recibo || '-' || verifactu_invoice_seq) FROM ingresos "
                             "WHERE pagado = 'SI' AND verifactu_estado = 'ERROR'") && q.next()
                          ? q.value(0).toInt() : 0;
    q.finish();
    db.close();
    return count;
}

QStringList pendingRectificationNumbers(QSqlDatabase &db)
{
    QStringList out;
    if (dbNotConfigured(db, __func__) || !db.open())
        return out;
    QSqlQuery q(db);
    if (q.exec("SELECT DISTINCT n_recibo FROM ingresos WHERE COALESCE(verifactu_rectifies_n_recibo, '') != '' "
               "AND pagado = 'SI' AND verifactu_estado = 'PENDIENTE'"))
        while (q.next())
            out << q.value(0).toString();
    q.finish();
    db.close();
    return out;
}

bool markRectificationUnsent(QSqlDatabase &db, const QString &number, const QString &reason)
{
    if (dbNotConfigured(db, __func__) || !db.open())
        return false;
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET verifactu_estado = 'ERROR', verifactu_error = :r WHERE n_recibo = :n "
              "AND COALESCE(verifactu_rectifies_n_recibo, '') != '' AND verifactu_estado = 'PENDIENTE'");
    q.bindValue(":r", reason);
    q.bindValue(":n", number);
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (!ok)
        qWarning() << "markRectificationUnsent: rectificativa" << number << "not updated -" << q.lastError().text();
    db.close();
    return ok;
}

bool markRectificationResent(QSqlDatabase &db, const QString &number)
{
    if (dbNotConfigured(db, __func__) || !db.open())
        return false;
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET verifactu_estado = 'PENDIENTE', verifactu_error = '' WHERE n_recibo = :n "
              "AND COALESCE(verifactu_rectifies_n_recibo, '') != '' AND verifactu_estado = 'ERROR'");
    q.bindValue(":n", number);
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (!ok)
        qWarning() << "markRectificationResent: rectificativa" << number << "not updated -" << q.lastError().text();
    db.close();
    return ok;
}

QString pendingRectificationOf(QSqlDatabase &db, const QString &original)
{
    if (dbNotConfigured(db, __func__) || !db.open())
        return QString();
    QSqlQuery q(db);
    q.prepare("SELECT n_recibo FROM ingresos WHERE verifactu_rectifies_n_recibo = :o AND pagado = 'SI' "
              "AND verifactu_estado = 'PENDIENTE' LIMIT 1");
    q.bindValue(":o", original);
    const QString number = q.exec() && q.next() ? q.value(0).toString() : QString();
    q.finish();
    db.close();
    return number;
}

QString refusedRectificationOf(QSqlDatabase &db, const QString &original)
{
    if (dbNotConfigured(db, __func__) || !db.open())
        return QString();
    QSqlQuery q(db);
    q.prepare("SELECT n_recibo FROM ingresos WHERE verifactu_rectifies_n_recibo = :o AND pagado = 'SI' "
              "AND verifactu_estado = 'ERROR' ORDER BY CAST(n_recibo AS INTEGER) DESC LIMIT 1");
    q.bindValue(":o", original);
    const QString number = q.exec() && q.next() ? q.value(0).toString() : QString();
    q.finish();
    db.close();
    return number;
}

QString rectifiedTicketOf(QSqlDatabase &db, const QString &nRecibo)
{
    if (dbNotConfigured(db, __func__) || !db.open())
        return QString();
    QSqlQuery q(db);
    q.prepare("SELECT MIN(verifactu_rectifies_n_recibo) FROM ingresos "
              "WHERE n_recibo = :n AND COALESCE(verifactu_rectifies_n_recibo, '') != ''");
    q.bindValue(":n", nRecibo);
    const QString original = q.exec() && q.next() ? q.value(0).toString() : QString();
    q.finish();
    db.close();
    return original;
}

int applySettledVerifactuCancellation(QSqlDatabase &db, const QString &invoiceId, const QDate &cancelDate,
                                      const VerifactuResult &result)
{
    QString nRecibo;
    int seq = 0;
    splitInvoiceId(invoiceId, &nRecibo, &seq);
    // -1 only when the database answers that no paid row carries the InvoiceID: a read
    // failure is 0, so an accepted cancellation of a real ticket is never passed over.
    if (dbNotConfigured(db, __func__) || !db.open())
        return 0;
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM ingresos WHERE n_recibo = :n AND verifactu_invoice_seq = :seq AND pagado = 'SI'");
    q.bindValue(":n",   nRecibo);
    q.bindValue(":seq", seq);
    if (!q.exec() || !q.next()) {
        qWarning() << "applySettledVerifactuCancellation: lookup failed for" << invoiceId << "-" << q.lastError().text();
        return 0;
    }
    if (q.value(0).toInt() == 0)
        return -1;
    q.prepare("SELECT COUNT(*) FROM ingresos WHERE n_recibo = :n AND verifactu_invoice_seq = :seq "
              "AND pagado = 'SI' AND COALESCE(verifactu_estado, '') != 'ANULADA'");
    q.bindValue(":n",   nRecibo);
    q.bindValue(":seq", seq);
    if (result.isSuccess() && q.exec() && q.next() && q.value(0).toInt() == 0)
        return 2;                                                   // already marked (its dialog did)
    if (!result.isSuccess())
        return 0;
    return markInvoiceSeqCancelled(db, nRecibo, seq, cancelDate, result.rawXml) ? 1 : 0;
}

QStringList readClientPhones(QSqlDatabase &db, const QString &client)
{
    QStringList phones = { QString(), QString() }; // {tel_fijo, movil}
    if (dbNotConfigured(db, __func__)) return phones;

    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT tel_fijo, movil FROM clientes WHERE nombre = :n");
    q.bindValue(":n", client);
    if (q.exec() && q.first()) {
        phones[0] = q.value(0).toString();
        phones[1] = q.value(1).toString();
    } else if (q.lastError().isValid()) {
        qWarning() << "readClientPhones: query error for client" << client << "-" << q.lastError().text();
    }
    db.close();
    return phones;
}

double garmentImporte(const QString &quantityText, const QString &sizeText, double unitPrice)
{
    const double quantity = quantityText.trimmed().replace(',', '.').toDouble();
    // The price list is in cents; readGarmentPrice hands a float, so re-round first.
    double price = quantity * roundToCents(unitPrice);
    if (price < 0.0)
        return 0.0;
    const double size = sizeText.trimmed().replace(',', '.').toDouble();
    return roundToCents((size != 0.0) ? size * price : price);
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

bool insertNewItemToTable(QSqlDatabase &db, const QStringList &items, const QString &table)
{
    if (dbNotConfigured(db, __func__)) return false;

    // Build positional placeholders so values bind safely - never concatenate user input
    // into the SQL (a client named O'Brien would otherwise break the statement, and a
    // hostile value could close the literal and inject DDL).
    QString placeholders;
    for (int i = 0; i < items.count(); ++i)
        placeholders += (i == 0 ? "?" : ", ?");

    qDebug() << "insertNewItemToTable: INSERT INTO" << table << "VALUES (" << items.count() << "items )";
    db.open();
    QSqlQuery q(db);
    q.prepare("INSERT INTO " + table + " VALUES (" + placeholders + ")");
    for (const QString &item : items)
        q.addBindValue(item);
    const bool ok = q.exec();
    if (!ok)
        qWarning() << "insertNewItemToTable: insert into" << table << "failed -" << q.lastError().text();
    db.close();
    return ok;
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

    db.open();
    QSqlQuery q(db);
    // Only a row that is paid and NOT already settled may be reconciled. The
    // estado guard is the important one: ENVIADA already has its own CSV, and
    // ANULADA / RECTIFICADA were deliberately superseded - re-stamping either
    // from a query would silently revive a cancelled invoice.
    q.prepare("UPDATE ingresos SET verifactu_estado = :estado, verifactu_csv = :csv, "
              "verifactu_url_qr = :url, verifactu_timestamp = :ts, "
              "verifactu_error = '', verifactu_invoice_id = :id "
              "WHERE n_recibo = :n AND verifactu_invoice_seq = :seq AND pagado = 'SI' "
              "  AND verifactu_estado NOT IN ('ENVIADA', 'ANULADA', 'RECTIFICADA')");
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
        db.close();
        return 0;
    }
    const int rows = q.numRowsAffected();
    qDebug() << "reconcileVerifactuFromAeat: reconciled" << rows << "row(s) of"
             << verifactuInvoiceId(nRecibo, seq) << "from AEAT, CSV:" << csv;
    db.close();
    return rows;
}

PendingVerifactuEvent verifactuEventFor(QSqlDatabase &db, const QString &nRecibo, int seq)
{
    PendingVerifactuEvent e;
    if (dbNotConfigured(db, __func__)) return e;
    if (!db.open()) {
        qWarning() << "verifactuEventFor: db.open() failed -" << db.lastError().text();
        return e;
    }
    // Restricted to pagado='SI': a retry re-submits ONE payment event, so it must
    // carry that event's own total, not the whole ticket (the unpaid remainder was
    // never part of the invoice). fecha_pago is the date AEAT keyed the original
    // submission on - reusing it is what makes a duplicate register as a duplicate
    // instead of silently creating a second invoice.
    QSqlQuery q(db);
    q.prepare("SELECT " + kEarliestFechaPago + ", MIN(cliente), SUM(importe), COUNT(*) "
              "FROM ingresos "
              "WHERE n_recibo = :n AND verifactu_invoice_seq = :seq AND pagado = 'SI'");
    q.bindValue(":n",   nRecibo);
    q.bindValue(":seq", seq);
    if (q.exec() && q.next() && q.value(3).toInt() > 0) {
        e.nRecibo   = nRecibo;
        e.seq       = seq;
        e.fechaPago = q.value(0).toString();
        e.cliente   = q.value(1).toString();
        e.importe   = q.value(2).toDouble();
    }
    db.close();
    return e;
}

bool submittedInvoiceEvents(QSqlDatabase &db, const QString &nRecibo,
                            QVector<SubmittedInvoiceEvent> &events)
{
    events.clear();
    if (dbNotConfigured(db, __func__)) return false;
    if (!db.open()) {
        qWarning() << "submittedInvoiceEvents: db.open() failed -" << db.lastError().text();
        return false;
    }
    // Rows of one event share its CSV, estado and invoice id, so MAX picks them. The
    // date is over every paid row of the seq, as verifactuEventFor (retry, query) takes it.
    QSqlQuery q(db);
    q.prepare("SELECT verifactu_invoice_seq, COALESCE(MAX(verifactu_invoice_id), ''), SUM(importe), "
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
        db.close();
        return false;
    }
    while (q.next()) {
        SubmittedInvoiceEvent e;
        e.seq       = q.value(0).toInt();
        e.invoiceId = q.value(1).toString();
        e.importe   = q.value(2).toDouble();
        e.csv       = q.value(3).toString();
        e.estado    = q.value(4).toString();
        e.fechaPago = q.value(5).toString();
        // Legacy 8.0-8.4 rows and pre-Phase-G PayDialog rows have no stored id.
        if (e.invoiceId.isEmpty())
            e.invoiceId = verifactuInvoiceId(nRecibo, e.seq);
        events.append(e);
    }
    db.close();
    return true;
}

QVector<PendingVerifactuEvent> pendingVerifactuEvents(QSqlDatabase &db, const QString &floorIso)
{
    QVector<PendingVerifactuEvent> events;
    if (dbNotConfigured(db, __func__)) return events;
    if (!db.open()) {
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
    // (seq>0=0) submitted with the reception date which equals fecha_pago for a
    // paid-at-save row. A row with no fecha_pago (unpaid) yields an invalid date
    // the dialog refuses to retry - correct, it has no AEAT invoice to recover.
    //
    // The recovery FLOOR, however, gates on fecha_recepcion (when the ticket
    // entered the system) to exclude pre-Verifactu legacy tickets; both columns
    // store dd-MM-yyyy, substr-rebuilt to yyyy-MM-dd for lexicographic compares.
    QSqlQuery q(db);
    q.prepare(
        "SELECT n_recibo, verifactu_invoice_seq, " + kEarliestFechaPago + ", "
        "       MIN(cliente), SUM(importe) "
        "FROM ingresos "
        "WHERE (verifactu_estado IS NULL OR verifactu_estado = '' "
        "       OR verifactu_estado = 'PENDIENTE') "
        "  AND pagado = 'SI' "
        "  AND fecha_pago IS NOT NULL AND fecha_pago != '' "
        "  AND COALESCE(verifactu_rectifies_n_recibo, '') = '' "      // never resent as a plain invoice
        "  AND substr(fecha_recepcion, 7, 4) || '-' "
        "      || substr(fecha_recepcion, 4, 2) || '-' "
        "      || substr(fecha_recepcion, 1, 2) >= :floor "
        "GROUP BY n_recibo, verifactu_invoice_seq "
        "ORDER BY CAST(n_recibo AS INTEGER) DESC, verifactu_invoice_seq");
    q.bindValue(":floor", floorIso);
    if (!q.exec()) {
        qWarning() << "pendingVerifactuEvents: SELECT failed -" << q.lastError().text();
        db.close();
        return events;
    }
    while (q.next()) {
        PendingVerifactuEvent e;
        e.nRecibo   = q.value(0).toString();
        e.seq       = q.value(1).toInt();
        e.fechaPago = q.value(2).toString();
        e.cliente   = q.value(3).toString();
        e.importe   = q.value(4).toDouble();
        events.append(e);
    }
    db.close();
    return events;
}

bool aeatExportRecords(QSqlDatabase &db, const QDate &from, const QDate &to,
                       QVector<AeatExportRecord> &records, int *undatedEvents)
{
    records.clear();
    if (undatedEvents)
        *undatedEvents = 0;
    if (dbNotConfigured(db, __func__)) return false;
    if (!db.open()) {
        qWarning() << "aeatExportRecords: db.open() failed -" << db.lastError().text();
        return false;
    }
    // Rows of one event share its dates, CSV, payload and ids, so MAX picks them.
    // Grouping also by CSV keeps two submissions apart should old data ever share a seq.
    QSqlQuery q(db);
    q.prepare("SELECT n_recibo, verifactu_invoice_seq, " + kEarliestFechaPago + ", SUM(importe), "
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
              "ORDER BY MIN(" + kFechaPagoIso + "), CAST(n_recibo AS INTEGER), verifactu_invoice_seq");
    q.bindValue(":from",  from.toString(Qt::ISODate));
    q.bindValue(":to",    to.toString(Qt::ISODate));
    q.bindValue(":from2", from.toString(Qt::ISODate));
    q.bindValue(":to2",   to.toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "aeatExportRecords: SELECT failed -" << q.lastError().text();
        db.close();
        return false;
    }
    while (q.next()) {
        AeatExportRecord r;
        r.nRecibo           = q.value(0).toString();
        r.seq               = q.value(1).toInt();
        r.fechaPago         = q.value(2).toString();
        r.importe           = q.value(3).toDouble();
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
    db.close();
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
    db.open();
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
    db.close();
    return changed;
}

int nextVerifactuInvoiceSeq(QSqlDatabase &db, const QString &ticketNum, const QString &aeatEnvironment)
{
    if (dbNotConfigured(db, __func__)) return 0;
    int next = 0;
    db.open();
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
    QSqlQuery used(db);
    used.prepare("SELECT invoice_number FROM aeat_records WHERE kind = 'alta' "
                 "AND (invoice_number = :n OR invoice_number LIKE :prefix) AND (:e = '' OR environment = :e)");
    used.bindValue(":n", ticketNum);
    used.bindValue(":e", aeatEnvironment.isNull() ? QStringLiteral("") : aeatEnvironment);
    used.bindValue(":prefix", ticketNum + QStringLiteral("-%"));
    if (used.exec()) {
        while (used.next()) {
            const QString id = used.value(0).toString();
            bool numeric = true;
            const int seq = id == ticketNum ? 0 : id.mid(ticketNum.size() + 1).toInt(&numeric);
            if (numeric)
                next = qMax(next, seq + 1);
        }
    }
    db.close();
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
// it is counted in invalidAmounts so the report can flag it.
static void collectIncomeTickets(QSqlDatabase &db, QDate start, QDate end,
                                 QVector<IncomeTicketDetail> *buckets, int bucketCount,
                                 int (*bucketOf)(const QString &))
{
    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT n_recibo, cliente, fecha_pago, importe FROM ingresos WHERE " + kIngresosIncomeWhere
              + " ORDER BY " + kFechaPagoIso + ", CAST(n_recibo AS INTEGER)");
    q.bindValue(":start", start.toString("yyyy-MM-dd"));
    q.bindValue(":end",   end.toString("yyyy-MM-dd"));
    if (!q.exec()) {
        qWarning() << "collectIncomeTickets: query failed -" << q.lastError().text();
        db.close();
        return;
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
        const QString importe = q.value(3).toString();
        if (importe.contains(QLatin1Char(',')))
            t.invalidAmounts++;
        else
            t.importe += importe.toDouble();
        t.garments++;
    }
    db.close();
}

// Every gastos row in [start, end) into buckets[bucketOf(fecha)]. A NULL iva is
// kept as -1 so it is not mistaken for sin IVA (0).
static void collectExpenses(QSqlDatabase &db, QDate start, QDate end,
                            QVector<ExpenseDetail> *buckets, int bucketCount,
                            int (*bucketOf)(const QString &))
{
    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT n_factura, empresa, servicio, fecha, iva, importe FROM gastos WHERE " + kGastosPeriodWhere
              + " ORDER BY " + kFechaGastoIso + ", id");
    q.bindValue(":start", start.toString("yyyy-MM-dd"));
    q.bindValue(":end",   end.toString("yyyy-MM-dd"));
    if (!q.exec()) {
        qWarning() << "collectExpenses: query failed -" << q.lastError().text();
        db.close();
        return;
    }
    while (q.next()) {
        const int bucket = bucketOf(q.value(3).toString());
        if (bucket < 0 || bucket >= bucketCount) continue;
        ExpenseDetail e;
        e.nFactura = q.value(0).toString();
        e.empresa  = q.value(1).toString();
        e.servicio = q.value(2).toString();
        e.fecha    = q.value(3).toString();
        e.iva      = q.value(4).isNull() ? -1 : q.value(4).toInt();
        const QString importe = q.value(5).toString();
        e.invalidAmount = importe.contains(QLatin1Char(','));
        e.importe  = e.invalidAmount ? 0.0 : importe.toDouble();
        buckets[bucket].append(e);
    }
    db.close();
}

// Regularised rows in [start, end) of their fecha_anulacion, aggregated by n_recibo
// into buckets[bucketOf(fecha_anulacion)].
static void collectRegularizations(QSqlDatabase &db, QDate start, QDate end,
                                   QVector<RegularizationDetail> *buckets, int bucketCount,
                                   int (*bucketOf)(const QString &))
{
    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT n_recibo, cliente, fecha_pago, fecha_anulacion, verifactu_estado, importe "
              "FROM ingresos WHERE " + kRegularizationWhere
              + " ORDER BY " + kFechaAnulacionIso + ", CAST(n_recibo AS INTEGER)");
    q.bindValue(":start", start.toString("yyyy-MM-dd"));
    q.bindValue(":end",   end.toString("yyyy-MM-dd"));
    if (!q.exec()) {
        qWarning() << "collectRegularizations: query failed -" << q.lastError().text();
        db.close();
        return;
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
        const QString importe = q.value(5).toString();
        if (importe.contains(QLatin1Char(',')))
            r.invalidAmounts++;
        else
            r.importe += importe.toDouble();
        r.garments++;
    }
    db.close();
}

QVector<IncomeTicketDetail> incomeTicketsBetweenDates(QSqlDatabase &db, QDate startDate, QDate endDate)
{
    QVector<IncomeTicketDetail> tickets;
    if (dbNotConfigured(db, __func__)) return tickets;
    collectIncomeTickets(db, startDate, endDate, &tickets, 1, singleBucket);
    return tickets;
}

QVector<ExpenseDetail> expensesBetweenDates(QSqlDatabase &db, QDate startDate, QDate endDate)
{
    QVector<ExpenseDetail> expenses;
    if (dbNotConfigured(db, __func__)) return expenses;
    collectExpenses(db, startDate, endDate, &expenses, 1, singleBucket);
    return expenses;
}

QVector<RegularizationDetail> regularizationsBetweenDates(QSqlDatabase &db, QDate startDate, QDate endDate)
{
    QVector<RegularizationDetail> regs;
    if (dbNotConfigured(db, __func__)) return regs;
    collectRegularizations(db, startDate, endDate, &regs, 1, singleBucket);
    return regs;
}

QuarterlyDetails annualDetailsByQuarter(QSqlDatabase &db, int year)
{
    QuarterlyDetails d;
    if (dbNotConfigured(db, __func__)) return d;
    const QDate start(year, 1, 1), end(year + 1, 1, 1);
    collectIncomeTickets(db, start, end, d.income, 4, quarterBucket);
    collectExpenses(db, start, end, d.expenses, 4, quarterBucket);
    collectRegularizations(db, start, end, d.regularizations, 4, quarterBucket);
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

    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT MAX(" + kFechaPagoIso + ") FROM ingresos WHERE n_recibo = :n AND pagado = 'SI'");
    q.bindValue(":n", nRecibo);
    QDate last;
    if (q.exec() && q.first())
        last = QDate::fromString(q.value(0).toString(), Qt::ISODate);
    else
        qWarning() << "ticketLastPaymentDate: query failed -" << q.lastError().text();
    db.close();
    return last;
}

bool markInvoiceSeqCancelled(QSqlDatabase &db, const QString &nRecibo, int seq, QDate cancelDate,
                             const QString &cancelXml)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
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
    db.close();
    return ok;
}

bool markTicketRectified(QSqlDatabase &db, const QString &nRecibo, QDate rectificationDate)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
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
    db.close();
    return ok;
}
