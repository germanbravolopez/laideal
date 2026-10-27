#ifndef STATUSAPIPRINTER_H
#define STATUSAPIPRINTER_H

#include <QByteArray>
#include <QString>

#include <atomic>

#include "printerstatus.h"

// Hand-over between the Status API worker and the watchdog that bounds it: exactly one
// side wins. Either the worker starts the send (the watchdog can then no longer promise
// the bytes will not come out) or the watchdog gives up first (the worker must never send).
class StatusApiSendGate
{
public:
    // Worker, right before the DLL send. False when the watchdog already gave up: do not send.
    bool beginSend()
    {
        int expected = Idle;
        return m_state.compare_exchange_strong(expected, Sending);
    }
    // Watchdog, on timeout. True when the worker had not started sending and now never will,
    // so the caller may safely print the same bytes by another route.
    bool abandon()
    {
        int expected = Idle;
        return m_state.compare_exchange_strong(expected, Abandoned);
    }

private:
    enum State { Idle, Sending, Abandoned };
    std::atomic<int> m_state{Idle};
};

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
    // With a gate, the send is skipped (false) when the gate was abandoned first.
    static bool sendAndReadStatus(const QByteArray &escpos, const QString &queueName,
                                  PrinterStatus *status, QString *err = nullptr,
                                  StatusApiSendGate *gate = nullptr);

    // Same as sendAndReadStatus() but runs the (potentially blocking) Epson DLL
    // calls on a detached worker thread and waits at most timeoutMs. If the DLL
    // does not return in time - e.g. a printer firmware/driver quirk hangs
    // BiOpenMonPrinter/BiDirectIOEx - it abandons the worker and returns false
    // with an invalid *status, so the UI thread never freezes and the caller
    // falls back to RAW. After one timeout the Status API is disabled for the
    // rest of the process (subsequent calls return immediately) so every later
    // print is not delayed by the same hang. Call this from the UI thread.
    // A worker abandoned before its send never sends later. One abandoned while
    // already inside the send may still print: *mayStillPrint is then true and the
    // caller must not print the same bytes by RAW (it would come out twice).
    static bool sendAndReadStatusBounded(const QByteArray &escpos, const QString &queueName,
                                         PrinterStatus *status, QString *err, int timeoutMs,
                                         bool *mayStillPrint = nullptr);

    // Diagnostic: opens the printer and calls each Status API function in turn
    // (BiOpenMonPrinter / BiGetStatus / BiGetType / BiDirectIOEx real-time status
    // / BiCloseMonPrinter), timing and logging each one ([diag] in the log), and
    // returns a human-readable report. Runs behind the same watchdog, so a call
    // that hangs shows as the last line with no result after it and the report
    // ends with a "BLOQUEO" note naming it as the culprit. Used by the "Probar
    // Status API" button in SettingsDialog to pinpoint a firmware-induced hang.
    static QString runDiagnosticsBounded(const QString &queueName, int timeoutMs);
};

#endif // STATUSAPIPRINTER_H
