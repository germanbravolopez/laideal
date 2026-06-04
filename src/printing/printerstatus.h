#ifndef PRINTERSTATUS_H
#define PRINTERSTATUS_H

#include <QString>
#include <QtGlobal>

// Decoded device status read back from an Epson thermal printer through the
// Status API (ASB = Automatic Status Back, a 32-bit word returned by
// BiGetStatus). This struct is pure: fromAsb() turns the raw word into named
// flags and summary() into a Spanish operator message, with no I/O - so it is
// unit-tested in tests/test_escpos.cpp like EscPosBuilder. The host-specific
// piece (loading the DLL and calling BiGetStatus) lives in StatusApiPrinter.
//
// See docs/modules/printer/02_control_methods.md (option B) and
// 03_escpos_command_reference.md (Status API functions).
struct PrinterStatus
{
    bool valid = false;             // true once decoded from a real ASB word
    bool offline = false;           // printer not ready (ASB_OFF_LINE)
    bool coverOpen = false;         // cover/roll-paper door open
    bool paperNearEnd = false;      // roll paper near end (warning)
    bool paperEnd = false;          // roll paper end (no paper)
    bool cutterError = false;       // auto-cutter error
    bool mechanicalError = false;   // mechanical error
    bool unrecoverableError = false;// unrecoverable error (needs power cycle)
    bool drawerOpen = false;        // cash-drawer kick signal level
    quint32 raw = 0;                // the original ASB word, for logging

    // Decode an ASB status word into the flags above (valid = true).
    static PrinterStatus fromAsb(quint32 asb);

    // A hard problem that prevents/aborts printing (paper out, cover open,
    // cutter/mechanical/unrecoverable error, or offline). Worth a modal alert.
    bool hasError() const;
    // A soft condition worth surfacing but not blocking (paper near end).
    bool hasWarning() const;

    // Spanish operator message describing the most relevant condition, e.g.
    // "Sin papel", "Tapa abierta", "Papel casi agotado" or "Impresora lista".
    QString summary() const;
};

#endif // PRINTERSTATUS_H
