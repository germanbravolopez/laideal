#ifndef APPLOGGER_H
#define APPLOGGER_H

#include <QString>

class AppLogger
{
public:
    static void install();
    static QString logFilePath();
};

#endif // APPLOGGER_H
