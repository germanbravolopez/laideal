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
        // The shop's own definitions (read from its database, 24-10-2026): every original
        // column NOT NULL, so a NULL the app binds fails here as it fails in the shop. The
        // DEFAULT '' is the tests' only addition: it lets their seeding INSERTs name just the
        // columns they need (every INSERT of the app names all of them).
        "CREATE TABLE ingresos ("
        " n_recibo TEXT NOT NULL DEFAULT '', cliente TEXT NOT NULL DEFAULT '', fecha_recepcion TEXT NOT NULL DEFAULT '', fecha_pago TEXT NOT NULL DEFAULT '',"
        " fecha_recogida TEXT NOT NULL DEFAULT '', importe TEXT NOT NULL DEFAULT '', pagado TEXT NOT NULL DEFAULT '', estado TEXT NOT NULL DEFAULT '',"
        " cantidad TEXT NOT NULL DEFAULT '', prenda TEXT NOT NULL DEFAULT '', size TEXT NOT NULL DEFAULT '', servicio TEXT NOT NULL DEFAULT '',"
        " observaciones TEXT NOT NULL DEFAULT '', edit_lock TEXT NOT NULL, hash TEXT NOT NULL DEFAULT '')",
        // Column order as in the shop DB (GASTOS_IDX_* read rows by position); no primary key there.
        "CREATE TABLE gastos (id NUMERIC NOT NULL, n_factura TEXT NOT NULL DEFAULT '', servicio TEXT NOT NULL DEFAULT '', "
        "descripcion TEXT NOT NULL DEFAULT '', empresa TEXT NOT NULL DEFAULT '', fecha TEXT NOT NULL DEFAULT '', iva TEXT NOT NULL DEFAULT '', "
        "importe TEXT NOT NULL DEFAULT '', edit_lock TEXT)",
        "CREATE TABLE clientes (nombre TEXT NOT NULL DEFAULT '', tel_fijo TEXT NOT NULL DEFAULT '', movil TEXT NOT NULL DEFAULT '', direccion TEXT NOT NULL DEFAULT '')",
        "CREATE TABLE prendas (nombre TEXT NOT NULL DEFAULT '', precio_limpieza TEXT NOT NULL DEFAULT '', precio_plancha TEXT NOT NULL DEFAULT '', "
        "PRIMARY KEY(nombre))",
        "CREATE TABLE proveedores (nombre TEXT NOT NULL DEFAULT '', cif TEXT NOT NULL DEFAULT '', direccion TEXT NOT NULL DEFAULT '', telefono TEXT NOT NULL DEFAULT '')",
        "CREATE TABLE servicios (nombre TEXT NOT NULL DEFAULT '')",
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
