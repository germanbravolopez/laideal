#ifndef APPLOGGER_H
#define APPLOGGER_H

#include <QString>

class AppLogger
{
public:
    // Installs the file handler at logFilePath(); falls back to the temp folder when
    // the home folder log cannot be opened. Without any writable file nothing is installed.
    static void install();
    // The log file in use (the home path until install() had to fall back).
    static QString logFilePath();

    // Test seams: install at an explicit path with a size limit, and detach again.
    static bool installAt(const QString &path, qint64 maxBytes = 5 * 1024 * 1024);
    static void uninstall();
};

#endif // APPLOGGER_H
