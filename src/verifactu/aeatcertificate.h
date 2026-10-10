#ifndef AEATCERTIFICATE_H
#define AEATCERTIFICATE_H

// The owner's electronic certificate for the direct AEAT client, through Windows'
// own crypto API (Qt's Schannel backend cannot read PKCS#12 files, and the release
// ships no OpenSSL). Two sources:
//  - the Windows personal store (CurrentUser\MY), where the FNMT certificate is
//    installed when it is used in the browser: picked by thumbprint, nothing to
//    store but the thumbprint, the key stays protected by Windows;
//  - a .pfx / .p12 file and its password, imported in memory only (the key is not
//    persisted in Windows).
// The handle is what AeatTransport presents to AEAT in the TLS handshake.

#include <QDateTime>
#include <QList>
#include <QString>

class AeatCertificate
{
public:
    struct Info {
        QString   subject;          // e.g. "LOPEZ DOMINGUEZ ROCIO - 00000000T"
        QString   issuer;
        QString   thumbprint;       // SHA-1, upper-case hex
        QDateTime expiry;
        bool      hasPrivateKey = false;
    };

    AeatCertificate() = default;
    ~AeatCertificate();
    AeatCertificate(const AeatCertificate &) = delete;
    AeatCertificate &operator=(const AeatCertificate &) = delete;

    // Personal certificates with a private key, for the choice in Configuración.
    static QList<Info> personalCertificates();

    bool loadFromStore(const QString &thumbprint, QString *error);
    bool loadFromFile(const QString &path, const QString &password, QString *error);

    bool  isLoaded() const { return m_context != nullptr; }
    Info  info() const;
    // The Windows certificate context (PCCERT_CONTEXT), or nullptr.
    const void *context() const { return m_context; }

private:
    const void *m_context = nullptr;   // PCCERT_CONTEXT
    void       *m_store = nullptr;     // HCERTSTORE kept open while the context is used

    void release();
    bool accept(const void *context, QString *error);
};

#endif // AEATCERTIFICATE_H
