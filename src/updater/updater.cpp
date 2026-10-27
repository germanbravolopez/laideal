#include "updater.h"

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QDebug>

// Hardcoded - this is a bespoke deployment. If the repo ever moves these are
// the two lines to change.
static const char *kGitHubApiLatestUrl =
    "https://api.github.com/repos/germanbravolopez/laideal/releases/latest";
static const char *kInstallerAssetPrefix = "laideal_setup_";

Updater::InstallerAsset Updater::findInstallerAsset(const QJsonArray &assets, const QString &tag)
{
    InstallerAsset found;
    QString installerName;
    QJsonObject chosen;
    const QString exactName = tag.isEmpty() ? QString()
        : QString::fromLatin1(kInstallerAssetPrefix) + tag + QStringLiteral(".exe");
    for (const QJsonValue &v : assets) {
        const QJsonObject a = v.toObject();
        const QString name  = a.value("name").toString();
        if (!name.startsWith(QString::fromLatin1(kInstallerAssetPrefix))
            || !name.endsWith(".exe", Qt::CaseInsensitive))
            continue;
        if (name == exactName) {
            chosen = a;
            installerName = name;
            break;
        }
        if (installerName.isEmpty()) {   // the first prefix match stays the fallback
            chosen = a;
            installerName = name;
        }
    }
    if (!installerName.isEmpty()) {
        found.url  = QUrl(chosen.value("browser_download_url").toString());
        found.size = chosen.value("size").toVariant().toLongLong();
        const QString digest = chosen.value("digest").toString();
        if (digest.startsWith(QLatin1String("sha256:")))
            found.sha256 = parseChecksumFile(digest.mid(7).toLatin1());
    }
    for (const QJsonValue &v : assets) {
        const QJsonObject a = v.toObject();
        if (!installerName.isEmpty() && a.value("name").toString() == installerName + ".sha256")
            found.checksumUrl = QUrl(a.value("browser_download_url").toString());
    }
    return found;
}

QString Updater::installerFileName(const QUrl &url)
{
    static const QRegularExpression rx(R"(^laideal_setup_\d+\.\d+\.exe$)");
    const QString name = url.fileName();
    return rx.match(name).hasMatch() ? name : QStringLiteral("laideal_setup.exe");
}

bool Updater::isTrustedDownloadUrl(const QUrl &url, bool allowLoopback)
{
    const QString host = url.host().toLower();
    if (allowLoopback && url.scheme() == QLatin1String("http")
        && (host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost")))
        return true;
    return url.isValid() && url.scheme() == QLatin1String("https")
           && (host == QLatin1String("github.com") || host.endsWith(QLatin1String(".githubusercontent.com")));
}

QString Updater::parseChecksumFile(const QByteArray &content)
{
    static const QRegularExpression rx(R"(^\s*([0-9a-fA-F]{64})(\s|$))");
    const QRegularExpressionMatch m = rx.match(QString::fromLatin1(content));
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

QString Updater::installerMismatch(const QByteArray &bytes, qint64 expectedSize, const QString &expectedSha256)
{
    if (expectedSha256.isEmpty())
        return QStringLiteral("La versión no publica la suma SHA-256 del instalador.");
    if (expectedSize > 0 && bytes.size() != expectedSize)
        return QStringLiteral("El instalador descargado está incompleto (%1 de %2 bytes).").arg(bytes.size()).arg(expectedSize);
    if (QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != expectedSha256)
        return QStringLiteral("El instalador descargado no coincide con el publicado (SHA-256 distinta).");
    return QString();
}

Updater::Updater(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_apiUrl(QString::fromLatin1(kGitHubApiLatestUrl))
    , m_silentOnNoUpdate(false)
{
}

QString Updater::currentVersion()
{
    return QString(PROJECT_VERSION);
}

static const QRegularExpression &versionRegex()
{
    static const QRegularExpression rx(R"(^v?(\d+)\.(\d+)$)");
    return rx;
}

bool Updater::isValidVersion(const QString &version)
{
    return versionRegex().match(version).hasMatch();
}

int Updater::compareVersions(const QString &a, const QString &b)
{
    const QRegularExpressionMatch ma = versionRegex().match(a);
    const QRegularExpressionMatch mb = versionRegex().match(b);
    if (!ma.hasMatch() || !mb.hasMatch())
        return 0;   // never "newer"
    const int majA = ma.captured(1).toInt();
    const int minA = ma.captured(2).toInt();
    const int majB = mb.captured(1).toInt();
    const int minB = mb.captured(2).toInt();
    if (majA != majB) return majA - majB;
    return minA - minB;
}

void Updater::checkForUpdates(bool silentOnNoUpdate)
{
    if (m_checkInFlight) {
        // Overlapping check: do not start another request. A manual click still
        // wants its answer shown, so it turns the running check non-silent.
        if (!silentOnNoUpdate)
            m_checkSilent = false;
        return;
    }
    m_checkInFlight = true;
    m_checkSilent = silentOnNoUpdate;

    QNetworkRequest req{m_apiUrl};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("LAIDEAL/%1").arg(currentVersion()));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(10000);

    qDebug() << "Updater: checking for updates at" << m_apiUrl.toString()
             << "(current" << currentVersion() << ", silent=" << silentOnNoUpdate << ")";

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReleaseReply(reply);
    });
}

Updater::Release Updater::parseRelease(const QByteArray &json, const QString &installedVersion, bool allowLoopback)
{
    Release release;
    QJsonParseError jerr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &jerr);
    if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
        release.error = QStringLiteral("Respuesta JSON inválida: %1").arg(jerr.errorString());
        return release;
    }
    const QJsonObject obj = doc.object();
    release.tag   = obj.value("tag_name").toString();
    release.notes = obj.value("body").toString();

    if (release.tag.isEmpty()) {
        release.error = QStringLiteral("La respuesta de GitHub no contiene 'tag_name'.");
        return release;
    }
    if (!isValidVersion(release.tag)) {
        release.error = QStringLiteral("La etiqueta de la última versión no es válida: %1").arg(release.tag);
        return release;
    }
    release.newer = compareVersions(release.tag, installedVersion) > 0;
    if (!release.newer)
        return release;

    release.installer = findInstallerAsset(obj.value("assets").toArray(), release.tag);
    if (!release.installer.url.isValid()) {
        release.error = QStringLiteral(
            "La versión %1 no incluye un instalador 'laideal_setup_X.Y.exe' "
            "en sus assets de GitHub.").arg(release.tag);
    } else if (!isTrustedDownloadUrl(release.installer.url, allowLoopback)
               || (release.installer.sha256.isEmpty() && !isTrustedDownloadUrl(release.installer.checksumUrl, allowLoopback))) {
        release.error = QStringLiteral("La versión %1 no publica un instalador que se pueda comprobar "
                                       "(dirección de GitHub y suma SHA-256).").arg(release.tag);
    }
    return release;
}

QString Updater::releaseNotesForUi(const QString &body)
{
    static const QRegularExpression heading(R"((?m)^## +(.+?)\s*$)");
    QRegularExpressionMatchIterator it = heading.globalMatch(body);
    int start = -1;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (start >= 0)
            return body.mid(start, m.capturedStart() - start).trimmed();
        if (m.captured(1).compare(QStringLiteral("Español"), Qt::CaseInsensitive) == 0)
            start = m.capturedEnd();
    }
    return start >= 0 ? body.mid(start).trimmed() : body;
}

QString Updater::describeNetworkError(QNetworkReply::NetworkError error, int httpStatus, const QString &rawText)
{
    if (httpStatus == 403 || httpStatus == 429)
        return QStringLiteral("GitHub ha limitado las consultas desde esta red; vuelva a intentarlo dentro de una hora.");
    if (httpStatus == 404)
        return QStringLiteral("GitHub no encuentra la versión publicada (404).");
    switch (error) {
    case QNetworkReply::TimeoutError:
        return QStringLiteral("GitHub no ha respondido a tiempo.");
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
        return QStringLiteral("No se puede conectar con GitHub; revise la conexión a internet.");
    case QNetworkReply::SslHandshakeFailedError:
        return QStringLiteral("No se pudo establecer una conexión segura con GitHub.");
    default:
        return QStringLiteral("No se pudo consultar GitHub: %1").arg(rawText);
    }
}

void Updater::handleReleaseReply(QNetworkReply *reply)
{
    reply->deleteLater();
    m_checkInFlight = false;
    m_silentOnNoUpdate = m_checkSilent;   // read by the slots of the signals below

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Updater: GitHub request failed:" << reply->errorString();
        emit checkFailed(describeNetworkError(
            reply->error(), reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
            reply->errorString()));
        return;
    }

    const Release release = parseRelease(reply->readAll(), currentVersion(), m_allowLoopback);
    if (!release.error.isEmpty()) {
        qWarning() << "Updater:" << release.error;
        emit checkFailed(release.error);
        return;
    }
    qDebug() << "Updater: latest=" << release.tag << "current=" << currentVersion() << "newer=" << release.newer;
    if (!release.newer) {
        emit noUpdateAvailable();
        return;
    }
    m_installer = release.installer;
    emit updateAvailable(release.tag, releaseNotesForUi(release.notes), release.installer.url);
}

void Updater::cancelDownload()
{
    if (!m_downloadReply)
        return;
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    qDebug() << "Updater: download cancelled";
}

void Updater::downloadInstaller(const QUrl &installerUrl)
{
    if (m_downloadReply)
        return;   // one download at a time; a second would race the stale-file cleanup
    // Deliberately NOT the temp dir: on hardened machines %TEMP% is ACL'd to deny
    // execute to the normal (non-elevated) user, so the downloaded installer can't
    // be launched from there (Windows "no se puede acceder ... permisos"). A
    // per-user app data dir grants the user full control (execute), so the
    // installer runs (and ShellExecute "runas" can then elevate it).
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/updates");
    if (!QDir().mkpath(dir)) {
        emit downloadFailed(QStringLiteral("No se pudo acceder al directorio de actualizaciones: %1").arg(dir));
        return;
    }
    // Remove any installer left by a previous update so they do not accumulate.
    const QDir updatesDir(dir);
    for (const QString &stale : updatesDir.entryList({ QStringLiteral("laideal_setup*.exe"), QStringLiteral("*.part") }, QDir::Files))
        QFile::remove(updatesDir.filePath(stale));

    if (installerUrl != m_installer.url) {
        emit downloadFailed(QStringLiteral("No hay datos para comprobar este instalador; vuelva a buscar actualizaciones."));
        return;
    }
    if (!m_installer.sha256.isEmpty()) {
        startInstallerDownload();
        return;
    }
    // The published laideal_setup_X.Y.exe.sha256 first: a few bytes.
    QNetworkRequest req{m_installer.checksumUrl};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LAIDEAL/%1").arg(currentVersion()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    QNetworkReply *reply = m_nam->get(req);
    m_downloadReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_downloadReply = nullptr;
        const QString sha256 = reply->error() == QNetworkReply::NoError && isTrustedDownloadUrl(reply->url(), m_allowLoopback)
                                   ? parseChecksumFile(reply->read(4096)) : QString();
        if (sha256.isEmpty()) {
            emit downloadFailed(QStringLiteral("No se pudo leer la suma SHA-256 publicada del instalador."));
            return;
        }
        m_installer.sha256 = sha256;
        startInstallerDownload();
    });
}

void Updater::startInstallerDownload()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/updates");
    const QString localPath = QDir(dir).filePath(installerFileName(m_installer.url));

    qDebug() << "Updater: downloading" << m_installer.url << "to" << localPath;

    QNetworkRequest req{m_installer.url};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("LAIDEAL/%1").arg(currentVersion()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(60000);   // no byte for a minute: give up instead of hanging

    QNetworkReply *reply = m_nam->get(req);
    m_downloadReply = reply;
    connect(reply, &QNetworkReply::downloadProgress,
            this, &Updater::downloadProgress);
    connect(reply, &QNetworkReply::finished, this, [this, reply, localPath]() {
        handleDownloadReply(reply, localPath);
    });
}

void Updater::handleDownloadReply(QNetworkReply *reply, const QString &localPath)
{
    reply->deleteLater();
    m_downloadReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit downloadFailed(reply->errorString());
        return;
    }
    if (!isTrustedDownloadUrl(reply->url(), m_allowLoopback)) {
        qWarning() << "Updater: the download was redirected to" << reply->url().host();
        emit downloadFailed(QStringLiteral("La descarga se ha redirigido fuera de GitHub; no se instala."));
        return;
    }
    const QByteArray bytes = reply->readAll();
    if (const QString mismatch = installerMismatch(bytes, m_installer.size, m_installer.sha256); !mismatch.isEmpty()) {
        qWarning() << "Updater: installer rejected -" << mismatch;
        emit downloadFailed(mismatch);
        return;
    }

    // Written as .part and renamed: a cut-off write never leaves a runnable installer.
    const QString partPath = localPath + QStringLiteral(".part");
    QFile out(partPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit downloadFailed(QStringLiteral("No se pudo crear el archivo:\n%1").arg(partPath));
        return;
    }
    if (out.write(bytes) != bytes.size() || !out.flush()) {
        out.close();
        QFile::remove(partPath);
        emit downloadFailed(QStringLiteral("Escritura incompleta en:\n%1").arg(partPath));
        return;
    }
    out.close();
    QFile::remove(localPath);
    if (!QFile::rename(partPath, localPath)) {
        QFile::remove(partPath);
        emit downloadFailed(QStringLiteral("No se pudo guardar el instalador en:\n%1").arg(localPath));
        return;
    }

    qDebug() << "Updater: download complete," << bytes.size() << "bytes ->" << localPath;
    emit downloadFinished(localPath);
}
