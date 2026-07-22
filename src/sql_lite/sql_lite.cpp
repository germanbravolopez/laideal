#include "sql_lite.h"

#include "../verifactu/verifactutypes.h"

#include <QDateTime>
#include <QDebug>
#include <QMessageBox>
#include <QUuid>
#include <QSqlError>
#include <QSqlQuery>

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

bool updateTicketPayment(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                         const QString &fechaPago, const QString &pagado)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET fecha_pago = :fp, pagado = :pag "
              "WHERE n_recibo = :n AND hash = :h");
    q.bindValue(":fp",  fechaPago);
    q.bindValue(":pag", pagado);
    q.bindValue(":n",   nRecibo);
    q.bindValue(":h",   hash);
    bool ok = q.exec();
    if (!ok)
        qWarning() << "updateTicketPayment: UPDATE failed -" << q.lastError().text();
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

bool updateTicketSizeAndPrice(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                              const QString &size, const QString &importe)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET size = :sz, importe = :imp "
              "WHERE n_recibo = :n AND hash = :h");
    q.bindValue(":sz",  size);
    q.bindValue(":imp", importe);
    q.bindValue(":n",   nRecibo);
    q.bindValue(":h",   hash);
    bool ok = q.exec();
    if (!ok)
        qWarning() << "updateTicketSizeAndPrice: UPDATE failed -" << q.lastError().text();
    db.close();
    return ok;
}

bool updateGarmentQtyAndImporte(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                const QString &cantidad, const QString &importe)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET cantidad = :cant, importe = :imp "
              "WHERE n_recibo = :n AND hash = :h");
    q.bindValue(":cant", cantidad);
    q.bindValue(":imp",  importe);
    q.bindValue(":n",    nRecibo);
    q.bindValue(":h",    hash);
    bool ok = q.exec();
    if (!ok)
        qWarning() << "updateGarmentQtyAndImporte: UPDATE failed -" << q.lastError().text();
    db.close();
    return ok;
}

bool updateGarmentServiceAndImporte(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                                    const QString &servicio, const QString &importe)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET servicio = :serv, importe = :imp "
              "WHERE n_recibo = :n AND hash = :h");
    q.bindValue(":serv", servicio);
    q.bindValue(":imp",  importe);
    q.bindValue(":n",    nRecibo);
    q.bindValue(":h",    hash);
    bool ok = q.exec();
    if (!ok)
        qWarning() << "updateGarmentServiceAndImporte: UPDATE failed -" << q.lastError().text();
    db.close();
    return ok;
}

bool garmentIsLocallyVoidable(const QString &pagado, const QString &verifactuEstado)
{
    if (pagado == QLatin1String("SI"))
        return false;
    return verifactuEstadoFromString(verifactuEstado) == VerifactuEstado::NotSubmitted;
}

bool voidGarmentRow(QSqlDatabase &db, const QString &nRecibo, const QString &hash)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
    QSqlQuery q(db);
    // fecha_pago / fecha_recogida are stamped with the cancellation date so the
    // moment the garment was voided is recorded. Neither date is taxable here:
    // the row keeps pagado = "NO" and gets verifactu_estado ANULADA, and every
    // accounting query filters on pagado = 'SI' plus a not-ANULADA estado.
    q.prepare("UPDATE ingresos SET estado = :est, verifactu_estado = :vest, "
              "fecha_pago = :fecha, fecha_recogida = :fecha "
              "WHERE n_recibo = :n AND hash = :h");
    q.bindValue(":est",   QStringLiteral(INGRESOS_ESTADO_ANULADO));
    q.bindValue(":vest",  verifactuEstadoToString(VerifactuEstado::Anulada));
    q.bindValue(":fecha", QDate::currentDate().toString("dd-MM-yyyy"));
    q.bindValue(":n",     nRecibo);
    q.bindValue(":h",     hash);
    bool ok = q.exec();
    if (!ok)
        qWarning() << "voidGarmentRow: UPDATE failed -" << q.lastError().text();
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

bool insertGarmentRow(QSqlDatabase &db, const IngresoGarmentRow &row)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
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
    q.bindValue(":importe",          row.importe);
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
    bool ok = q.exec();
    if (!ok)
        qWarning() << "insertGarmentRow: INSERT failed -" << q.lastError().text();
    db.close();
    return ok;
}

QString ticketVerifactuEstado(QSqlDatabase &db, const QString &nRecibo)
{
    if (dbNotConfigured(db, __func__)) return QString();

    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT verifactu_estado FROM ingresos WHERE n_recibo = :n LIMIT 1");
    q.bindValue(":n", nRecibo);
    QString estado;
    if (q.exec() && q.next())
        estado = q.value(0).toString();
    db.close();
    return estado;
}

float totalPriceBetweenDates(QSqlDatabase &db, const QString &table,
                             QDate startDate, QDate endDate, int iva)
{
    if (dbNotConfigured(db, __func__)) return 0.0f;

    float totalPrice = 0.0f;
    db.open();
    QSqlQuery q(db);

    if (table == "ingresos") {
        // Exclude ANULADA (cancelled) and RECTIFICADA (superseded by a substitution
        // rectificativa) rows from taxable income. The rectifying row itself carries
        // the corrected total and counts normally. Rows without verifactu_estado
        // (NULL or empty, pre-v8.0) are included as normal.
        q.prepare("SELECT importe FROM ingresos WHERE (pagado = 'SI') AND "
                  "(verifactu_estado IS NULL OR verifactu_estado = '' OR "
                  " (verifactu_estado != 'ANULADA' AND verifactu_estado != 'RECTIFICADA')) AND "
                  "(date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2)) >= date(:start)) AND "
                  "(date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2)) < date(:end))");
        q.bindValue(":start", startDate.toString("yyyy-MM-dd"));
        q.bindValue(":end",   endDate.toString("yyyy-MM-dd"));
        q.exec();
    } else if (table == "gastos") {
        // Use >= start AND < end (same half-open interval as ingresos) so that expenses on the
        // first day of the next quarter are not double-counted in the current quarter.
        q.prepare("SELECT importe FROM gastos WHERE (iva = :iva) AND "
                  "(date(substr(fecha,7,4)||'-'||substr(fecha,4,2)||'-'||substr(fecha,1,2)) >= date(:start)) AND "
                  "(date(substr(fecha,7,4)||'-'||substr(fecha,4,2)||'-'||substr(fecha,1,2)) < date(:end))");
        q.bindValue(":iva",   iva);
        q.bindValue(":start", startDate.toString("yyyy-MM-dd"));
        q.bindValue(":end",   endDate.toString("yyyy-MM-dd"));
        q.exec();
    } else {
        qCritical() << "totalPriceBetweenDates: unsupported table:" << table;
        QMessageBox::critical(nullptr, "Error base de datos",
                              "totalPriceBetweenDates: tabla no soportada: " + table,
                              QMessageBox::Ok, QMessageBox::Ok);
        db.close();
        return 0.0f;
    }

    if (q.isSelect()) {
        while (q.next()) {
            if (!q.value(0).toString().contains(',')) {
                totalPrice += q.value(0).toFloat();
            } else {
                qCritical() << "totalPriceBetweenDates: comma decimal found in" << table;
                QMessageBox::critical(nullptr, "Error en la base de datos",
                                      "En la tabla " + table + " se ha detectado que hay valores decimales "
                                      "guardados con ','. Utilizar herramienta de limpiado de importes decimales.",
                                      QMessageBox::Ok, QMessageBox::Ok);
            }
        }
    } else {
        qCritical() << "totalPriceBetweenDates: query error for table" << table;
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar totalPriceBetweenDates.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }
    db.close();
    return totalPrice;
}

int countOperationsBetweenDates(QSqlDatabase &db, const QString &table,
                                QDate startDate, QDate endDate)
{
    if (dbNotConfigured(db, __func__)) return 0;

    int count = 0;
    db.open();
    QSqlQuery q(db);

    if (table == "ingresos") {
        // Distinct paid tickets, excluding ANULADA / RECTIFICADA, mirroring the
        // estado + date filter of totalPriceBetweenDates so the operation count
        // matches the income total shown alongside it.
        q.prepare("SELECT COUNT(DISTINCT n_recibo) FROM ingresos WHERE (pagado = 'SI') AND "
                  "(verifactu_estado IS NULL OR verifactu_estado = '' OR "
                  " (verifactu_estado != 'ANULADA' AND verifactu_estado != 'RECTIFICADA')) AND "
                  "(date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2)) >= date(:start)) AND "
                  "(date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2)) < date(:end))");
        q.bindValue(":start", startDate.toString("yyyy-MM-dd"));
        q.bindValue(":end",   endDate.toString("yyyy-MM-dd"));
        q.exec();
    } else if (table == "gastos") {
        q.prepare("SELECT COUNT(*) FROM gastos WHERE "
                  "(date(substr(fecha,7,4)||'-'||substr(fecha,4,2)||'-'||substr(fecha,1,2)) >= date(:start)) AND "
                  "(date(substr(fecha,7,4)||'-'||substr(fecha,4,2)||'-'||substr(fecha,1,2)) < date(:end))");
        q.bindValue(":start", startDate.toString("yyyy-MM-dd"));
        q.bindValue(":end",   endDate.toString("yyyy-MM-dd"));
        q.exec();
    } else {
        qCritical() << "countOperationsBetweenDates: unsupported table:" << table;
        db.close();
        return 0;
    }

    if (q.isSelect() && q.next())
        count = q.value(0).toInt();
    else
        qWarning() << "countOperationsBetweenDates: query error for table" << table << q.lastError().text();

    db.close();
    return count;
}

int readLockForMonthAndYear(QSqlDatabase &db, const QString &table, int month, int year)
{
    if (dbNotConfigured(db, __func__)) return 0;

    const QString mStr = monthStr(month);
    const QString yStr = QString::number(year);

    const QString pattern = "%-" + mStr + "-" + yStr;
    db.open();
    QSqlQuery q(db);
    int editLock = 2; // 2 = no data found for period
    if (table == "ingresos") {
        q.prepare("SELECT edit_lock FROM ingresos WHERE fecha_pago LIKE :pat");
        q.bindValue(":pat", pattern);
        q.exec();
    } else if (table == "gastos") {
        q.prepare("SELECT edit_lock FROM gastos WHERE fecha LIKE :pat");
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
    db.close();
    return editLock;
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
    double price = quantity * unitPrice;
    if (price < 0.0)
        return 0.0;
    const double size = sizeText.trimmed().replace(',', '.').toDouble();
    return (size != 0.0) ? size * price : price;
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

void insertNewItemToTable(QSqlDatabase &db, const QStringList &items, const QString &table)
{
    if (dbNotConfigured(db, __func__)) return;

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
    if (!q.exec())
        qWarning() << "insertNewItemToTable: insert into" << table << "failed -" << q.lastError().text();
    db.close();
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
        "SELECT n_recibo, verifactu_invoice_seq, MIN(fecha_pago), "
        "       MIN(cliente), SUM(importe) "
        "FROM ingresos "
        "WHERE (verifactu_estado IS NULL OR verifactu_estado = '' "
        "       OR verifactu_estado = 'PENDIENTE') "
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

QuarterlyAccountingTotals annualAccountingByQuarter(QSqlDatabase &db, int year)
{
    QuarterlyAccountingTotals t;
    if (dbNotConfigured(db, __func__)) return t;

    // Half-open [start, end) over the whole year, same shape as the per-quarter
    // helpers but for all 12 months at once; quarter bucketing happens in SQL.
    const QString start = QStringLiteral("%1-01-01").arg(year);
    const QString end   = QStringLiteral("%1-01-01").arg(year + 1);

    db.open();

    // Ingresos in one scan, GROUP BY quarter. Filters mirror
    // totalPriceBetweenDates (pagado/estado/date) and the COUNT(DISTINCT n_recibo)
    // mirrors countOperationsBetweenDates. Quarter = (month + 2) / 3 with month =
    // substr(fecha_pago,4,2): months 1-3 -> 1, 4-6 -> 2, 7-9 -> 3, 10-12 -> 4
    // (integer division). Rows with an invalid/empty date are dropped by the
    // date() comparison (NULL), exactly as the per-quarter query drops them.
    {
        QSqlQuery q(db);
        q.prepare(
            "SELECT (CAST(substr(fecha_pago,4,2) AS INTEGER) + 2) / 3 AS q, "
            "       SUM(importe), COUNT(DISTINCT n_recibo) "
            "FROM ingresos WHERE (pagado = 'SI') AND "
            "(verifactu_estado IS NULL OR verifactu_estado = '' OR "
            " (verifactu_estado != 'ANULADA' AND verifactu_estado != 'RECTIFICADA')) AND "
            "(date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2)) >= date(:start)) AND "
            "(date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2)) < date(:end)) "
            "GROUP BY q");
        q.bindValue(":start", start);
        q.bindValue(":end",   end);
        if (q.exec()) {
            while (q.next()) {
                const int quarter = q.value(0).toInt();
                if (quarter < 1 || quarter > 4) continue;
                t.ingImporte[quarter - 1] = q.value(1).toDouble();
                t.ingTickets[quarter - 1] = q.value(2).toInt();
            }
        } else {
            qWarning() << "annualAccountingByQuarter: ingresos query failed -" << q.lastError().text();
        }
    }

    // Gastos in one scan, GROUP BY (quarter, iva). importe goes to the 10/21/0
    // bucket by rate (other rates contribute to no importe bucket, matching the
    // existing computeFigures which only queries 10/21/0); facturas counts every
    // row regardless of rate (matches countOperationsBetweenDates for gastos).
    {
        QSqlQuery q(db);
        q.prepare(
            "SELECT (CAST(substr(fecha,4,2) AS INTEGER) + 2) / 3 AS q, iva, "
            "       SUM(importe), COUNT(*) "
            "FROM gastos WHERE "
            "(date(substr(fecha,7,4)||'-'||substr(fecha,4,2)||'-'||substr(fecha,1,2)) >= date(:start)) AND "
            "(date(substr(fecha,7,4)||'-'||substr(fecha,4,2)||'-'||substr(fecha,1,2)) < date(:end)) "
            "GROUP BY q, iva");
        q.bindValue(":start", start);
        q.bindValue(":end",   end);
        if (q.exec()) {
            while (q.next()) {
                const int quarter = q.value(0).toInt();
                if (quarter < 1 || quarter > 4) continue;
                const int    iva = q.value(1).toInt();
                const double imp = q.value(2).toDouble();
                if (iva == 10)      t.gas10Importe[quarter - 1] = imp;
                else if (iva == 21) t.gas21Importe[quarter - 1] = imp;
                else if (iva == 0)  t.gasNiImporte[quarter - 1] = imp;
                t.gasFacturas[quarter - 1] += q.value(3).toInt();
            }
        } else {
            qWarning() << "annualAccountingByQuarter: gastos query failed -" << q.lastError().text();
        }
    }

    db.close();
    return t;
}

void updateTicketVerifactuFields(QSqlDatabase &db, const QString &ticketNum,
                                 const VerifactuResult &result, int seq)
{
    if (dbNotConfigured(db, __func__)) return;

    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    const QString estado    = verifactuEstadoToString(
        result.isSuccess() ? VerifactuEstado::Enviada : VerifactuEstado::Error);
    // seq=0 = save-time / retry submit (bare n_recibo); seq>0 = PayDialog event
    // (<n_recibo>-<seq>). The WHERE clause always scopes by seq so a retry of
    // save-time never clobbers PayDialog rows.
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
              "WHERE n_recibo = :n_recibo AND verifactu_invoice_seq = :seq");
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
        q.bindValue(":id",     "");
    }
    q.bindValue(":n_recibo", ticketNum);
    q.bindValue(":seq",      seq);
    if (!q.exec())
        qWarning() << "updateTicketVerifactuFields UPDATE failed for ticket" << ticketNum
                   << "seq" << seq << "-" << q.lastError().text();
    db.close();
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
