#ifndef UPDATER_H
#define UPDATER_H

#include <QObject>
#include <QString>
#include <QUrl>

class QJsonArray;
class QNetworkAccessManager;
class QNetworkReply;

// Async GitHub-releases-based updater.
//
// checkForUpdates() queries
//   GET https://api.github.com/repos/germanbravolopez/laideal/releases/latest
// compares the returned tag_name to the compile-time PROJECT_VERSION, and emits
// either updateAvailable (newer release exists), noUpdateAvailable, or
// checkFailed. The release's laideal_setup_X.Y.exe asset URL is forwarded so
// the caller can pass it to downloadInstaller().
//
// downloadInstaller() saves the installer .exe under the per-user app data dir
// (as .part, renamed once its size and SHA-256 match what the release publishes:
// the asset's "digest" or the laideal_setup_X.Y.exe.sha256 asset) and emits
// downloadFinished with the local path. An installer that cannot be verified is
// never handed over to run. The caller is expected to
// launch it detached and quit the app so Inno Setup can replace the files in
// place (the .iss declares CloseApplications + RestartApplications as a
// safety net in case the app has not finished exiting yet).
//
// Network failures and missing assets are treated as no-update for the
// startup-check flow (silentOnNoUpdate=true): we never block the app from
// starting because GitHub is down or the laptop is offline.
class Updater : public QObject
{
    Q_OBJECT

public:
    explicit Updater(QObject *parent = nullptr);

    // Returns the compile-time PROJECT_VERSION as "X.Y".
    static QString currentVersion();

    // True for exactly "X.Y" or "vX.Y" (nothing before or after).
    static bool isValidVersion(const QString &version);

    // Compares two "X.Y" version strings numerically (major-then-minor).
    // Returns >0 if a > b, 0 if equal, <0 if a < b. A leading 'v' is ignored.
    // Anything that is not exactly X.Y ("12.0.1", "nightly") compares equal to
    // everything, so a malformed tag is never reported as newer; callers check
    // isValidVersion() first to report it as a failure.
    static int compareVersions(const QString &a, const QString &b);

    // Kicks off the GitHub /releases/latest request. If silentOnNoUpdate is
    // true, noUpdateAvailable and checkFailed are suppressed in the caller's
    // UI (the signals still fire so the caller can log) - intended for the
    // startup check so offline / no-update is not user-visible.
    void checkForUpdates(bool silentOnNoUpdate);

    // Silent flag of the check whose result is being emitted right now (valid
    // inside the noUpdateAvailable / checkFailed / updateAvailable slots).
    bool isSilentCheck() const { return m_silentOnNoUpdate; }

    // True while a download (checksum or installer) is running.
    bool isDownloading() const { return m_downloadReply != nullptr; }
    // Aborts the running download without emitting downloadFailed.
    void cancelDownload();

    // What a release says about its installer.
    struct InstallerAsset {
        QUrl    url;
        qint64  size = 0;
        QString sha256;        // from the asset's "digest" ("sha256:<hex>"), when GitHub gives it
        QUrl    checksumUrl;   // the laideal_setup_X.Y.exe.sha256 asset
    };
    static InstallerAsset findInstallerAsset(const QJsonArray &assets);
    // https on github.com or *.githubusercontent.com only.
    static bool isTrustedDownloadUrl(const QUrl &url);
    // The lower-case hex digest of a sha256sum line ("<hex>  <file>" or "<hex>"); empty if none.
    static QString parseChecksumFile(const QByteArray &content);
    // Why these installer bytes must not run; empty when size and SHA-256 match.
    static QString installerMismatch(const QByteArray &bytes, qint64 expectedSize, const QString &expectedSha256);

    // Downloads installerUrl to %TEMP%; emits downloadFinished or
    // downloadFailed. The local path under %TEMP% is reused across runs -
    // a subsequent download for the same version overwrites the file.
    void downloadInstaller(const QUrl &installerUrl);

signals:
    void updateAvailable(const QString &latestVersion,
                         const QString &releaseNotes,
                         const QUrl &installerUrl);
    void noUpdateAvailable();
    void checkFailed(const QString &error);

    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString &localPath);
    void downloadFailed(const QString &error);

private:
    void handleReleaseReply(QNetworkReply *reply);
    void startInstallerDownload();
    void handleDownloadReply(QNetworkReply *reply, const QString &localPath);

    QNetworkAccessManager *m_nam;
    bool m_silentOnNoUpdate;
    bool m_checkInFlight = false;
    bool m_checkSilent = true;               // of the check in flight; a manual click makes it false
    QNetworkReply *m_downloadReply = nullptr;
    InstallerAsset m_installer;   // of the release last reported by updateAvailable
};

#endif // UPDATER_H
