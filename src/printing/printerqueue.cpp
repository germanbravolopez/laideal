#include "printerqueue.h"

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <winspool.h>
#  include <vector>
#endif

QString PrinterQueue::defaultName()
{
#ifdef Q_OS_WIN
    DWORD len = 0;
    GetDefaultPrinterW(nullptr, &len);   // first call asks for the buffer size
    if (len == 0) return QString();
    std::vector<wchar_t> buf(len);
    if (!GetDefaultPrinterW(buf.data(), &len)) return QString();
    return QString::fromWCharArray(buf.data());
#else
    return QString();
#endif
}
