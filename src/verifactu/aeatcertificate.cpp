#include "aeatcertificate.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#endif

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("AeatCertificate", text);
}

#ifdef Q_OS_WIN
QDateTime fromFileTime(const FILETIME &ft)
{
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    return QDateTime(QDate(st.wYear, st.wMonth, st.wDay), QTime(st.wHour, st.wMinute, st.wSecond), Qt::UTC).toLocalTime();
}

QString nameOf(PCCERT_CONTEXT ctx, DWORD flags)
{
    wchar_t buffer[512] = {};
    CertGetNameStringW(ctx, CERT_NAME_SIMPLE_DISPLAY_TYPE, flags, nullptr, buffer, 512);
    return QString::fromWCharArray(buffer);
}

AeatCertificate::Info infoOf(PCCERT_CONTEXT ctx)
{
    AeatCertificate::Info info;
    info.subject = nameOf(ctx, 0);
    info.issuer = nameOf(ctx, CERT_NAME_ISSUER_FLAG);
    BYTE hash[20];
    DWORD size = sizeof(hash);
    if (CertGetCertificateContextProperty(ctx, CERT_HASH_PROP_ID, hash, &size))
        info.thumbprint = QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(hash), int(size)).toHex()).toUpper();
    info.expiry = fromFileTime(ctx->pCertInfo->NotAfter);
    DWORD keySpec = 0;
    BOOL mustFree = FALSE;
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0;
    info.hasPrivateKey = CryptAcquireCertificatePrivateKey(ctx, CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG
                                                                    | CRYPT_ACQUIRE_CACHE_FLAG,
                                                           nullptr, &key, &keySpec, &mustFree);
    if (info.hasPrivateKey && mustFree) {
        if (keySpec == CERT_NCRYPT_KEY_SPEC)
            NCryptFreeObject(key);
        else
            CryptReleaseContext(key, 0);
    }
    return info;
}
#endif

} // namespace

AeatCertificate::~AeatCertificate()
{
    release();
}

void AeatCertificate::release()
{
#ifdef Q_OS_WIN
    if (m_context)
        CertFreeCertificateContext(static_cast<PCCERT_CONTEXT>(m_context));
    if (m_store)
        CertCloseStore(static_cast<HCERTSTORE>(m_store), 0);
#endif
    m_context = nullptr;
    m_store = nullptr;
}

AeatCertificate::Info AeatCertificate::info() const
{
#ifdef Q_OS_WIN
    if (m_context)
        return infoOf(static_cast<PCCERT_CONTEXT>(m_context));
#endif
    return Info();
}

QList<AeatCertificate::Info> AeatCertificate::personalCertificates()
{
    QList<Info> out;
#ifdef Q_OS_WIN
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                     CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG, L"MY");
    if (!store)
        return out;
    PCCERT_CONTEXT ctx = nullptr;
    while ((ctx = CertEnumCertificatesInStore(store, ctx)) != nullptr) {
        const Info info = infoOf(ctx);
        if (info.hasPrivateKey)
            out << info;
    }
    CertCloseStore(store, 0);
#endif
    return out;
}

bool AeatCertificate::accept(const void *context, QString *error)
{
#ifdef Q_OS_WIN
    const Info info = infoOf(static_cast<PCCERT_CONTEXT>(context));
    if (!info.hasPrivateKey) {
        *error = tr("El certificado %1 no tiene su clave privada disponible.").arg(info.subject);
        return false;
    }
    if (info.expiry < QDateTime::currentDateTime()) {
        *error = tr("El certificado electrónico caducó el %1.").arg(info.expiry.toString("dd-MM-yyyy"));
        return false;
    }
    return true;
#else
    Q_UNUSED(context)
    *error = tr("El certificado electrónico solo se puede usar en Windows.");
    return false;
#endif
}

bool AeatCertificate::loadFromStore(const QString &thumbprint, QString *error)
{
    release();
#ifdef Q_OS_WIN
    const QByteArray hash = QByteArray::fromHex(thumbprint.trimmed().toLatin1());
    if (hash.size() != 20) {
        *error = tr("No se ha elegido el certificado electrónico.");
        return false;
    }
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                     CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG, L"MY");
    if (!store) {
        *error = tr("No se puede abrir el almacén de certificados de Windows.");
        return false;
    }
    CRYPT_HASH_BLOB blob{ DWORD(hash.size()), reinterpret_cast<BYTE *>(const_cast<char *>(hash.constData())) };
    PCCERT_CONTEXT ctx = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                                                    CERT_FIND_HASH, &blob, nullptr);
    if (!ctx) {
        CertCloseStore(store, 0);
        *error = tr("El certificado elegido ya no está instalado en Windows.");
        return false;
    }
    m_store = store;
    m_context = ctx;
    if (!accept(ctx, error)) {
        release();
        return false;
    }
    return true;
#else
    Q_UNUSED(thumbprint)
    return accept(nullptr, error);
#endif
}

bool AeatCertificate::loadFromFile(const QString &path, const QString &password, QString *error)
{
    release();
    QFile file(path);
    if (path.trimmed().isEmpty() || !file.open(QIODevice::ReadOnly)) {
        *error = tr("No se encuentra el certificado electrónico: %1").arg(path);
        return false;
    }
#ifdef Q_OS_WIN
    QByteArray data = file.readAll();
    CRYPT_DATA_BLOB blob{ DWORD(data.size()), reinterpret_cast<BYTE *>(data.data()) };
    const std::wstring pw = password.toStdWString();
    // In memory only: the key is not written to the Windows key store.
    HCERTSTORE store = PFXImportCertStore(&blob, pw.c_str(), PKCS12_NO_PERSIST_KEY | CRYPT_USER_KEYSET);
    if (!store) {
        *error = tr("No se puede abrir el certificado %1: contraseña incorrecta o archivo no válido.").arg(path);
        return false;
    }
    // The end-entity certificate is the one that carries the key.
    PCCERT_CONTEXT ctx = nullptr;
    PCCERT_CONTEXT found = nullptr;
    while ((ctx = CertEnumCertificatesInStore(store, ctx)) != nullptr) {
        if (infoOf(ctx).hasPrivateKey) {
            found = CertDuplicateCertificateContext(ctx);
            CertFreeCertificateContext(ctx);
            break;
        }
    }
    if (!found) {
        CertCloseStore(store, 0);
        *error = tr("El archivo %1 no contiene la clave privada del certificado.").arg(path);
        return false;
    }
    m_store = store;
    m_context = found;
    if (!accept(found, error)) {
        release();
        return false;
    }
    return true;
#else
    Q_UNUSED(password)
    return accept(nullptr, error);
#endif
}
