#ifndef THERMALPRINTER_H
#define THERMALPRINTER_H

#include <QByteArray>
#include <QString>

// Transport for a raw ESC/POS byte stream. On Windows it writes the bytes to a
// printer queue as RAW spool data (OpenPrinter -> StartDocPrinter datatype RAW
// -> WritePrinter), so the spooler hands them to the device untouched - no GDI
// rendering, no Excel, no driver page. This is the only host-specific piece of
// the printing path; everything upstream is the pure EscPosBuilder.
//
// See docs/modules/printer/02_control_methods.md (option A). Non-Windows builds
// (the CI test runner) compile a stub that fails cleanly, so EscPosBuilder /
// TicketRenderer stay cross-platform and unit-testable.
class ThermalPrinter
{
public:
    // Send escpos to the named queue. An empty queueName uses the Windows
    // default printer. Returns false and fills *err (when non-null) on failure
    // instead of throwing, so callers can show a message box like before.
    static bool send(const QByteArray &escpos, const QString &queueName, QString *err = nullptr);
};

#endif // THERMALPRINTER_H
