#include "sql_lite.h"

#include <QMessageBox>
#include <QRandomGenerator>
#include <QSqlQuery>

// ---------------------------------------------------------------------------
// DB path — set once in main() before MainWindow is constructed
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
    qWarning() << caller << "— database path not configured, skipping query";
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
        else
            QMessageBox::warning(nullptr, "Error base de datos",
                                 "Búsqueda vacía al usar readMaxValueInColumnFromTable.",
                                 QMessageBox::Ok, QMessageBox::Ok);
    } else {
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
        else
            QMessageBox::warning(nullptr, "Error base de datos",
                                 "Búsqueda vacía al usar readMaxNMinYearInColumnFromTable.",
                                 QMessageBox::Ok, QMessageBox::Ok);
    } else {
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
                QMessageBox::critical(nullptr, "Error en la base de datos",
                                      "En la tabla prendas se ha detectado que hay valores decimales guardados con ','. "
                                      "Utilizar herramienta de limpiado de importes decimales.",
                                      QMessageBox::Ok, QMessageBox::Ok);
                price = -1.0f;
            }
        } else {
            QMessageBox::warning(nullptr, "Error base de datos",
                                 "No se ha encontrado la prenda '" + garment +
                                 "' al usar readGarmentPrice para el servicio '" + service + "'.\n"
                                 "Añadir un precio en el listado de prendas antes de continuar con esta acción.",
                                 QMessageBox::Ok, QMessageBox::Ok);
            price = -1.0f;
        }
    } else {
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
            QMessageBox::warning(nullptr, "Búsqueda vacía",
                                 "El elemento '" + itemToSearch + "' no se ha encontrado en la base de datos para '"
                                 + columnToSearch + "'.",
                                 QMessageBox::Ok, QMessageBox::Ok);
        }
    } else if (printMsg) {
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

    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE clientes SET " + column + " = :item WHERE nombre = :client");
    q.bindValue(":item", item);
    q.bindValue(":client", client);
    bool ok = q.exec();
    db.close();
    return ok;
}

bool addNewClient(QSqlDatabase &db, const QString &client, const QString &telFijo,
                  const QString &direccion, const QString &movil)
{
    if (dbNotConfigured(db, __func__)) return false;

    db.open();
    QSqlQuery q(db);
    q.prepare("INSERT INTO clientes (nombre, tel_fijo, direccion, movil) "
              "VALUES (:nombre, :tel_fijo, :direccion, :movil)");
    q.bindValue(":nombre",    client);
    q.bindValue(":tel_fijo",  telFijo);
    q.bindValue(":direccion", direccion);
    q.bindValue(":movil",     movil);
    bool ok = q.exec();
    db.close();
    return ok;
}

float totalPriceBetweenDates(QSqlDatabase &db, const QString &table,
                             QDate startDate, QDate endDate, int iva)
{
    if (dbNotConfigured(db, __func__)) return 0.0f;

    float totalPrice = 0.0f;
    db.open();
    QSqlQuery q(db);

    if (table == "ingresos") {
        q.exec("SELECT importe FROM ingresos WHERE (pagado = 'SI') AND "
               "(date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2)) >= date('"
               + startDate.toString("yyyy-MM-dd") + "')) AND "
               "(date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2)) < date('"
               + endDate.toString("yyyy-MM-dd") + "'))");
    } else if (table == "gastos") {
        q.exec("SELECT importe FROM gastos WHERE (iva = " + QString::number(iva) + ") AND "
               "(date(substr(fecha,7,4)||'-'||substr(fecha,4,2)||'-'||substr(fecha,1,2)) BETWEEN date('"
               + startDate.toString("yyyy-MM-dd") + "') AND date('"
               + endDate.toString("yyyy-MM-dd") + "'))");
    } else {
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
                QMessageBox::critical(nullptr, "Error en la base de datos",
                                      "En la tabla " + table + " se ha detectado que hay valores decimales "
                                      "guardados con ','. Utilizar herramienta de limpiado de importes decimales.",
                                      QMessageBox::Ok, QMessageBox::Ok);
            }
        }
    } else {
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar totalPriceBetweenDates.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }
    db.close();
    return totalPrice;
}

int readLockForMonthAndYear(QSqlDatabase &db, const QString &table, int month, int year)
{
    if (dbNotConfigured(db, __func__)) return 0;

    const QString mStr = monthStr(month);
    const QString yStr = QString::number(year);

    db.open();
    QSqlQuery q(db);
    int editLock = 2; // 2 = no data found for period
    if (table == "ingresos")
        q.exec("SELECT edit_lock FROM " + table + " WHERE fecha_pago LIKE '%-" + mStr + "-" + yStr + "'");
    else if (table == "gastos")
        q.exec("SELECT edit_lock FROM " + table + " WHERE fecha LIKE '%-" + mStr + "-" + yStr + "'");
    else
        QMessageBox::critical(nullptr, "Error leyendo el bloqueo de contabilidad",
                              "Tabla solicitada no está soportada por la función 'readLockForMonthAndYear'.",
                              QMessageBox::Ok, QMessageBox::Ok);

    if (q.isSelect())
        editLock = q.first() ? q.value(0).toInt() : 0;
    else
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar readLockForMonthAndYear.",
                              QMessageBox::Ok, QMessageBox::Ok);
    db.close();
    return editLock;
}

void updateLockInIngresos(QSqlDatabase &db, int value, int month, int year)
{
    if (dbNotConfigured(db, __func__)) return;

    const QString mStr = monthStr(month);
    const QString yStr = QString::number(year);
    const QString val  = QString::number(value);

    db.open();
    QSqlQuery q(db);
    q.exec("UPDATE ingresos SET edit_lock = " + val + " WHERE fecha_pago LIKE '%-" + mStr + "-" + yStr + "'");
    q.exec("UPDATE gastos   SET edit_lock = " + val + " WHERE fecha      LIKE '%-" + mStr + "-" + yStr + "'");
    db.close();
}

int updateComasInDecimalData(QSqlDatabase &db, const QString &table, const QString &item)
{
    if (dbNotConfigured(db, __func__)) return 0;

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
            q.exec();
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
            q.exec();
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
            q.exec();
            db.close();
            ++errorCnt;
        }
    } else {
        QMessageBox::critical(nullptr, "Error en updateComasInDecimalData",
                              "La tabla especificada '" + table + "' no está soportada.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }
    return errorCnt;
}

void insertNewItemToTable(QSqlDatabase &db, const QStringList &items, const QString &table)
{
    if (dbNotConfigured(db, __func__)) return;

    QString query = "INSERT INTO " + table + " VALUES (";
    for (int i = 0; i < items.count(); ++i) {
        query += "'" + items.value(i) + "'";
        query += (i == items.count() - 1) ? ");" : ", ";
    }
    db.open();
    QSqlQuery q(db);
    q.exec(query);
    db.close();
}

QString genHash16()
{
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    constexpr int len = 16;
    QString hash;
    hash.reserve(len);
    for (int i = 0; i < len; ++i)
        hash += alphanum[QRandomGenerator::global()->bounded(static_cast<quint32>(sizeof(alphanum) - 1))];
    return hash;
}
