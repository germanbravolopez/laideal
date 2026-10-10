#include "aeattransport.h"

#include "aeatcertificate.h"

#include <QCoreApplication>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#endif

namespace {

#ifdef Q_OS_WIN
QString lastErrorText(const QString &step)
{
    const DWORD code = GetLastError();
    wchar_t buffer[512] = {};
    FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   GetModuleHandleW(L"winhttp.dll"), code, 0, buffer, 512, nullptr);
    QString text = QString::fromWCharArray(buffer).trimmed();
    if (code == ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED)
        text = QCoreApplication::translate("AeatTransport", "la AEAT pide un certificado electrónico y no hay ninguno configurado");
    return QStringLiteral("%1: %2 (%3)").arg(step, text.isEmpty() ? QStringLiteral("error") : text).arg(code);
}

// Closes a WinHTTP handle when it goes out of scope.
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};
#endif

} // namespace

AeatTransport::AeatTransport(QObject *parent)
    : QObject(parent)
{
}

AeatTransport::~AeatTransport() = default;

AeatTransport::Response AeatTransport::postBlocking(const QString &url, const QByteArray &body,
                                                    const void *certificateContext, int timeoutMs)
{
    Response response;
#ifdef Q_OS_WIN
    const QUrl parsed(url);
    const bool secure = parsed.scheme() == QLatin1String("https");
    if (secure && !certificateContext) {
        response.error = QCoreApplication::translate("AeatTransport", "Falta el certificado electrónico para conectar con la AEAT");
        return response;
    }
    Handle session{ WinHttpOpen(L"LaIdeal-Verifactu/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
    if (!session.h) {
        response.error = lastErrorText(QStringLiteral("WinHttpOpen"));
        return response;
    }
    WinHttpSetTimeouts(session.h, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    const std::wstring host = parsed.host().toStdWString();
    const INTERNET_PORT port = INTERNET_PORT(parsed.port(secure ? 443 : 80));
    Handle connection{ WinHttpConnect(session.h, host.c_str(), port, 0) };
    if (!connection.h) {
        response.error = lastErrorText(QStringLiteral("WinHttpConnect"));
        return response;
    }
    const std::wstring path = parsed.path(QUrl::FullyEncoded).toStdWString();
    Handle request{ WinHttpOpenRequest(connection.h, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0) };
    if (!request.h) {
        response.error = lastErrorText(QStringLiteral("WinHttpOpenRequest"));
        return response;
    }
    if (secure && !WinHttpSetOption(request.h, WINHTTP_OPTION_CLIENT_CERT_CONTEXT,
                                    const_cast<void *>(certificateContext), sizeof(CERT_CONTEXT))) {
        response.error = lastErrorText(QStringLiteral("certificado"));
        return response;
    }
    const wchar_t *headers = L"Content-Type: text/xml; charset=utf-8\r\nSOAPAction: \"\"\r\n";
    if (!WinHttpSendRequest(request.h, headers, DWORD(-1L), const_cast<char *>(body.constData()), DWORD(body.size()),
                            DWORD(body.size()), 0)
        || !WinHttpReceiveResponse(request.h, nullptr)) {
        response.error = lastErrorText(QStringLiteral("envío"));
        return response;
    }
    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    response.httpStatus = int(status);
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) {
            response.error = lastErrorText(QStringLiteral("lectura"));
            break;
        }
        if (available == 0)
            break;
        QByteArray chunk(int(available), Qt::Uninitialized);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), available, &read)) {
            response.error = lastErrorText(QStringLiteral("lectura"));
            break;
        }
        response.body += chunk.left(int(read));
    }
#else
    Q_UNUSED(url) Q_UNUSED(body) Q_UNUSED(certificateContext) Q_UNUSED(timeoutMs)
    response.error = QStringLiteral("AeatTransport: only implemented on Windows");
#endif
    return response;
}

void AeatTransport::post(const QString &url, const QByteArray &body, const Done &done)
{
    const void *context = m_certificate ? m_certificate->context() : nullptr;
#ifdef Q_OS_WIN
    // The worker holds its own reference: the certificate may be released meanwhile.
    if (context)
        context = CertDuplicateCertificateContext(static_cast<PCCERT_CONTEXT>(context));
#endif
    const int timeoutMs = m_timeoutMs;
    QPointer<AeatTransport> self(this);
    QThreadPool::globalInstance()->start(QRunnable::create([self, url, body, context, timeoutMs, done]() {
        const Response response = postBlocking(url, body, context, timeoutMs);
#ifdef Q_OS_WIN
        if (context)
            CertFreeCertificateContext(static_cast<PCCERT_CONTEXT>(context));
#endif
        // Answered on the main thread, and only if the transport still exists then.
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, done, response]() {
            if (self)
                done(response);
        }, Qt::QueuedConnection);
    }));
}
