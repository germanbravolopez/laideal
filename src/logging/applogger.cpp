#include "applogger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

namespace {
constexpr qint64 kDefaultMaxBytes = 5 * 1024 * 1024;

QFile s_logFile;
QTextStream s_logStream;
QMutex s_logMutex;
QString s_activePath;
qint64 s_maxBytes = kDefaultMaxBytes;
qint64 s_written = 0;   // bytes in the open file, counted from its size at open

bool openLog(const QString &path)
{
    s_logStream.setDevice(nullptr);
    if (s_logFile.isOpen())
        s_logFile.close();
    s_logFile.setFileName(path);
    if (!s_logFile.open(QIODevice::Append | QIODevice::Text))
        return false;
    s_logStream.setDevice(&s_logFile);
    s_written = s_logFile.size();
    return true;
}

// Called under the mutex. The previous ".old" is only replaced once the current file
// has been moved aside; when that fails (file held open elsewhere) logging carries on
// in the same file and the next attempt waits for another full limit.
void rotateLocked()
{
    const QString path = s_activePath;
    s_logStream.flush();
    s_logFile.close();
    const QString moved = path + ".rotating";
    QFile::remove(moved);   // leftover of an interrupted rotation
    if (QFile::rename(path, moved)) {
        QFile::remove(path + ".old");
        if (!QFile::rename(moved, path + ".old"))
            QFile::remove(moved);
    }
    if (!openLog(path))
        s_logStream.setDevice(nullptr);
    else if (s_written >= s_maxBytes)
        s_written = 0;   // rename failed: do not retry on every message
}

void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    {
        QMutexLocker locker(&s_logMutex);
        if (s_logStream.device()) {
            const char *tag = "DEBUG";
            switch (type) {
            case QtWarningMsg:  tag = "WARN";  break;
            case QtCriticalMsg: tag = "CRIT";  break;
            case QtFatalMsg:    tag = "FATAL"; break;
            default: break;
            }
            const QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
                                 + " [" + tag + "] " + msg + "\n";
            s_logStream << line;
            s_logStream.flush();
            s_written += line.toUtf8().size();
            if (s_written > s_maxBytes)
                rotateLocked();
        }
    }
    if (type == QtFatalMsg)
        abort();
}
} // namespace

QString AppLogger::logFilePath()
{
    return s_activePath.isEmpty() ? QDir::homePath() + "/.laideal.log" : s_activePath;
}

bool AppLogger::installAt(const QString &path, qint64 maxBytes)
{
    QMutexLocker locker(&s_logMutex);
    s_maxBytes = maxBytes;
    // Rotate a log that is already too large before this session appends to it.
    if (QFileInfo::exists(path) && QFileInfo(path).size() > s_maxBytes) {
        s_activePath = path;
        if (openLog(path))
            rotateLocked();
    }
    if (!openLog(path))
        return false;
    s_activePath = path;
    s_logStream << "\n=== Session started: "
                << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
                << " ===\n";
    s_logStream.flush();
    qInstallMessageHandler(messageHandler);
    return true;
}

void AppLogger::uninstall()
{
    qInstallMessageHandler(nullptr);
    QMutexLocker locker(&s_logMutex);
    s_logStream.setDevice(nullptr);
    if (s_logFile.isOpen())
        s_logFile.close();
    s_activePath.clear();
}

void AppLogger::install()
{
    if (installAt(QDir::homePath() + "/.laideal.log", kDefaultMaxBytes))
        return;
    // Read-only or redirected home: keep a log somewhere writable rather than none.
    installAt(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/laideal.log",
              kDefaultMaxBytes);
}
