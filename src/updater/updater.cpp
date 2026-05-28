#include "updater.h"

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

    // Find the laideal_setup_X.Y.exe asset.
    const QJsonArray assets = obj.value("assets").toArray();
    QUrl installerUrl;
    for (const QJsonValue &v : assets) {
        const QJsonObject a = v.toObject();
        const QString name  = a.value("name").toString();
        if (name.startsWith(QString::fromLatin1(kInstallerAssetPrefix))
            && name.endsWith(".exe", Qt::CaseInsensitive)) {
            installerUrl = QUrl(a.value("browser_download_url").toString());
            break;
        }
    }
    if (!installerUrl.isValid()) {
        emit checkFailed(QStringLiteral(
            "La versión %1 no incluye un instalador 'laideal_setup_X.Y.exe' "
            "en sus assets de GitHub.").arg(latestTag));
        return;
    }

    emit updateAvailable(latestTag, notes, installerUrl);
}

void Updater::downloadInstaller(const QUrl &installerUrl)
{
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (!QDir().mkpath(tempDir)) {
        emit downloadFailed(QStringLiteral("No se pudo acceder al directorio temporal: %1").arg(tempDir));
        return;
    }
    const QString fileName = installerUrl.fileName();
    const QString localPath = QDir(tempDir).filePath(fileName.isEmpty()
                                                     ? QStringLiteral("laideal_setup.exe")
                                                     : fileName);

    qDebug() << "Updater: downloading" << installerUrl << "to" << localPath;

    QNetworkRequest req{installerUrl};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("LAIDEAL/%1").arg(currentVersion()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

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

    QFile out(localPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit downloadFailed(QStringLiteral("No se pudo crear el archivo:\n%1").arg(localPath));
        return;
    }
    const QByteArray bytes = reply->readAll();
    if (out.write(bytes) != bytes.size()) {
        out.close();
        QFile::remove(localPath);
        emit downloadFailed(QStringLiteral("Escritura incompleta en:\n%1").arg(localPath));
        return;
    }
    out.close();

    qDebug() << "Updater: download complete," << bytes.size() << "bytes ->" << localPath;
    emit downloadFinished(localPath);
}
