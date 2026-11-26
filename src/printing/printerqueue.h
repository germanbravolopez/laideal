#ifndef PRINTERQUEUE_H
#define PRINTERQUEUE_H

#include <QString>

// Internal to the printing library: shared by the RAW spooler and the Status API paths.
namespace PrinterQueue {
// The Windows default printer name, or empty if none is set (always empty off Windows).
QString defaultName();
}

#endif // PRINTERQUEUE_H
