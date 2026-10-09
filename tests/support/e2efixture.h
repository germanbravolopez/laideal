#ifndef E2EFIXTURE_H
#define E2EFIXTURE_H

// Shared setup for the end-to-end suites: the ingresos / gastos / clientes /
// prendas schema on a throwaway SQLite file, test settings on a throwaway JSON
// file (never ~/.laideal_settings.json), and small DB helpers. Every external
// effect is off: printing, the GitHub update check, the startup backup and the
// pending-submissions recovery dialog (a scenario re-enables what it tests).

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QtTest>

#include "appsettings.h"
#include "testschema.h"

namespace E2e {

inline bool exec(QSqlDatabase &db, const QString &sql)
{
    db.open();
    QSqlQuery q(db);
    const bool ok = q.exec(sql);
    if (!ok)
        qWarning() << "E2e::exec failed:" << q.lastError().text() << "::" << sql;
    db.close();
    return ok;
}

inline QString scalar(QSqlDatabase &db, const QString &sql)
{
    db.open();
    QSqlQuery q(db);
    QString v;
    if (q.exec(sql) && q.first())
        v = q.value(0).toString();
    db.close();
    return v;
}

// The tables the app reads, through the app's own migrations (support/testschema.h).
inline bool createSchema(QSqlDatabase &db)
{
    return TestSchema::create(db);
}

inline void clearTables(QSqlDatabase &db)
{
    exec(db, "DELETE FROM ingresos");
    exec(db, "DELETE FROM gastos");
    exec(db, "DELETE FROM clientes");
}

// Settings for a run: Verifactu configured (the endpoint itself is redirected by
// VerifactuConfig::setEndpointOverride), reports under `dir`, every external
// effect off.
inline bool configureSettings(const QDir &dir)
{
    const QString path = dir.filePath("settings.json");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write("{}");
    f.close();
    AppSettings *s = AppSettings::instance();
    s->loadFrom(path);
    s->setVerifactuNif("B00000000");
    s->setVerifactuName("Tintoreria E2E");
    s->setVerifactuServiceKey("e2e-service-key");
    s->setVerifactuProduction(false);
    s->setEnablePrinting(false);
    s->setCheckUpdatesOnStartup(false);
    s->setBackupEnabled(false);
    s->setVerifactuPendingRecoveryEnabled(false);
    s->setReportsRoot(dir.filePath("reports"));
    return true;
}

// One unpaid garment of a ticket, as MainWindow::saveTicket leaves it.
inline bool seedGarment(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                        const QString &importe, const QString &fechaRecepcion = QStringLiteral("05-02-2026"))
{
    return exec(db, QStringLiteral("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, "
                                   "fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, "
                                   "observaciones, edit_lock, hash, verifactu_estado, verifactu_invoice_seq) "
                                   "VALUES ('%1', 'Cliente E2E', '%4', '', '', '%3', 'NO', 'En tienda', '1', "
                                   "'Camisa', '', 'Limp.', '', 0, '%2', 'SIN COBRAR', 0)")
                        .arg(nRecibo, hash, importe, fechaRecepcion));
}

// One paid garment already accepted by AEAT (seq 0, bare InvoiceID).
inline bool seedSentGarment(QSqlDatabase &db, const QString &nRecibo, const QString &hash,
                            const QString &importe, const QString &fechaPago, const QString &csv)
{
    return exec(db, QStringLiteral("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, "
                                   "fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, "
                                   "observaciones, edit_lock, hash, verifactu_csv, verifactu_estado, "
                                   "verifactu_invoice_seq, verifactu_invoice_id) "
                                   "VALUES ('%1', 'Cliente E2E', '%4', '%4', '', '%3', 'SI', 'En tienda', '1', "
                                   "'Camisa', '', 'Limp.', '', 0, '%2', '%5', 'ENVIADA', 0, '%1')")
                        .arg(nRecibo, hash, importe, fechaPago, csv));
}

} // namespace E2e

#endif // E2EFIXTURE_H
