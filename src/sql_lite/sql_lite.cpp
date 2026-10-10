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

// The row's amount is still open: unpaid, never sent to AEAT, not locked by Contabilidad.
static const QString kAmountEditableWhere =
    QStringLiteral(" AND COALESCE(pagado, '') != 'SI' "
                   " AND COALESCE(verifactu_estado, '') NOT IN ('ENVIADA', 'ERROR', 'ANULADA', 'RECTIFICADA') "
                   " AND COALESCE(edit_lock, 0) = 0");

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
    q.exec("SELECT MAX(" + column + ") FROM " + table);
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
                                      "En la tabla prendas se ha detectado que hay valores decimales guardados con ','. "
                                      "Utilizar herramienta de limpiado de importes decimales.",
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
    q.prepare("SELECT cantidad, importe, edit_lock FROM ingresos WHERE n_recibo = :n AND hash = :h");
    q.bindValue(":n", nRecibo);
    q.bindValue(":h", hash);
    if (!q.exec() || !q.first()) {
        qWarning() << "splitGarmentRow: row not found -" << nRecibo << hash << q.lastError().text();
        db.close();
        return QString();
    }
    const int qty = q.value(0).toInt();
    if (q.value(2).toInt() != 0) {
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
              "AND COALESCE(edit_lock, 0) = 0 AND cantidad = :oldCant");
    u.bindValue(":cant", QString::number(qty - nGarm));
    u.bindValue(":imp",  money(total4 - split4));
    u.bindValue(":n",    nRecibo);
    u.bindValue(":h",    hash);
    u.bindValue(":oldCant", QString::number(qty));
    bool ok = u.exec() && u.numRowsAffected() == 1;
    // Every other column is copied, verifactu_* included: the split-off garments stay
    // in the invoice (and the estado) of the row they come from.
    QSqlQuery i(db);
    i.prepare("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, "
              "  importe, pagado, estado, cantidad, prenda, size, servicio, observaciones, edit_lock, "
              "  hash, verifactu_csv, verifactu_timestamp, verifactu_estado, verifactu_error, "
              "  verifactu_url_qr, verifactu_xml, verifactu_hash, verifactu_rectifies_n_recibo, "
              "  verifactu_rectification_type, verifactu_invoice_seq, verifactu_invoice_id, "
              "  fecha_anulacion, verifactu_cancel_xml) "
              "SELECT n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, "
              "  :imp, pagado, estado, :cant, prenda, size, servicio, observaciones, edit_lock, "
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
              ":observaciones, :edit_lock, :hash, :verifactu_estado)");
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
    q.bindValue(":edit_lock",        row.editLock);
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



int readLockForMonthAndYear(QSqlDatabase &db, const QString &table, int month, int year)
{
    if (dbNotConfigured(db, __func__)) return 0;

    const QString mStr = monthStr(month);
    const QString yStr = QString::number(year);

    const QString pattern = "%-" + mStr + "-" + yStr;
    db.open();
    QSqlQuery q(db);
    int editLock = 2; // 2 = no data found for period
    // COALESCE(MAX(edit_lock), 0): the month reads locked (1) if ANY row in it is
    // locked, regardless of row order, and 0 when it is open or has no rows. MAX
    // (not "first row wins") matters because a row added after the close (e.g. a
    // split-off garment) carries edit_lock 0 - it must not mask a locked sibling and
    // report an already-locked month as open. Callers only branch on == 1 / == 0,
    // so keeping the empty-month result at 0 preserves existing behaviour.
    if (table == "ingresos") {
        q.prepare("SELECT COALESCE(MAX(edit_lock), 0) FROM ingresos WHERE fecha_pago LIKE :pat");
        q.bindValue(":pat", pattern);
        q.exec();
    } else if (table == "gastos") {
        q.prepare("SELECT COALESCE(MAX(edit_lock), 0) FROM gastos WHERE fecha LIKE :pat");
        q.bindValue(":pat", pattern);
        q.exec();
    } else {
        qCritical() << "readLockForMonthAndYear: unsupported table:" << table;
        QMessageBox::critical(nullptr, "Error leyendo el bloqueo de contabilidad",
                              "Tabla solicitada no está soportada por la función 'readLockForMonthAndYear'.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }

    if (q.isSelect())
        editLock = q.first() ? q.value(0).toInt() : 0;
    else {
        qCritical() << "readLockForMonthAndYear: query error for table" << table << "month" << mStr << "year" << yStr;
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar readLockForMonthAndYear.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }
    db.close();
    return editLock;
}

int readLockForQuarter(QSqlDatabase &db, const QString &table, int quarter, int year)
{
    if (dbNotConfigured(db, __func__)) return 0;

    const QString m1   = monthStr((quarter - 1) * 3 + 1);
    const QString m2   = monthStr((quarter - 1) * 3 + 2);
    const QString m3   = monthStr((quarter - 1) * 3 + 3);
    const QString yStr = QString::number(year);

    QString dateCol;
    if (table == "ingresos")    dateCol = "fecha_pago";
    else if (table == "gastos") dateCol = "fecha";
    else {
        qCritical() << "readLockForQuarter: unsupported table:" << table;
        return 2;
    }

    // One pass over the quarter's three months (date stored as dd-MM-yyyy, so
    // substr(col,4,2) is MM and substr(col,7,4) is yyyy). COUNT separates the
    // no-data case (2) from data-present; MAX(edit_lock) reports the quarter as
    // locked (1) if any row is locked, else open (0).
    // An open connection stays open: Listado asks while its table model reads through it.
    const bool wasOpen = db.isOpen();
    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*), COALESCE(MAX(edit_lock), 0) FROM " + table + " WHERE "
              "substr(" + dateCol + ", 4, 2) IN (:m1, :m2, :m3) AND substr(" + dateCol + ", 7, 4) = :y");
    q.bindValue(":m1", m1);
    q.bindValue(":m2", m2);
    q.bindValue(":m3", m3);
    q.bindValue(":y",  yStr);

    int editLock = 2; // 2 = no data found for the quarter
    if (q.exec() && q.first())
        editLock = (q.value(0).toInt() == 0) ? 2 : q.value(1).toInt();
    else
        qWarning() << "readLockForQuarter: query error for table" << table
                   << "quarter" << quarter << "year" << yStr << "-" << q.lastError().text();
    q.finish();
    if (!wasOpen)
        db.close();
    return editLock;
}

QStringList verifactuStoredRecordXmls(QSqlDatabase &db)
{
    if (dbNotConfigured(db, __func__)) return {};
    QStringList out;
    db.open();
    QSqlQuery q(db);
    if (q.exec("SELECT verifactu_xml FROM ingresos WHERE COALESCE(verifactu_xml, '') != '' "
               "UNION ALL SELECT verifactu_cancel_xml FROM ingresos WHERE COALESCE(verifactu_cancel_xml, '') != ''"))
        while (q.next())
            out << q.value(0).toString();
    else
        qWarning() << "verifactuStoredRecordXmls: query failed -" << q.lastError().text();
    db.close();
    return out;
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

int aeatPendingRecordCount(QSqlDatabase &db)
{
    if (dbNotConfigured(db, __func__)) return 0;
    int count = 0;
    db.open();
    QSqlQuery q(db);
    // The table is the direct client's; a failed query just means it was never used.
    if (q.exec("SELECT COUNT(*) FROM aeat_records WHERE state = 'PENDIENTE'") && q.next())
        count = q.value(0).toInt();
    db.close();
    return count;
}

int applySettledVerifactuResult(QSqlDatabase &db, const QString &invoiceId, const VerifactuResult &result)
{
    QString nRecibo = invoiceId.trimmed();
    int seq = 0;
    const int dash = nRecibo.lastIndexOf(QLatin1Char('-'));
    if (dash > 0) {
        bool numeric = false;
        const int parsed = nRecibo.mid(dash + 1).toInt(&numeric);
        if (numeric) {
            seq = parsed;
            nRecibo = nRecibo.left(dash);
        }
    }
    if (verifactuEventFor(db, nRecibo, seq).nRecibo.isEmpty())
        return -1;
    return updateTicketVerifactuFields(db, nRecibo, result, seq);
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

void updateLockForMonth(QSqlDatabase &db, int value, int month, int year)
{
    if (dbNotConfigured(db, __func__)) return;

    const QString mStr    = monthStr(month);
    const QString yStr    = QString::number(year);
    const QString pattern = "%-" + mStr + "-" + yStr;

    qDebug() << "updateLockForMonth: UPDATE ingresos+gastos SET edit_lock =" << value
             << "WHERE month=" << mStr << "year=" << yStr;
    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET edit_lock = :val WHERE fecha_pago LIKE :pat");
    q.bindValue(":val", value);
    q.bindValue(":pat", pattern);
    if (!q.exec())
        qWarning() << "updateLockForMonth: failed to update ingresos for" << mStr << yStr << "-" << q.lastError().text();
    q.prepare("UPDATE gastos SET edit_lock = :val WHERE fecha LIKE :pat");
    q.bindValue(":val", value);
    q.bindValue(":pat", pattern);
    if (!q.exec())
        qWarning() << "updateLockForMonth: failed to update gastos for" << mStr << yStr << "-" << q.lastError().text();
    db.close();
}

int updateComasInDecimalData(QSqlDatabase &db, const QString &table, const QString &item)
{
    if (dbNotConfigured(db, __func__)) return 0;

    qDebug() << "updateComasInDecimalData: scanning" << table << "for comma decimals in column" << item;
    int errorCnt = 0;

    if (table == "ingresos") {
        QStringList items  = readColumnFromTable(db, item,       table, "");
        QStringList ids1   = readColumnFromTable(db, "n_recibo", table, "");
        QStringList ids2   = readColumnFromTable(db, "hash",     table, "");

        for (int i = 0; i < items.count(); ++i) {
            if (!items[i].contains(',')) continue;
            db.open();
            QSqlQuery q(db);
            q.prepare("UPDATE " + table + " SET " + item + " = :value "
                      "WHERE n_recibo = :id1 AND hash = :id2");
            q.bindValue(":value", QString(items[i]).replace(',', '.'));
            q.bindValue(":id1",   ids1[i]);
            q.bindValue(":id2",   ids2[i]);
            if (!q.exec())
                qWarning() << "updateComasInDecimalData: failed to fix comma in" << table << "hash" << ids2[i] << "-" << q.lastError().text();
            db.close();
            ++errorCnt;
        }
    } else if (table == "gastos") {
        QStringList items = readColumnFromTable(db, item, table, "id");
        QStringList ids   = readColumnFromTable(db, "id", table, "id");

        for (int i = 0; i < items.count(); ++i) {
            if (!items[i].contains(',')) continue;
            db.open();
            QSqlQuery q(db);
            q.prepare("UPDATE " + table + " SET " + item + " = :value WHERE id = :id");
            q.bindValue(":value", QString(items[i]).replace(',', '.'));
            q.bindValue(":id",   ids[i]);
            if (!q.exec())
                qWarning() << "updateComasInDecimalData: failed to fix comma in" << table << "id" << ids[i] << "-" << q.lastError().text();
            db.close();
            ++errorCnt;
        }
    } else if (table == "prendas") {
        QStringList items = readColumnFromTable(db, item,     table, "nombre");
        QStringList ids   = readColumnFromTable(db, "nombre", table, "nombre");

        for (int i = 0; i < items.count(); ++i) {
            if (!items[i].contains(',')) continue;
            db.open();
            QSqlQuery q(db);
            q.prepare("UPDATE " + table + " SET " + item + " = :value WHERE nombre = :id");
            q.bindValue(":value", QString(items[i]).replace(',', '.'));
            q.bindValue(":id",   ids[i]);
            if (!q.exec())
                qWarning() << "updateComasInDecimalData: failed to fix comma in" << table << "nombre" << ids[i] << "-" << q.lastError().text();
            db.close();
            ++errorCnt;
        }
    } else {
        qCritical() << "updateComasInDecimalData: unsupported table:" << table;
        QMessageBox::critical(nullptr, "Error en updateComasInDecimalData",
                              "La tabla especificada '" + table + "' no está soportada.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }
    return errorCnt;
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
        "  AND substr(fecha_recepcion, 7, 4) || '-' "
        "      || substr(fecha_recepcion, 4, 2) || '-' "
        "      || substr(fecha_recepcion, 1, 2) >= :floor "
        "GROUP BY n_recibo, verifactu_invoice_seq "
        "ORDER BY n_recibo DESC, verifactu_invoice_seq");
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

int nextVerifactuInvoiceSeq(QSqlDatabase &db, const QString &ticketNum)
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
    db.close();
    return next;
}

QString genHash16()
{
    // QUuid::Id128 is the 32-char hex form without braces/dashes. Truncating
    // to 16 leaves 64 bits of cryptographic entropy - 2^32 row birthday-
    // collision is around one chance in 4 billion, far below any plausible
    // ingresos size. Replaces a homegrown alphanum loop on QRandomGenerator
    // that had a legacy rand()-era predecessor responsible for the
    // cross-ticket hash collisions surfaced by "Crear hash en ingresos".
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
    const int quarter = (date.month() + 2) / 3;
    return readLockForQuarter(db, "ingresos", quarter, date.year()) == 1
        || readLockForQuarter(db, "gastos", quarter, date.year()) == 1;
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
    q.prepare("UPDATE ingresos SET verifactu_estado = :estado, "
              "fecha_anulacion = COALESCE(NULLIF(fecha_anulacion, ''), :fecha) "
              "WHERE n_recibo = :num AND pagado = 'SI'");
    q.bindValue(":estado", verifactuEstadoToString(VerifactuEstado::Rectificada));
    q.bindValue(":fecha",  rectificationDate.toString("dd-MM-yyyy"));
    q.bindValue(":num",    nRecibo);
    const bool ok = q.exec() && q.numRowsAffected() > 0;   // no paid row matched = nothing marked
    if (!ok)
        qWarning() << "markTicketRectified: UPDATE failed or matched no paid row for ticket" << nRecibo << "-" << q.lastError().text();
    db.close();
    return ok;
}
