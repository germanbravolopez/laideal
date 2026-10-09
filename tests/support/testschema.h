#ifndef TESTSCHEMA_H
#define TESTSCHEMA_H

// The shop database's tables for a throwaway test DB. `ingresos` is created with
// its original (pre-Verifactu) columns and then brought up to date by the app's
// own migrateDatabase(), so every column a migration adds reaches the suites
// without being copied here by hand.

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
        " observaciones TEXT, edit_lock INTEGER DEFAULT 0, hash TEXT)",
        "CREATE TABLE gastos (id INTEGER PRIMARY KEY, n_factura TEXT, servicio TEXT, "
        "descripcion TEXT, empresa TEXT, fecha TEXT, importe TEXT, iva INTEGER, "
        "edit_lock INTEGER DEFAULT 0)",
        "CREATE TABLE clientes (nombre TEXT, tel_fijo TEXT, movil TEXT, direccion TEXT)",
        "CREATE TABLE prendas (nombre TEXT, precio_limpieza TEXT, precio_plancha TEXT)",
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

} // namespace TestSchema

#endif // TESTSCHEMA_H
