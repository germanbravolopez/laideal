#include "thermalprinter.h"

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

#ifdef Q_OS_WIN
namespace {
// Human-readable text for the last Win32 error.
QString winError()
{
    const DWORD code = GetLastError();
    if (code == 0) return QStringLiteral("error desconocido");
    LPWSTR buf = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    QString msg = n ? QString::fromWCharArray(buf, static_cast<int>(n)).trimmed()
                    : QStringLiteral("error %1").arg(code);
    if (buf) LocalFree(buf);
    return msg;
}

// The Windows default printer name, or empty if none is set.
QString defaultPrinterName()
{
    DWORD len = 0;
    GetDefaultPrinterW(nullptr, &len);   // first call asks for the buffer size
    if (len == 0) return QString();
    std::vector<wchar_t> buf(len);
    if (!GetDefaultPrinterW(buf.data(), &len)) return QString();
    return QString::fromWCharArray(buf.data());
}
} // namespace
#endif

bool ThermalPrinter::send(const QByteArray &escpos, const QString &queueName, QString *err)
{
    auto fail = [err](const QString &m) { if (err) *err = m; return false; };

#ifdef Q_OS_WIN
    QString queue = queueName.trimmed();
    if (queue.isEmpty())
        queue = defaultPrinterName();
    if (queue.isEmpty())
        return fail(QStringLiteral("No hay impresora configurada ni impresora predeterminada."));

    std::wstring wQueue = queue.toStdWString();
    HANDLE hPrinter = nullptr;
    if (!OpenPrinterW(const_cast<LPWSTR>(wQueue.c_str()), &hPrinter, nullptr))
        return fail(QStringLiteral("No se pudo abrir la impresora '%1': %2").arg(queue, winError()));

    // RAW datatype: the spooler forwards the bytes to the device verbatim.
    std::wstring docName = L"La Ideal ticket";
    std::wstring rawType = L"RAW";
    DOC_INFO_1W docInfo{};
    docInfo.pDocName  = const_cast<LPWSTR>(docName.c_str());
    docInfo.pOutputFile = nullptr;
    docInfo.pDatatype = const_cast<LPWSTR>(rawType.c_str());

    bool ok = false;
    QString errMsg;
    if (StartDocPrinterW(hPrinter, 1, reinterpret_cast<LPBYTE>(&docInfo)) != 0) {
        if (StartPagePrinter(hPrinter)) {
            DWORD written = 0;
            const BOOL wrote = WritePrinter(
                hPrinter,
                const_cast<LPVOID>(static_cast<LPCVOID>(escpos.constData())),
                static_cast<DWORD>(escpos.size()), &written);
            EndPagePrinter(hPrinter);
            if (wrote && written == static_cast<DWORD>(escpos.size()))
                ok = true;
            else
                errMsg = QStringLiteral("Fallo al enviar los datos a '%1': %2").arg(queue, winError());
        } else {
            errMsg = QStringLiteral("StartPagePrinter falló en '%1': %2").arg(queue, winError());
        }
        EndDocPrinter(hPrinter);
    } else {
        errMsg = QStringLiteral("StartDocPrinter falló en '%1': %2").arg(queue, winError());
    }
    ClosePrinter(hPrinter);

    if (!ok)
        return fail(errMsg);
    return true;
#else
    Q_UNUSED(escpos);
    Q_UNUSED(queueName);
    return fail(QStringLiteral("La impresión solo está disponible en Windows."));
#endif
}
