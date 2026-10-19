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

Updater::InstallerAsset Updater::findInstallerAsset(const QJsonArray &assets)
{
    InstallerAsset found;
    QString installerName;
    for (const QJsonValue &v : assets) {
        const QJsonObject a = v.toObject();
        const QString name  = a.value("name").toString();
        if (name.startsWith(QString::fromLatin1(kInstallerAssetPrefix))
            && name.endsWith(".exe", Qt::CaseInsensitive)) {
            installerName = name;
            found.url  = QUrl(a.value("browser_download_url").toString());
            found.size = a.value("size").toVariant().toLongLong();
            const QString digest = a.value("digest").toString();
            if (digest.startsWith(QLatin1String("sha256:")))
                found.sha256 = parseChecksumFile(digest.mid(7).toLatin1());
            break;
        }
    }
    for (const QJsonValue &v : assets) {
        const QJsonObject a = v.toObject();
        if (!installerName.isEmpty() && a.value("name").toString() == installerName + ".sha256")
            found.checksumUrl = QUrl(a.value("browser_download_url").toString());
    }
    return found;
}

bool Updater::isTrustedDownloadUrl(const QUrl &url)
{
    const QString host = url.host().toLower();
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
    , m_silentOnNoUpdate(false)
{
}

QString Updater::currentVersion()
{
    return QString(PROJECT_VERSION);
}

int Updater::compareVersions(const QString &a, const QString &b)
{
    static const QRegularExpression rx(R"(^v?(\d+)\.(\d+))");
    QRegularExpressionMatch ma = rx.match(a);
    QRegularExpressionMatch mb = rx.match(b);
    if (!ma.hasMatch() || !mb.hasMatch()) {
        // Fall back to lexicographic; better than crashing on a malformed tag.
        return QString::compare(a, b);
    }
    const int majA = ma.captured(1).toInt();
    const int minA = ma.captured(2).toInt();
    const int majB = mb.captured(1).toInt();
    const int minB = mb.captured(2).toInt();
    if (majA != majB) return majA - majB;
    return minA - minB;
}

void Updater::checkForUpdates(bool silentOnNoUpdate)
{
    m_silentOnNoUpdate = silentOnNoUpdate;

    QNetworkRequest req{QUrl(QString::fromLatin1(kGitHubApiLatestUrl))};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("LAIDEAL/%1").arg(currentVersion()));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(10000);

    qDebug() << "Updater: checking for updates at" << kGitHubApiLatestUrl
             << "(current" << currentVersion() << ", silent=" << silentOnNoUpdate << ")";

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReleaseReply(reply);
    });
}

void Updater::handleReleaseReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        const QString err = reply->errorString();
        qWarning() << "Updater: GitHub request failed:" << err;
        emit checkFailed(err);
        return;
    }

    QJsonParseError jerr;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &jerr);
    if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString err = QStringLiteral("Respuesta JSON inválida: %1").arg(jerr.errorString());
        qWarning() << "Updater:" << err;
        emit checkFailed(err);
        return;
    }
    const QJsonObject obj = doc.object();
    const QString latestTag = obj.value("tag_name").toString();
    const QString notes     = obj.value("body").toString();

    if (latestTag.isEmpty()) {
        emit checkFailed(QStringLiteral("La respuesta de GitHub no contiene 'tag_name'."));
        return;
    }

    const int cmp = compareVersions(latestTag, currentVersion());
    qDebug() << "Updater: latest=" << latestTag << "current=" << currentVersion() << "cmp=" << cmp;
    if (cmp <= 0) {
        emit noUpdateAvailable();
        return;
    }

    const InstallerAsset installer = findInstallerAsset(obj.value("assets").toArray());
    if (!installer.url.isValid()) {
        emit checkFailed(QStringLiteral(
            "La versión %1 no incluye un instalador 'laideal_setup_X.Y.exe' "
            "en sus assets de GitHub.").arg(latestTag));
        return;
    }
    if (!isTrustedDownloadUrl(installer.url)
        || (installer.sha256.isEmpty() && !isTrustedDownloadUrl(installer.checksumUrl))) {
        qWarning() << "Updater: release" << latestTag << "has an untrusted installer URL or no SHA-256";
        emit checkFailed(QStringLiteral("La versión %1 no publica un instalador que se pueda comprobar "
                                        "(dirección de GitHub y suma SHA-256).").arg(latestTag));
        return;
    }

    m_installer = installer;
    emit updateAvailable(latestTag, notes, installer.url);
}

void Updater::downloadInstaller(const QUrl &installerUrl)
{
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
    for (const QString &stale : updatesDir.entryList({ QStringLiteral("*.exe"), QStringLiteral("*.part") }, QDir::Files))
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
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QString sha256 = reply->error() == QNetworkReply::NoError && isTrustedDownloadUrl(reply->url())
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
    const QString fileName = m_installer.url.fileName();
    const QString localPath = QDir(dir).filePath(fileName.isEmpty()
                                                 ? QStringLiteral("laideal_setup.exe")
                                                 : fileName);

    qDebug() << "Updater: downloading" << m_installer.url << "to" << localPath;

    QNetworkRequest req{m_installer.url};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("LAIDEAL/%1").arg(currentVersion()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(60000);   // no byte for a minute: give up instead of hanging

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::downloadProgress,
            this, &Updater::downloadProgress);
    connect(reply, &QNetworkReply::finished, this, [this, reply, localPath]() {
        handleDownloadReply(reply, localPath);
    });
}

void Updater::handleDownloadReply(QNetworkReply *reply, const QString &localPath)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit downloadFailed(reply->errorString());
        return;
    }
    if (!isTrustedDownloadUrl(reply->url())) {
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
