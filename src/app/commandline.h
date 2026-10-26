#ifndef COMMANDLINE_H
#define COMMANDLINE_H

// The switches of main.cpp that need a value.

#include <QStringList>

namespace CommandLine {

// The value after `name` in `args` (args[0] is the program). `dangling` is set when the
// switch is the last argument: it must not fall back to the shop's own settings.
inline QString switchValue(const QStringList &args, const QString &name, bool *dangling = nullptr)
{
    const int at = args.indexOf(name);
    if (dangling)
        *dangling = at > 0 && at + 1 >= args.size();
    return at > 0 && at + 1 < args.size() ? args.at(at + 1) : QString();
}

} // namespace CommandLine

#endif // COMMANDLINE_H
