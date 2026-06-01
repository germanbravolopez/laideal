#include "backup_manager.h"

#include "appsettings.h"
#include "sql_lite.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {
// Connection names are unique so the backup never piggy-backs on the app's
// main connection (which would interfere with QSqlDatabase::open/close
// cycles in sql_lite helpers).
constexpr const char *kSnapshotConnectionName = "laideal_backup_snapshot";
constexpr const char *kVerifyConnectionName   = "laideal_backup_verify";

// Daily-for-30-days + monthly-for-4-years window. Older copies are removed
// by pruneOldBackups().
constexpr int kKeepEveryDays   = 30;
constexpr int kKeepMonthlyYears = 4;

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
    return lastTime.secsTo(QDateTime::currentDateTime()) >= minIntervalSeconds;
}

BackupManager::Result BackupManager::performBackup()
{
    Result res;
    const QString sourcePath = dbPath();
    if (sourcePath.isEmpty() || !QFile::exists(sourcePath)) {
        res.errorMessage = tr("No se encuentra la base de datos en %1.").arg(sourcePath);
        qWarning() << "BackupManager: source DB not found at" << sourcePath;
        return res;
    }

    const QString dir = backupDirectory();
    const QString target = QDir(dir).filePath(timestampedFileName());

    // Step 1: VACUUM INTO produces a transactionally-consistent point-in-time
    // copy without requiring exclusive access on the live DB.
    {
        QSqlDatabase snap = QSqlDatabase::addDatabase("QSQLITE", kSnapshotConnectionName);
        snap.setDatabaseName(sourcePath);
        if (!snap.open()) {
            res.errorMessage = tr("No se pudo abrir la base de datos para copia: %1")
                                   .arg(snap.lastError().text());
            qWarning() << "BackupManager: open(snapshot) failed -" << snap.lastError().text();
            QSqlDatabase::removeDatabase(kSnapshotConnectionName);
            return res;
        }
        QSqlQuery q(snap);
        // VACUUM INTO path is a string literal in SQLite syntax (cannot bind);
        // escape single quotes by doubling them.
        QString escaped = target;
        escaped.replace("'", "''");
        if (!q.exec(QString("VACUUM INTO '%1'").arg(escaped))) {
            res.errorMessage = tr("No se pudo crear la copia: %1").arg(q.lastError().text());
            qWarning() << "BackupManager: VACUUM INTO failed -" << q.lastError().text();
            snap.close();
            QSqlDatabase::removeDatabase(kSnapshotConnectionName);
            QFile::remove(target);
            return res;
        }
        snap.close();
    }
    QSqlDatabase::removeDatabase(kSnapshotConnectionName);

    // Step 2: open the copy read-only and run PRAGMA integrity_check. SQLite's
    // own check is the canonical way to certify a backup file before counting
    // it toward the retention window.
    {
        QSqlDatabase ver = QSqlDatabase::addDatabase("QSQLITE", kVerifyConnectionName);
        ver.setDatabaseName(target);
        ver.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (!ver.open()) {
            res.errorMessage = tr("No se pudo verificar la copia: %1").arg(ver.lastError().text());
            qWarning() << "BackupManager: open(verify) failed -" << ver.lastError().text();
            QSqlDatabase::removeDatabase(kVerifyConnectionName);
            QFile::remove(target);
            return res;
        }
        QSqlQuery q(ver);
        if (!q.exec("PRAGMA integrity_check") || !q.next()) {
            res.errorMessage = tr("Fallo al verificar la copia: %1")
                                   .arg(errorIfNotOk(q, "integrity_check"));
            qWarning() << "BackupManager: integrity_check exec failed -" << q.lastError().text();
            ver.close();
            QSqlDatabase::removeDatabase(kVerifyConnectionName);
            QFile::remove(target);
            return res;
        }
        const QString verdict = q.value(0).toString();
        ver.close();
        QSqlDatabase::removeDatabase(kVerifyConnectionName);
        if (verdict != "ok") {
            res.errorMessage = tr("La copia parece estar corrupta (PRAGMA integrity_check: %1).")
                                   .arg(verdict);
            qWarning() << "BackupManager: integrity_check verdict =" << verdict;
            QFile::remove(target);
            return res;
        }
    }

    res.success      = true;
    res.backupPath   = target;
    res.bytesWritten = QFileInfo(target).size();

    AppSettings::instance()->setBackupLastTime(
        QDateTime::currentDateTime().toString(Qt::ISODate));
    AppSettings::instance()->save();

    const int pruned = pruneOldBackups();
    qDebug() << "BackupManager: backup completed -" << target
             << "(" << res.bytesWritten << "bytes, pruned" << pruned << "old copies)";
    return res;
}

int BackupManager::pruneOldBackups()
{
    QDir dir(backupDirectory());
    if (!dir.exists()) return 0;

    // Filename schema: laideal_yyyy-MM-dd_HHmmss.db (timestampedFileName()).
    static const QRegularExpression namePattern(
        R"(^laideal_(\d{4})-(\d{2})-(\d{2})_(\d{2})(\d{2})(\d{2})\.db$)");

    const QFileInfoList entries = dir.entryInfoList(
        {"laideal_*.db"}, QDir::Files, QDir::Name);
    const QDateTime now = QDateTime::currentDateTime();

    // Group monthly-window survivors by "yyyy-MM" so we can keep one per month.
    QHash<QString, QString> monthlyKept;
    int deleted = 0;
    for (const QFileInfo &fi : entries) {
        const auto m = namePattern.match(fi.fileName());
        if (!m.hasMatch()) continue;
        const QDateTime ts(QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()),
                           QTime(m.captured(4).toInt(), m.captured(5).toInt(), m.captured(6).toInt()));
        if (!ts.isValid()) continue;

        const qint64 ageDays = ts.daysTo(now);
        if (ageDays < kKeepEveryDays) {
            continue; // recent: keep every copy
        }
        if (ageDays > kKeepMonthlyYears * 365) {
            if (QFile::remove(fi.absoluteFilePath())) ++deleted;
            continue;
        }
        // Monthly window: keep the first survivor per (year, month), delete the rest.
        const QString monthKey = ts.toString("yyyy-MM");
        if (!monthlyKept.contains(monthKey)) {
            monthlyKept.insert(monthKey, fi.absoluteFilePath());
        } else {
            if (QFile::remove(fi.absoluteFilePath())) ++deleted;
        }
    }
    return deleted;
}
