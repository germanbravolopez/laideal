#include "applogger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

static QFile s_logFile;
static QTextStream s_logStream;
static QMutex s_logMutex;

static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    QMutexLocker locker(&s_logMutex);

    const char *tag = "DEBUG";
    switch (type) {
    case QtWarningMsg:  tag = "WARN";  break;
    case QtCriticalMsg: tag = "CRIT";  break;
    case QtFatalMsg:    tag = "FATAL"; break;
    default: break;
    }

    s_logStream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
                << " [" << tag << "] " << msg << "\n";
    s_logStream.flush();

    if (type == QtFatalMsg)
        abort();
}

QString AppLogger::logFilePath()
{
    return QDir::homePath() + "/.laideal.log";
}

void AppLogger::install()
{
    const QString path = logFilePath();

    // Rotate when log exceeds 5 MB so the file stays manageable
    if (QFileInfo::exists(path) && QFileInfo(path).size() > 5 * 1024 * 1024) {
        QFile::remove(path + ".old");
        QFile::rename(path, path + ".old");
    }

    s_logFile.setFileName(path);
    if (!s_logFile.open(QIODevice::Append | QIODevice::Text))
        return;

    s_logStream.setDevice(&s_logFile);
    s_logStream << "\n=== Session started: "
                << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
                << " ===\n";
    s_logStream.flush();

    qInstallMessageHandler(messageHandler);
}
