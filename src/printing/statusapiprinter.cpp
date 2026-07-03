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
#include <QElapsedTimer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

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
typedef int(WINAPI *BiGetType_t)(int, LPBYTE, LPBYTE, LPBYTE, LPBYTE);

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
        // Only log when something is off (nonzero ASB or a read error): a normal
        // ready print stays quiet, and the raw word still identifies the offline
        // cause in the field (this unit reports off=0x1, lid-open/no-paper=0x4).
        if (rc != 0 || asb != 0)
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

    // Send the ESC/POS bytes. We only write, so the read buffer is empty. The
    // 2500 ms timeout bounds how long an offline send (cover open / no paper)
    // blocks before returning ERR_ACCESS - short enough that the warning appears
    // quickly and the caller's watchdog (which must sit above this) does not trip
    // on a normal offline print. An online send returns in tens of ms.
    int readLen = 0;
    const int rc = io(handle,
                      escpos.size(),
                      reinterpret_cast<LPBYTE>(const_cast<char *>(escpos.constData())),
                      &readLen, nullptr, 2500, 0, 0);
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

bool StatusApiPrinter::sendAndReadStatusBounded(const QByteArray &escpos, const QString &queueName,
                                                PrinterStatus *status, QString *err, int timeoutMs)
{
    // Latches on the first timeout so a firmware/driver hang delays only one
    // print, not every one after it. Never reset (a stuck DLL stays stuck until
    // the process restarts); the operator can also just disable the setting.
    static std::atomic<bool> s_unresponsive{false};
    if (s_unresponsive.load()) {
        if (status) *status = PrinterStatus();
        if (err) *err = QStringLiteral("Status API deshabilitada tras un bloqueo previo (usando RAW).");
        return false;
    }

    struct Shared {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        bool ret = false;
        PrinterStatus status;
        QString err;
    };
    auto sh = std::make_shared<Shared>();
    // Copy the inputs so the detached worker owns them even if we return first.
    const QByteArray bytes = escpos;
    const QString queue = queueName;
    std::thread([sh, bytes, queue]() {
        PrinterStatus st;
        QString e;
        const bool r = StatusApiPrinter::sendAndReadStatus(bytes, queue, &st, &e);
        {
            std::lock_guard<std::mutex> lk(sh->m);
            sh->ret = r;
            sh->status = st;
            sh->err = e;
            sh->done = true;
        }
        sh->cv.notify_one();
    }).detach();

    std::unique_lock<std::mutex> lk(sh->m);
    if (sh->cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&sh] { return sh->done; })) {
        if (status) *status = sh->status;
        if (err) *err = sh->err;
        return sh->ret;
    }

    // The worker is stuck inside the Epson DLL. Leave it detached (its shared
    // state keeps its result slot alive harmlessly) and fall back to RAW.
    s_unresponsive.store(true);
    qWarning() << "StatusApiPrinter: bloqueo de la Status API (>" << timeoutMs
               << "ms); deshabilitada para esta sesion, usando RAW.";
    if (status) *status = PrinterStatus();
    if (err) *err = QStringLiteral("Status API sin respuesta (%1 ms), usando RAW.").arg(timeoutMs);
    return false;
}

#ifdef Q_OS_WIN
namespace {
// Runs the Status API call sequence, invoking log() with a line before AND after
// each Bi* call (with elapsed ms) so a hang is pinpointed: the culprit is the
// last "->" line with no matching result line. Sends only a real-time status
// request (DLE EOT 1), which reads status without printing.
void diagSequence(const QString &queueName, const std::function<void(const QString &)> &log)
{
    log(QStringLiteral("Cargando EPSStmApi.dll..."));
    HMODULE dll = LoadLibraryW(L"EPSStmApi.dll");
    if (!dll) { log(QStringLiteral("ERROR: no se pudo cargar EPSStmApi.dll.")); return; }

    auto open  = reinterpret_cast<BiOpenMonPrinter_t>(GetProcAddress(dll, "BiOpenMonPrinter"));
    auto close = reinterpret_cast<BiCloseMonPrinter_t>(GetProcAddress(dll, "BiCloseMonPrinter"));
    auto stat  = reinterpret_cast<BiGetStatus_t>(GetProcAddress(dll, "BiGetStatus"));
    auto io    = reinterpret_cast<BiDirectIOEx_t>(GetProcAddress(dll, "BiDirectIOEx"));
    auto type  = reinterpret_cast<BiGetType_t>(GetProcAddress(dll, "BiGetType"));
    log(QStringLiteral("Funciones resueltas: open=%1 close=%2 status=%3 io=%4 type=%5")
            .arg(open ? 1 : 0).arg(close ? 1 : 0).arg(stat ? 1 : 0).arg(io ? 1 : 0).arg(type ? 1 : 0));
    if (!open || !close || !stat || !io) {
        log(QStringLiteral("ERROR: faltan funciones en EPSStmApi.dll."));
        FreeLibrary(dll);
        return;
    }

    QString queue = queueName.trimmed();
    if (queue.isEmpty()) queue = defaultPrinterName();
    QByteArray name = queue.toLocal8Bit();
    if (name.isEmpty()) {
        log(QStringLiteral("ERROR: no hay impresora configurada ni predeterminada."));
        FreeLibrary(dll);
        return;
    }
    log(QStringLiteral("Impresora: '%1'").arg(queue));

    QElapsedTimer t;
    log(QStringLiteral("-> BiOpenMonPrinter..."));
    t.start();
    const int handle = open(2, const_cast<LPSTR>(name.constData()));
    log(QStringLiteral("   BiOpenMonPrinter rc/handle=%1 (%2 ms)").arg(handle).arg(t.elapsed()));
    if (handle <= 0) { FreeLibrary(dll); return; }

    log(QStringLiteral("-> BiGetStatus..."));
    t.restart();
    DWORD asb = 0;
    const int rcs = stat(handle, &asb);
    log(QStringLiteral("   BiGetStatus rc=%1 asb=0x%2 (%3 ms)")
            .arg(rcs).arg(asb, 0, 16).arg(t.elapsed()));

    if (type) {
        log(QStringLiteral("-> BiGetType..."));
        t.restart();
        BYTE tid = 0, font = 0, ex = 0, sp = 0;
        const int rct = type(handle, &tid, &font, &ex, &sp);
        log(QStringLiteral("   BiGetType rc=%1 typeID=%2 (%3 ms)")
                .arg(rct).arg(tid).arg(t.elapsed()));
    }

    log(QStringLiteral("-> BiDirectIOEx (DLE EOT 1, estado en tiempo real, no imprime)..."));
    t.restart();
    BYTE cmd[3] = { 0x10, 0x04, 0x01 };
    BYTE rbuf[8] = { 0 };
    int rlen = 1;
    const int rcd = io(handle, 3, cmd, &rlen, rbuf, 3000, 0, 0);
    log(QStringLiteral("   BiDirectIOEx rc=%1 readLen=%2 byte0=0x%3 (%4 ms)")
            .arg(rcd).arg(rlen).arg(rbuf[0], 0, 16).arg(t.elapsed()));

    log(QStringLiteral("-> BiCloseMonPrinter..."));
    t.restart();
    const int rcc = close(handle);
    log(QStringLiteral("   BiCloseMonPrinter rc=%1 (%2 ms)").arg(rcc).arg(t.elapsed()));

    FreeLibrary(dll);
}
} // namespace
#endif

QString StatusApiPrinter::runDiagnosticsBounded(const QString &queueName, int timeoutMs)
{
#ifdef Q_OS_WIN
    struct Shared {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        QString report;
    };
    auto sh = std::make_shared<Shared>();
    const QString queue = queueName;
    std::thread([sh, queue]() {
        auto log = [&sh](const QString &line) {
            qDebug().noquote() << "[diag]" << line;
            std::lock_guard<std::mutex> lk(sh->m);
            sh->report += line + QLatin1Char('\n');
        };
        diagSequence(queue, log);
        {
            std::lock_guard<std::mutex> lk(sh->m);
            sh->done = true;
        }
        sh->cv.notify_one();
    }).detach();

    std::unique_lock<std::mutex> lk(sh->m);
    if (sh->cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&sh] { return sh->done; }))
        return sh->report + QStringLiteral("\nDiagnostico completado.");
    return sh->report + QStringLiteral(
        "\n*** BLOQUEO: la ultima llamada mostrada arriba no respondio en %1 s.\n"
        "Esa es la funcion de la Status API que se cuelga con este firmware. ***")
        .arg(timeoutMs / 1000);
#else
    Q_UNUSED(queueName);
    Q_UNUSED(timeoutMs);
    return QStringLiteral("La Status API solo esta disponible en Windows.");
#endif
}
