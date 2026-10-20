#include "backup_manager.h"

#include "appsettings.h"
#include "sql_lite.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVector>

#include <algorithm>
#include <atomic>

namespace {
// Connection names are unique so the backup never piggy-backs on the app's
// main connection (which would interfere with QSqlDatabase::open/close
// cycles in sql_lite helpers).
constexpr const char *kSnapshotConnectionName = "laideal_backup_snapshot";
constexpr const char *kVerifyConnectionName   = "laideal_backup_verify";
constexpr const char *kPartSuffix             = ".part";

// One backup at a time in the process: the automatic one runs on a worker thread
// and the manual one on the GUI thread, and both use the connection names above.
std::atomic<bool> s_running{ false };

// Daily-for-30-days + monthly-for-4-years window. Older copies are removed
// by pruneOldBackups().
constexpr int kKeepEveryDays   = 30;
constexpr int kKeepMonthlyYears = 4;
constexpr int kKeepNewest       = 3;    // never pruned, whatever their age
constexpr int kMaxClockGapDays  = 45;   // a larger gap to "now" means the clock is suspect
constexpr int kWarnAfterDays    = 3;    // last successful backup older than this is reported

QString errorIfNotOk(QSqlQuery &q, const QString &context)
{
    if (q.lastError().isValid())
        return QString("%1: %2").arg(context, q.lastError().text());
    return QString();
}
} // namespace

BackupManager::BackupManager(QObject *parent) : QObject(parent) {}

QString BackupManager::backupDirectory() const
{
    // backups/ subdir next to the live DB - keeps snapshots adjacent to the
    // file they protect but separated from it so listing/cleanup is
    // unambiguous. mkpath() is idempotent and creates the subdir on first run.
    QString parent = QFileInfo(dbPath()).absolutePath();
    if (parent.isEmpty())
        parent = QDir::homePath();
    const QString dir = QDir(parent).filePath("backups");
    QDir().mkpath(dir);
    return dir;
}

QString BackupManager::timestampedFileName()
{
    // Same .db extension as the live DB so the operator sees a uniform set
    // of files. The timestamp prefix sorts lexicographically by time, which
    // keeps the pruner trivial.
    return QString("laideal_%1.db")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss"));
}

void BackupManager::removeStalePartFiles(const QString &dir)
{
    // Left by a run that was cut off (crash, power cut); none is being written now.
    QDir d(dir);
    for (const QString &name : d.entryList({ QStringLiteral("laideal_*.db") + kPartSuffix }, QDir::Files))
        if (QFile::remove(d.filePath(name)))
            qWarning() << "BackupManager: removed an unfinished copy" << name;
}

bool BackupManager::needsBackup(qint64 minIntervalSeconds) const
{
    if (!AppSettings::instance()->backupEnabled())
        return false;
    const QString last = AppSettings::instance()->backupLastTime();
    if (last.isEmpty())
        return true;
    const QDateTime lastTime = QDateTime::fromString(last, Qt::ISODate);
    if (!lastTime.isValid())
        return true;
    // A last time in the future (the clock was wrong then) must not stop backups.
    const qint64 elapsed = lastTime.secsTo(QDateTime::currentDateTime());
    return elapsed < 0 || elapsed >= minIntervalSeconds;
}

void BackupManager::recordSuccess(const Result &result)
{
    if (!result.success)
        return;
    AppSettings::instance()->setBackupLastTime(result.finishedAt.toString(Qt::ISODate));
    AppSettings::instance()->setBackupLastError(QString());
    if (!AppSettings::instance()->save())
        qWarning() << "BackupManager: could not save the backup time to the settings";
}

void BackupManager::recordFailure(const Result &result)
{
    if (result.success)
        return;
    AppSettings::instance()->setBackupLastError(
        QDateTime::currentDateTime().toString(Qt::ISODate) + QLatin1Char(' ') + result.errorMessage);
    if (!AppSettings::instance()->save())
        qWarning() << "BackupManager: could not save the backup error to the settings";
}

QString BackupManager::backupWarning(bool enabled, const QString &lastSuccessIso,
                                     const QString &lastError, const QDateTime &now)
{
    if (!enabled)
        return tr("Las copias de seguridad automáticas están desactivadas.");
    const QDateTime last = QDateTime::fromString(lastSuccessIso, Qt::ISODate);
    const bool tooOld = !last.isValid() || last.addDays(kWarnAfterDays) < now;
    if (!tooOld)
        return QString();
    QString msg = last.isValid()
        ? tr("La última copia de seguridad correcta es del %1 (hace más de %2 días).")
              .arg(last.toString("dd/MM/yyyy HH:mm")).arg(kWarnAfterDays)
        : tr("Todavía no se ha hecho ninguna copia de seguridad correcta.");
    if (!lastError.isEmpty())
        msg += tr(" Último error: %1").arg(lastError);
    return msg;
}

QString BackupManager::currentBackupWarning()
{
    AppSettings *s = AppSettings::instance();
    return backupWarning(s->backupEnabled(), s->backupLastTime(), s->backupLastError(),
                         QDateTime::currentDateTime());
}

BackupManager::Result BackupManager::performBackup()
{
    Result res;
    if (s_running.exchange(true)) {
        res.errorMessage = tr("Ya hay una copia de seguridad en curso; espere a que termine.");
        qWarning() << "BackupManager: a backup is already running";
        return res;
    }
    struct Release { ~Release() { s_running = false; } } release;

    const QString sourcePath = dbPath();
    if (sourcePath.isEmpty() || !QFile::exists(sourcePath)) {
        res.errorMessage = tr("No se encuentra la base de datos en %1.").arg(sourcePath);
        qWarning() << "BackupManager: source DB not found at" << sourcePath;
        return res;
    }

    const QString dir = backupDirectory();
    removeStalePartFiles(dir);
    const QString finalPath = QDir(dir).filePath(timestampedFileName());
    if (QFile::exists(finalPath)) {
        // Same second as the previous copy: that one is kept, never overwritten.
        res.errorMessage = tr("Ya se ha hecho una copia hace un momento (%1).").arg(QFileInfo(finalPath).fileName());
        qWarning() << "BackupManager: a copy with this timestamp already exists -" << finalPath;
        return res;
    }
    const QString target = finalPath + kPartSuffix;

    // Step 1: VACUUM INTO produces a transactionally-consistent point-in-time
    // copy without requiring exclusive access on the live DB.
    // Do the work in an inner block so the QSqlDatabase + QSqlQuery are out of
    // scope before removeDatabase() below - otherwise Qt warns the connection is
    // still in use (same reason as the Step 2 verify block).
    bool snapshotOk = false;
    {
        QSqlDatabase snap = QSqlDatabase::addDatabase("QSQLITE", kSnapshotConnectionName);
        snap.setDatabaseName(sourcePath);
        snap.setConnectOptions("QSQLITE_BUSY_TIMEOUT=30000");   // wait out a ticket being saved
        if (!snap.open()) {
            res.errorMessage = tr("No se pudo abrir la base de datos para copia: %1")
                                   .arg(snap.lastError().text());
            qWarning() << "BackupManager: open(snapshot) failed -" << snap.lastError().text();
        } else {
            QSqlQuery q(snap);
            // VACUUM INTO path is a string literal in SQLite syntax (cannot bind);
            // escape single quotes by doubling them.
            QString escaped = target;
            escaped.replace("'", "''");
            if (!q.exec(QString("VACUUM INTO '%1'").arg(escaped))) {
                res.errorMessage = tr("No se pudo crear la copia: %1").arg(q.lastError().text());
                qWarning() << "BackupManager: VACUUM INTO failed -" << q.lastError().text();
            } else {
                snapshotOk = true;
            }
            snap.close();
        }
    }
    QSqlDatabase::removeDatabase(kSnapshotConnectionName);
    if (!snapshotOk) {
        QFile::remove(target);
        return res;
    }

    // Step 2: open the copy read-only and run PRAGMA integrity_check. SQLite's
    // own check is the canonical way to certify a backup file before counting
    // it toward the retention window.
    // The QSqlDatabase and its QSqlQuery must both be out of scope before
    // removeDatabase(), or Qt warns "connection is still in use" on every backup.
    // So do all the work in this inner block, capture the outcome, and remove the
    // connection afterwards (mirrors the Step 1 snapshot pattern above).
    bool verifyOk = false;
    {
        QSqlDatabase ver = QSqlDatabase::addDatabase("QSQLITE", kVerifyConnectionName);
        ver.setDatabaseName(target);
        ver.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (!ver.open()) {
            res.errorMessage = tr("No se pudo verificar la copia: %1").arg(ver.lastError().text());
            qWarning() << "BackupManager: open(verify) failed -" << ver.lastError().text();
        } else {
            QSqlQuery q(ver);
            if (!q.exec("PRAGMA integrity_check") || !q.next()) {
                res.errorMessage = tr("Fallo al verificar la copia: %1")
                                       .arg(errorIfNotOk(q, "integrity_check"));
                qWarning() << "BackupManager: integrity_check exec failed -" << q.lastError().text();
            } else {
                const QString verdict = q.value(0).toString();
                if (verdict == "ok") {
                    verifyOk = true;
                } else {
                    res.errorMessage = tr("La copia parece estar corrupta (PRAGMA integrity_check: %1).")
                                           .arg(verdict);
                    qWarning() << "BackupManager: integrity_check verdict =" << verdict;
                }
            }
            ver.close();
        }
    }
    QSqlDatabase::removeDatabase(kVerifyConnectionName);
    if (!verifyOk) {
        QFile::remove(target);
        return res;
    }
    if (!QFile::rename(target, finalPath)) {
        res.errorMessage = tr("No se pudo dar el nombre final a la copia %1.").arg(QFileInfo(finalPath).fileName());
        qWarning() << "BackupManager: rename" << target << "->" << finalPath << "failed";
        QFile::remove(target);
        return res;
    }

    res.success      = true;
    res.backupPath   = finalPath;
    res.bytesWritten = QFileInfo(finalPath).size();
    res.finishedAt   = QDateTime::currentDateTime();

    const int pruned = pruneOldBackups();
    copyToSecondFolder(res);
    qDebug() << "BackupManager: backup completed -" << finalPath
             << "(" << res.bytesWritten << "bytes, pruned" << pruned << "old copies)";
    return res;
}

void BackupManager::copyToSecondFolder(Result &res) const
{
    if (m_secondFolder.trimmed().isEmpty())
        return;
    // Same name, written as .part and renamed: a cut-off copy never looks like a backup there.
    const QDir dir(m_secondFolder.trimmed());
    const QString name = QFileInfo(res.backupPath).fileName();
    const QString part = dir.filePath(name + kPartSuffix);
    if (!QDir().mkpath(dir.absolutePath())) {
        res.secondCopyError = tr("No se pudo usar la segunda carpeta de copias %1.").arg(dir.absolutePath());
    } else {
        removeStalePartFiles(dir.absolutePath());
        QFile::remove(part);
        if (!QFile::copy(res.backupPath, part) || QFileInfo(part).size() != res.bytesWritten
            || !QFile::rename(part, dir.filePath(name))) {
            QFile::remove(part);
            res.secondCopyError = tr("No se pudo copiar la copia de seguridad a la segunda carpeta %1.").arg(dir.absolutePath());
        } else {
            res.secondCopyPath = dir.filePath(name);
            QStringList names = dir.entryList({ QStringLiteral("laideal_*.db") }, QDir::Files, QDir::Name);
            for (const QString &old : backupsToPrune(names, QDateTime::currentDateTime()))
                QFile::remove(dir.filePath(old));
        }
    }
    if (!res.secondCopyError.isEmpty())
        qWarning() << "BackupManager:" << res.secondCopyError;
}

QStringList BackupManager::backupsToPrune(const QStringList &fileNames, const QDateTime &now)
{
    // Filename schema: laideal_yyyy-MM-dd_HHmmss.db (timestampedFileName()).
    static const QRegularExpression namePattern(
        R"(^laideal_(\d{4})-(\d{2})-(\d{2})_(\d{2})(\d{2})(\d{2})\.db$)");

    struct Copy { QString name; QDateTime ts; };
    QVector<Copy> copies;
    for (const QString &name : fileNames) {
        const auto m = namePattern.match(name);
        if (!m.hasMatch()) continue; // not one of ours: ignore (keep)
        const QDateTime ts(QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()),
                           QTime(m.captured(4).toInt(), m.captured(5).toInt(), m.captured(6).toInt()));
        if (ts.isValid())
            copies.append({ name, ts });
    }
    // Newest first.
    std::sort(copies.begin(), copies.end(), [](const Copy &a, const Copy &b) {
        return a.ts != b.ts ? a.ts > b.ts : a.name > b.name;
    });
    if (copies.isEmpty())
        return {};

    // A wrong clock must not decide what is deleted: a "now" before the newest copy
    // or long after it, or a jump between the two newest copies, prunes nothing.
    const QDateTime newest = copies.first().ts;
    if (now < newest || newest.addDays(kMaxClockGapDays) < now)
        return {};
    if (copies.size() > 1 && copies.at(1).ts.addDays(kMaxClockGapDays) < newest)
        return {};

    QSet<QString> monthlyKept; // "yyyy-MM" already has a survivor (the last copy of the month)
    QStringList toDelete;
    for (int i = 0; i < copies.size(); ++i) {
        const Copy &c = copies.at(i);
        if (i < kKeepNewest)
            continue; // always keep the newest copies
        if (c.ts.addDays(kKeepEveryDays) > now)
            continue; // recent: keep every copy
        if (c.ts.addYears(kKeepMonthlyYears) < now) {
            toDelete << c.name; // beyond the retention window
            continue;
        }
        // Monthly window, newest first: the first copy seen of a month is its last one.
        const QString monthKey = c.ts.toString("yyyy-MM");
        if (!monthlyKept.contains(monthKey))
            monthlyKept.insert(monthKey);
        else
            toDelete << c.name;
    }
    toDelete.sort();
    return toDelete;
}

int BackupManager::pruneOldBackups()
{
    QDir dir(backupDirectory());
    if (!dir.exists()) return 0;

    const QStringList names = dir.entryList({"laideal_*.db"}, QDir::Files, QDir::Name);
    const QStringList toDelete = backupsToPrune(names, QDateTime::currentDateTime());

    int deleted = 0;
    for (const QString &name : toDelete) {
        if (QFile::remove(dir.filePath(name))) ++deleted;
    }
    return deleted;
}
