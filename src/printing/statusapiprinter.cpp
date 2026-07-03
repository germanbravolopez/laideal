#include "statusapiprinter.h"

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

#include <QtGlobal>
#include <QDebug>

#ifdef Q_OS_WIN
namespace {
// Epson Status API entry points (EPSStmApi.dll), __stdcall. Resolved at runtime
// via GetProcAddress so the SDK is not needed to build. Signatures from the APD
// Status API reference (see docs/modules/printer/03_escpos_command_reference.md).
//   BiOpenMonPrinter(nType, pName): nType 2 = printer-queue name; returns a
//     monitor handle (>0) or <=0 on error.
//   BiDirectIOEx(handle, writeLen, writeBuf, &readLen, readBuf, timeoutMs,
//                nullTerminate, option): sends/receives raw bytes; 0 = success.
//   BiGetStatus(handle, &asb): fills the 32-bit ASB word; 0 = success.
//   BiCloseMonPrinter(handle): 0 = success.
typedef int(WINAPI *BiOpenMonPrinter_t)(int, LPSTR);
typedef int(WINAPI *BiDirectIOEx_t)(int, int, LPBYTE, LPINT, LPBYTE, int, int, int);
typedef int(WINAPI *BiGetStatus_t)(int, LPDWORD);
typedef int(WINAPI *BiCloseMonPrinter_t)(int);

// The Windows default printer name, or empty if none is set. Mirrors the helper
// in thermalprinter.cpp so the Status API path resolves a blank queue name to
// the default printer (the documented "use default" setting) instead of failing.
QString defaultPrinterName()
{
    DWORD len = 0;
    GetDefaultPrinterW(nullptr, &len);
    if (len == 0) return QString();
    std::vector<wchar_t> buf(len);
    if (!GetDefaultPrinterW(buf.data(), &len)) return QString();
    return QString::fromWCharArray(buf.data());
}
} // namespace
#endif

bool StatusApiPrinter::sendAndReadStatus(const QByteArray &escpos, const QString &queueName,
                                         PrinterStatus *status, QString *err)
{
    auto fail = [err](const QString &m) { if (err) *err = m; return false; };

#ifdef Q_OS_WIN
    HMODULE dll = LoadLibraryW(L"EPSStmApi.dll");
    if (!dll)
        return fail(QStringLiteral(
            "Status API no disponible (EPSStmApi.dll no encontrada)."));

    auto open  = reinterpret_cast<BiOpenMonPrinter_t>(GetProcAddress(dll, "BiOpenMonPrinter"));
    auto io    = reinterpret_cast<BiDirectIOEx_t>(GetProcAddress(dll, "BiDirectIOEx"));
    auto stat  = reinterpret_cast<BiGetStatus_t>(GetProcAddress(dll, "BiGetStatus"));
    auto close = reinterpret_cast<BiCloseMonPrinter_t>(GetProcAddress(dll, "BiCloseMonPrinter"));
    if (!open || !io || !stat || !close) {
        FreeLibrary(dll);
        return fail(QStringLiteral("Status API incompleta (faltan funciones en EPSStmApi.dll)."));
    }

    // nType 2 = printer-queue name. The API takes an ANSI string and does not
    // accept an empty name, so a blank setting (use-default) is resolved to the
    // Windows default printer here - same target the RAW path would use.
    QString queue = queueName.trimmed();
    if (queue.isEmpty())
        queue = defaultPrinterName();
    QByteArray name = queue.toLocal8Bit();
    if (name.isEmpty()) {
        FreeLibrary(dll);
        return fail(QStringLiteral("No hay impresora configurada ni predeterminada para la Status API."));
    }

    const int handle = open(2, const_cast<LPSTR>(name.constData()));
    if (handle <= 0) {
        FreeLibrary(dll);
        return fail(QStringLiteral("No se pudo abrir la impresora '%1' con la Status API (%2).")
                        .arg(queue, QString::number(handle)));
    }

    // Read the device status BEFORE sending. BiGetStatus returns the cached ASB
    // the printer auto-sends on cover-open / paper-out and succeeds even while
    // the printer is offline - unlike the BiDirectIOEx write, which returns
    // ERR_ACCESS (-80) when the cover is open, so a send-first design would only
    // ever see a bare send failure and never the real reason. (APD6 Status API
    // manual: "confirm the printer can print before printing".)
    // Read the ASB and (best-effort) decode it into *status. Logs the raw word so
    // the TM-T20III's actual ASB layout can be confirmed against our ASB_* bit
    // constants on the shop hardware.
    auto readStatus = [&](const char *when) {
        DWORD asb = 0;
        const int rc = stat(handle, &asb);
        qDebug().nospace() << "StatusApiPrinter: BiGetStatus(" << when << ") rc=" << rc
                           << " asb=0x" << QString::number(asb, 16)
                           << " summary=" << (rc == 0 ? PrinterStatus::fromAsb(asb).summary() : QString("-"));
        if (rc == 0 && status)
            *status = PrinterStatus::fromAsb(asb);
    };
    readStatus("pre-send");

    // A fatal fault (cutter / mechanical / unrecoverable) must not print: skip
    // the send and report false so the caller does not queue it via RAW either.
    if (status && status->isFatal()) {
        close(handle);
        FreeLibrary(dll);
        return fail(QStringLiteral("Impresora en error, no se envia: %1").arg(status->summary()));
    }

    // Send the ESC/POS bytes. We only write, so the read buffer is empty.
    int readLen = 0;
    const int rc = io(handle,
                      escpos.size(),
                      reinterpret_cast<LPBYTE>(const_cast<char *>(escpos.constData())),
                      &readLen, nullptr, 5000, 0, 0);
    bool sent = (rc == 0);

    // The cover-open / paper-out ASB often lands only after the send attempt, so
    // a pre-send read can still show "ready". Re-read on failure so the caller
    // gets the real reason (and the log captures the raw word either way).
    if (!sent)
        readStatus("post-send");

    close(handle);
    FreeLibrary(dll);

    if (!sent)
        return fail(QStringLiteral("Fallo al enviar los datos a '%1' con la Status API (%2).")
                        .arg(queue, QString::number(rc)));
    return true;
#else
    Q_UNUSED(escpos);
    Q_UNUSED(queueName);
    Q_UNUSED(status);
    return fail(QStringLiteral("La Status API solo está disponible en Windows."));
#endif
}
