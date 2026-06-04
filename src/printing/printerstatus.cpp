#include "printerstatus.h"

namespace {
// Epson ASB (Automatic Status Back) bit definitions, as documented in the
// APD Status API reference (EPSStmApi.h ASB_* constants). The same layout is
// returned by BiGetStatus for the TM-T20III. Only the bits we act on are named.
enum AsbBit : quint32 {
    Asb_DrawerKick        = 0x00000004u,  // cash-drawer connector signal level
    Asb_OffLine           = 0x00000008u,  // printer offline
    Asb_CoverOpen         = 0x00000020u,  // cover / roll-paper door open
    Asb_MechanicalErr     = 0x00000400u,  // mechanical error
    Asb_AutoCutterErr     = 0x00000800u,  // auto-cutter error
    Asb_UnrecoverableErr  = 0x00002000u,  // unrecoverable error
    Asb_ReceiptNearEnd    = 0x00020000u,  // roll paper near end
    Asb_ReceiptEnd        = 0x00080000u,  // roll paper end (no paper)
};
} // namespace

PrinterStatus PrinterStatus::fromAsb(quint32 asb)
{
    PrinterStatus s;
    s.valid              = true;
    s.raw                = asb;
    s.offline            = (asb & Asb_OffLine) != 0;
    s.coverOpen          = (asb & Asb_CoverOpen) != 0;
    s.paperNearEnd       = (asb & Asb_ReceiptNearEnd) != 0;
    s.paperEnd           = (asb & Asb_ReceiptEnd) != 0;
    s.cutterError        = (asb & Asb_AutoCutterErr) != 0;
    s.mechanicalError    = (asb & Asb_MechanicalErr) != 0;
    s.unrecoverableError = (asb & Asb_UnrecoverableErr) != 0;
    s.drawerOpen         = (asb & Asb_DrawerKick) != 0;
    return s;
}

bool PrinterStatus::hasError() const
{
    // offline is deliberately excluded: the printer reports OFF_LINE transiently
    // while executing a cut/feed or at power-up, so a status read right after a
    // successful send would otherwise raise a spurious alert on a ticket that
    // printed fine. A genuine power-off surfaces earlier as a send failure.
    return paperEnd || coverOpen || cutterError || mechanicalError
        || unrecoverableError;
}

bool PrinterStatus::hasWarning() const
{
    return paperNearEnd;
}

QString PrinterStatus::summary() const
{
    if (!valid)
        return QStringLiteral("Estado de la impresora desconocido");
    // Report the most actionable condition first (errors before warnings).
    if (unrecoverableError)
        return QStringLiteral("Error irrecuperable de la impresora (reinicie el equipo)");
    if (paperEnd)
        return QStringLiteral("Sin papel");
    if (coverOpen)
        return QStringLiteral("Tapa abierta");
    if (cutterError)
        return QStringLiteral("Error en el cortador");
    if (mechanicalError)
        return QStringLiteral("Error mecánico de la impresora");
    if (offline)
        return QStringLiteral("Impresora fuera de línea");
    if (paperNearEnd)
        return QStringLiteral("Papel casi agotado");
    return QStringLiteral("Impresora lista");
}
