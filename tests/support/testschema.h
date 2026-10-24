#ifndef TESTSCHEMA_H
#define TESTSCHEMA_H

// The shop database's tables for a throwaway test DB. `ingresos` is created with
// its original (pre-Verifactu) columns and then brought up to date by the app's
// own migrateDatabase(), so every column a migration adds reaches the suites
// without being copied here by hand. Contabilidad's quarterly lock lives in
// accounting_closures (created by that migration); the edit_lock columns are legacy
// and only read once, by the migration's backfill. Their constraints are the shop's:
// ingresos.edit_lock is TEXT NOT NULL without a default (every INSERT must still
// write it), gastos.edit_lock is a plain TEXT.

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

#include "sql_lite.h"

namespace TestSchema {

inline bool create(QSqlDatabase &db)
{
    static const char *const tables[] = {
        "CREATE TABLE ingresos ("
        " n_recibo TEXT, cliente TEXT, fecha_recepcion TEXT, fecha_pago TEXT,"
        " fecha_recogida TEXT, importe TEXT, pagado TEXT, estado TEXT,"
        " cantidad TEXT, prenda TEXT, size TEXT, servicio TEXT,"
        " observaciones TEXT, edit_lock TEXT NOT NULL, hash TEXT)",
        // Column order as in the shop DB (GASTOS_IDX_* read rows by position).
        "CREATE TABLE gastos (id INTEGER PRIMARY KEY, n_factura TEXT, servicio TEXT, "
        "descripcion TEXT, empresa TEXT, fecha TEXT, iva TEXT, importe TEXT, "
        "edit_lock TEXT)",
        "CREATE TABLE clientes (nombre TEXT, tel_fijo TEXT, movil TEXT, direccion TEXT)",
        "CREATE TABLE prendas (nombre TEXT, precio_limpieza TEXT, precio_plancha TEXT)",
        "CREATE TABLE proveedores (nombre TEXT, cif TEXT, direccion TEXT, telefono TEXT)",
        "CREATE TABLE servicios (nombre TEXT)",
    };
    if (!db.open())
        return false;
    bool ok = true;
    {
        QSqlQuery q(db);
        for (const char *sql : tables) {
            if (!q.exec(QString::fromLatin1(sql))) {
                qWarning() << "TestSchema::create failed:" << q.lastError().text() << "::" << sql;
                ok = false;
            }
        }
    }
    db.close();
    if (ok)
        migrateDatabase(db);
    return ok;
}

// accounting_closures is append-only (no DELETE): a fresh table is how a test reopens every quarter.
inline void resetClosures(QSqlDatabase &db)
{
    if (db.open()) {
        QSqlQuery(db).exec("DROP TABLE IF EXISTS accounting_closures");
        db.close();
    }
    migrateDatabase(db);
}

} // namespace TestSchema

#endif // TESTSCHEMA_H
