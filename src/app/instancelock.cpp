#include "instancelock.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

namespace InstanceLock {

QString lockPath(const QString &dbPath)
{
    const QString key = QDir::cleanPath(QFileInfo(QDir::fromNativeSeparators(dbPath)).absoluteFilePath()).toLower();
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return QDir(QDir::tempPath()).filePath(QStringLiteral("laideal-%1.lock").arg(QString::fromLatin1(hash)));
}

std::unique_ptr<QLockFile> acquire(const QString &dbPath, int waitMs)
{
    auto lock = std::make_unique<QLockFile>(lockPath(dbPath));
    lock->setStaleLockTime(0);   // stale only when its process is gone, never by age
    if (lock->tryLock(waitMs))
        return lock;
    qint64 pid = 0;
    QString host, app;
    lock->getLockInfo(&pid, &host, &app);
    qWarning() << "InstanceLock: the database" << dbPath << "is already open (pid" << pid << ")";
    return nullptr;
}

} // namespace InstanceLock
