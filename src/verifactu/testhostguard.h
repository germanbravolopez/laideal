#ifndef TESTHOSTGUARD_H
#define TESTHOSTGUARD_H

// Tripwire for the test suites: with LAIDEAL_TEST=1 (set by CTest for every test) the
// AEAT and gateway transports refuse any host but this machine, so a test whose fake
// server override is missing can never reach AEAT or IreneSolutions.

#include <QDebug>
#include <QUrl>
#include <QtGlobal>

namespace TestHostGuard {

// False only under LAIDEAL_TEST=1 for a host other than localhost / 127.0.0.1 / ::1.
inline bool allows(const QUrl &url)
{
    if (qEnvironmentVariable("LAIDEAL_TEST") != QLatin1String("1"))
        return true;
    const QString host = url.host().toLower();
    if (host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("::1"))
        return true;
    qCritical() << "TestHostGuard: LAIDEAL_TEST=1 refuses a request to" << url.host();
    return false;
}

} // namespace TestHostGuard

#endif // TESTHOSTGUARD_H
