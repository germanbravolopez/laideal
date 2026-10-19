#include "selfcheck.h"

#include "contabilidad.h"
#include "appsettings.h"
#include "sql_lite.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>

namespace SelfCheck {

namespace {

// Warnings and errors logged while a check runs (and still passed on to the log).
QStringList *s_captured = nullptr;
QtMessageHandler s_previous = nullptr;

void captureHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (s_captured && type != QtDebugMsg && type != QtInfoMsg)
        s_captured->append(message);
    if (s_previous)
        s_previous(type, context, message);
}

struct LogCapture {
    QStringList messages;
    LogCapture() { s_captured = &messages; s_previous = qInstallMessageHandler(captureHandler); }
    ~LogCapture() { qInstallMessageHandler(s_previous); s_captured = nullptr; s_previous = nullptr; }
};

QString scalar(QSqlDatabase &db, const QString &sql, const QVariantMap &binds = {})
{
    QSqlQuery q(db);
    q.prepare(sql);
    for (auto it = binds.cbegin(); it != binds.cend(); ++it)
        q.bindValue(it.key(), it.value());
    return q.exec() && q.next() ? q.value(0).toString() : QString();
}

Check fail(Check c, const QString &reason) { c.status = Check::Fail; c.reason = reason; return c; }

} // namespace

QString newestBackup(const QString &backupDir)
{
    QStringList names = QDir(backupDir).entryList({ QStringLiteral("laideal_*.db") }, QDir::Files, QDir::Name);
    return names.isEmpty() ? QString() : QDir(backupDir).filePath(names.last());
}

QByteArray testSettings(const QByteArray &liveJson, const QString &dbCopy)
{
    const QJsonDocument doc = QJsonDocument::fromJson(liveJson);
    if (!doc.isObject())
        return {};
    QJsonObject root = doc.object();
    const auto set = [&root](const char *group, const char *key, const QJsonValue &value) {
        QJsonObject g = root.value(QLatin1String(group)).toObject();
        g.insert(QLatin1String(key), value);
        root.insert(QLatin1String(group), g);
    };
    set("database", "path", QDir::fromNativeSeparators(dbCopy));
    set("verifactu", "connection", QStringLiteral("aeat"));
    set("verifactu", "aeat_direct_production", false);
    set("verifactu", "service_key", QString());
    set("verifactu", "aeat_certificate_password", QString());
    set("backup", "enabled", false);
    return QJsonDocument(root).toJson();
}

Check checkSchema(QSqlDatabase &db)
{
    Check c{ QStringLiteral("M1"), QStringLiteral("Base de datos real: migraciones y numeración") };
    QStringList failures;
    {
        LogCapture capture;
        const bool migrated = migrateDatabase(db);
        for (const QString &m : capture.messages)
            if (m.contains(QLatin1String("failed"), Qt::CaseInsensitive))
                failures << m;
        if (!migrated && failures.isEmpty())
            failures << QStringLiteral("migrateDatabase");
    }
    if (!failures.isEmpty())
        return fail(c, QStringLiteral("%1 instrucción(es) de migración fallaron (ver el log)").arg(failures.size()));
    if (!db.open())
        return fail(c, QStringLiteral("no se pudo abrir la copia de la base de datos"));
    const QString integrity = scalar(db, QStringLiteral("PRAGMA integrity_check"));
    const int columns = db.record(QStringLiteral("ingresos")).count();
    const bool closures = !scalar(db, QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' "
                                                    "AND name = 'accounting_closures'")).isEmpty();
    const int highest = scalar(db, QStringLiteral("SELECT MAX(CAST(n_recibo AS INTEGER)) FROM ingresos")).toInt();
    db.close();
    if (integrity != QLatin1String("ok"))
        return fail(c, QStringLiteral("la comprobación de integridad del archivo no es correcta"));
    if (columns != INGRESOS_COL_VERIFACTU_CANCEL_XML + 1 || !closures)
        return fail(c, QStringLiteral("a la base de datos le faltan columnas o tablas"));
    bool numberRead = false;
    if (nextTicketNumber(db, 0, &numberRead) != highest + 1 || !numberRead)
        return fail(c, QStringLiteral("el siguiente número de ticket no es el último + 1"));
    c.status = Check::Pass;
    c.reason = QStringLiteral("migraciones sin errores, archivo íntegro, siguiente ticket = último + 1");
    return c;
}

Check checkData(QSqlDatabase &db, const QString &contabilidadPath, DataFacts &facts)
{
    Check c{ QStringLiteral("M2"), QStringLiteral("Datos reales: último ticket, cliente y trimestre cerrado") };
    facts = DataFacts();
    if (!db.open())
        return fail(c, QStringLiteral("no se pudo abrir la copia de la base de datos"));
    facts.lastTicket = scalar(db, QStringLiteral("SELECT n_recibo FROM ingresos ORDER BY CAST(n_recibo AS INTEGER) DESC LIMIT 1"));
    const int garments = scalar(db, QStringLiteral("SELECT COUNT(*) FROM ingresos WHERE n_recibo = :n"),
                                { { ":n", facts.lastTicket } }).toInt();
    const QString client = scalar(db, QStringLiteral("SELECT cliente FROM ingresos WHERE n_recibo = :n LIMIT 1"),
                                  { { ":n", facts.lastTicket } });
    // Recogida's search by client: a LIKE on ingresos.cliente.
    const bool foundByClient = !client.isEmpty()
        && scalar(db, QStringLiteral("SELECT COUNT(*) FROM ingresos WHERE cliente LIKE :c AND n_recibo = :n"),
                  { { ":c", "%" + client + "%" }, { ":n", facts.lastTicket } }).toInt() > 0;
    {
        QSqlQuery q(db);
        // Imprimir skips its own estado check when handed a QR: no other paid row of the
        // invoice may be cancelled, rectified or refused.
        q.prepare(QStringLiteral("SELECT n_recibo, verifactu_invoice_seq, verifactu_url_qr FROM ingresos i "
                                 "WHERE pagado = 'SI' AND verifactu_estado = 'ENVIADA' AND COALESCE(verifactu_url_qr, '') != '' "
                                 "  AND NOT EXISTS (SELECT 1 FROM ingresos o WHERE o.n_recibo = i.n_recibo "
                                 "    AND o.verifactu_invoice_seq = i.verifactu_invoice_seq AND o.pagado = 'SI' "
                                 "    AND COALESCE(o.estado, '') != 'Anulado' AND COALESCE(o.verifactu_estado, '') != 'ENVIADA') "
                                 "ORDER BY CAST(n_recibo AS INTEGER) DESC LIMIT 1"));
        if (q.exec() && q.next()) {
            facts.printableTicket = q.value(0).toString();
            facts.printableSeq = q.value(1).toInt();
            facts.printableQrUrl = q.value(2).toString();
        }
    }
    db.close();
    if (facts.lastTicket.isEmpty() || garments == 0)
        return fail(c, QStringLiteral("no se encuentra ningún ticket"));
    if (!foundByClient)
        return fail(c, QStringLiteral("la búsqueda por cliente no encuentra el último ticket"));

    QSqlQuery q(db);
    db.open();
    if (q.exec(QStringLiteral("SELECT year, quarter FROM accounting_closures WHERE reopened_at IS NULL "
                              "ORDER BY year DESC, quarter DESC LIMIT 1")) && q.next()) {
        facts.closedYear = q.value(0).toInt();
        facts.closedQuarter = q.value(1).toInt();
    }
    q.finish();
    db.close();
    if (facts.closedQuarter > 0) {
        QDate start, end;
        Contabilidad::periodRangeFor(Contabilidad::Trimestral, facts.closedQuarter, facts.closedYear, start, end);
        bool incomeRead = false, regsRead = false, expensesRead = false;
        const QVector<IncomeTicketDetail> income = incomeTicketsBetweenDates(db, start, end, &incomeRead);
        const QVector<RegularizationDetail> regs = regularizationsBetweenDates(db, start, end, &regsRead);
        const QVector<ExpenseDetail> expenses = expensesBetweenDates(db, start, end, &expensesRead);
        if (!incomeRead || !regsRead || !expensesRead)
            return fail(c, QStringLiteral("no se pudieron leer los datos del último trimestre cerrado"));
        const Contabilidad::PeriodFigures f = Contabilidad::figuresFromDetails(
            income, regs, expenses, AppSettings::ivaRate(), start, end);
        facts.income = f.ingImporte;
        facts.incomeIva = f.ingIva;
        facts.expenses = f.gastosImporteTotal();
        facts.ivaResult = f.resultadoIva();
        for (bool detail : { false, true }) {
            const QString file = contabilidadPath + Contabilidad::reportRelativePath(
                Contabilidad::Trimestral, facts.closedQuarter, facts.closedYear, detail);
            if (facts.reportFile.isEmpty() && QFileInfo::exists(file))
                facts.reportFile = file;
        }
    }
    c.status = Check::Pass;
    c.reason = facts.closedQuarter > 0
        ? QStringLiteral("último ticket y su cliente encontrados; cifras del último trimestre cerrado calculadas")
        : QStringLiteral("último ticket y su cliente encontrados; no hay ningún trimestre cerrado");
    return c;
}

bool invoiceStillSent(const QString &liveDb, const QString &nRecibo, int seq)
{
    const QString name = QStringLiteral("selfcheck_live");
    bool sent = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(liveDb);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (QFileInfo::exists(liveDb) && db.open()) {
            sent = scalar(db, QStringLiteral("SELECT COUNT(*) FROM ingresos WHERE n_recibo = :n AND pagado = 'SI' "
                                             "AND verifactu_invoice_seq = :s AND COALESCE(estado, '') != 'Anulado' "
                                             "AND COALESCE(verifactu_estado, '') != 'ENVIADA'"),
                          { { ":n", nRecibo }, { ":s", seq } }) == QLatin1String("0")
                && scalar(db, QStringLiteral("SELECT COUNT(*) FROM ingresos WHERE n_recibo = :n AND pagado = 'SI' "
                                             "AND verifactu_invoice_seq = :s AND verifactu_estado = 'ENVIADA'"),
                          { { ":n", nRecibo }, { ":s", seq } }).toInt() > 0;
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return sent;
}

QString reportText(const QVector<Check> &checks, const QString &version, const QDateTime &when)
{
    const auto status = [](Check::Status s) {
        switch (s) {
        case Check::Pass: return QStringLiteral("CORRECTO");
        case Check::Fail: return QStringLiteral("FALLO");
        case Check::Skip: return QStringLiteral("OMITIDO");
        default:          return QStringLiteral("SIN HACER");
        }
    };
    int failed = 0;
    QStringList lines;
    lines << QStringLiteral("Comprobación tras actualizar - La Ideal %1").arg(version)
          << QStringLiteral("Fecha: %1").arg(when.toString(QStringLiteral("dd-MM-yyyy HH:mm")))
          << QString();
    for (const Check &c : checks) {
        failed += c.status == Check::Fail || c.status == Check::Pending;
        lines << QStringLiteral("%1 %2: %3").arg(c.id, c.title, status(c.status));
        if (!c.reason.isEmpty())
            lines << QStringLiteral("    %1").arg(c.reason);
        if (!c.note.isEmpty())
            lines << QStringLiteral("    Nota: %1").arg(c.note);
    }
    lines << QString() << (failed == 0 ? QStringLiteral("Resultado: todo correcto.")
                                       : QStringLiteral("Resultado: %1 comprobación(es) con fallo o sin hacer.").arg(failed));
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace SelfCheck
