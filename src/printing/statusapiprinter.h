#ifndef STATUSAPIPRINTER_H
#define STATUSAPIPRINTER_H

#include <QByteArray>
#include <QString>

#include "printerstatus.h"

// Optional transport that sends the same ESC/POS byte stream as ThermalPrinter
// but through Epson's Status API DLL (EPSStmApi.dll), which can also read the
// device's ASB status (paper out / near end, cover open, cutter error). It is
// used only when the print.use_status_api setting is on; otherwise the RAW
// spooler path (ThermalPrinter::send) is used.
//
// The DLL is loaded dynamically (LoadLibrary/GetProcAddress) so there is no
// build-time dependency on the Epson SDK and a machine without the APD/Status
// API installed simply gets a clean failure - the caller can then fall back to
// the RAW path. See docs/modules/printer/02_control_methods.md (option B).
class StatusApiPrinter
{
public:
    // Sends escpos via the Status API and reads the device status into *status.
    //
    // Returns true if the bytes were sent (the status read is best-effort:
    // status->valid is false if BiGetStatus failed, but the function still
    // returns true because the data was transmitted). Returns false and fills
    // *err - and sends nothing - if the DLL/API is unavailable or the printer
    // could not be opened, so the caller can safely fall back to RAW without
    // double-printing. An empty queueName uses the Windows default printer.
    static bool sendAndReadStatus(const QByteArray &escpos, const QString &queueName,
                                  PrinterStatus *status, QString *err = nullptr);

    // Same as sendAndReadStatus() but runs the (potentially blocking) Epson DLL
    // calls on a detached worker thread and waits at most timeoutMs. If the DLL
    // does not return in time - e.g. a printer firmware/driver quirk hangs
    // BiOpenMonPrinter/BiDirectIOEx - it abandons the worker and returns false
    // with an invalid *status, so the UI thread never freezes and the caller
    // falls back to RAW. After one timeout the Status API is disabled for the
    // rest of the process (subsequent calls return immediately) so every later
    // print is not delayed by the same hang. Call this from the UI thread.
    static bool sendAndReadStatusBounded(const QByteArray &escpos, const QString &queueName,
                                         PrinterStatus *status, QString *err, int timeoutMs);
};

#endif // STATUSAPIPRINTER_H
