#ifndef AEATTRANSPORT_H
#define AEATTRANSPORT_H

// How the direct AEAT client posts a SOAP request. Production goes through WinHTTP,
// the Windows HTTP stack, which presents the owner's certificate (AeatCertificate)
// in the TLS handshake - Qt's Schannel backend cannot use a PKCS#12 key. The call
// runs on a worker thread and answers on the caller's thread. Plain http:// URLs
// (the tests' local fake AEAT) need no certificate.

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

class AeatCertificate;

class AeatTransport : public QObject
{
    Q_OBJECT

public:
    struct Response {
        QByteArray body;
        int        httpStatus = 0;
        QString    error;        // transport failure (no connection, TLS, timeout); empty when AEAT answered
    };
    using Done = std::function<void(const Response &)>;

    explicit AeatTransport(QObject *parent = nullptr);
    ~AeatTransport() override;

    // `certificate` must outlive every request; nullptr for plain http.
    void setCertificate(const AeatCertificate *certificate) { m_certificate = certificate; }
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    // Posts `body` (text/xml SOAP) to `url`; `done` is called on this object's thread.
    void post(const QString &url, const QByteArray &body, const Done &done);

    // Synchronous core, also used by the worker thread.
    static Response postBlocking(const QString &url, const QByteArray &body, const void *certificateContext,
                                 int timeoutMs);

private:
    const AeatCertificate *m_certificate = nullptr;
    int m_timeoutMs = 30000;
};

#endif // AEATTRANSPORT_H
