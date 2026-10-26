#ifndef SELFCHECK_H
#define SELFCHECK_H

// The post-update self-check (`laideal.exe --self-check`), run by the shop user after
// installing a release: the checks of docs/testing/smoke_test.md that need the shop's
// real data and printer, against a copy of the newest backup with a copy of the
// settings (backups off, no certificate password). The live database is only read,
// and so is ~/.laideal_settings.json, except that AppSettings::load() may rewrite it (a
// legacy plaintext key is re-encrypted, a missing file is created). Nothing reaches AEAT.
// The pure parts live here so the suites can test them; SelfCheckDialog runs them and
// asks the guided questions.

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

namespace SelfCheck {

struct Check {
    enum Status { Pending, Pass, Fail, Skip };
    QString id;          // "M1", "M2", ...
    QString title;       // shown and reported
    Status status = Pending;
    QString reason;      // shown and reported: never shop data (no names, numbers, amounts)
    QString note;        // the user's own note on a guided check
};

// The newest `laideal_*.db` in the backup folder (the names sort by time); empty if none.
QString newestBackup(const QString &backupDir);

// The settings copy the check runs with: the live settings with the database pointing at
// `dbCopy`, the direct AEAT client in pre-production and no gateway key, so a stray
// request could only reach AEAT's test environment. Empty when `liveJson` is not JSON.
QByteArray testSettings(const QByteArray &liveJson, const QString &dbCopy);

// M1: the migrations run on the copy without a failed statement, the file passes
// PRAGMA integrity_check, ingresos has every column, and the next ticket number is the
// highest stored + 1.
Check checkSchema(QSqlDatabase &db);

// M1d: running the migrations a second time changes nothing (schema and every row stay
// the same) and reports no failed statement.
Check checkMigrationsTwice(QSqlDatabase &db);

// M1a: the settings file is readable JSON and the database path it names exists.
Check checkSettings(const QString &settingsFile, const QString &dbPath);

// M1b: the newest backup (its name carries the time) is within BackupManager's warning
// window, 3 days, of `now`. The reason names only the age.
Check checkBackupAge(const QString &backupFile, const QDateTime &now);

// M1e: ingresos rows whose importe is not a readable amount (moneyParse) and gastos whose
// IVA is not a recognised rate (expenseIvaRate = -1). Fails when any; the reason has counts only.
Check checkMoney(QSqlDatabase &db);

// M1c: every stored AEAT record of both environments matches its huella and links to the
// previous one (AeatChainCheck): an alteration of aeat_records is detected.
Check checkChain(QSqlDatabase &db);

// True when the live database (opened read-only) still holds invoice (n_recibo, seq) as
// sent: the backup can be a day old, and a since-cancelled invoice must not be reprinted.
bool invoiceStillSent(const QString &liveDb, const QString &nRecibo, int seq);

// Facts M2 / M3 need from the copy; shown on screen only, never reported.
struct DataFacts {
    QString lastTicket;              // highest n_recibo
    QString printableTicket;         // the newest ticket with an invoice AEAT holds, every paid row ENVIADA
    int printableSeq = 0;            // its invoice's verifactu_invoice_seq
    QString printableQrUrl;          // that invoice's stored verification URL
    int closedQuarter = 0, closedYear = 0;   // the newest closed quarter (0 when none)
    double income = 0.0, incomeIva = 0.0, expenses = 0.0, ivaResult = 0.0;
    QString reportFile;              // its filed report, if the PDF is there
};

// M2: the newest ticket loads with its garments and a search by its client finds it,
// and the newest closed quarter's figures are recomputed (without closing anything).
Check checkData(QSqlDatabase &db, const QString &contabilidadPath, DataFacts &facts);

// The report the user sends back: version, date and one line per check (status, reason,
// note). It carries no shop data beyond what the user typed in a note.
QString reportText(const QVector<Check> &checks, const QString &version, const QDateTime &when);
// The first start of a newer version (never a first installation): the moment to ask for the check.
inline bool isFirstStartAfterUpdate(const QString &lastStarted, const QString &current)
{
    return !lastStarted.isEmpty() && lastStarted != current;
}

} // namespace SelfCheck

#endif // SELFCHECK_H
