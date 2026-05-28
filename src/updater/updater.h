#ifndef UPDATER_H
#define UPDATER_H

#include <QObject>
#include <QString>
#include <QUrl>

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
// downloadInstaller() saves the installer .exe to %TEMP%\laideal_setup_X.Y.exe
// and emits downloadFinished with the local path. The caller is expected to
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

    // Compares two "X.Y" version strings numerically (major-then-minor).
    // Returns >0 if a > b, 0 if equal, <0 if a < b. Strings that don't match
    // the X.Y pattern (e.g. "v8.2" with leading 'v') are normalised first.
    static int compareVersions(const QString &a, const QString &b);

    // Kicks off the GitHub /releases/latest request. If silentOnNoUpdate is
    // true, noUpdateAvailable and checkFailed are suppressed in the caller's
    // UI (the signals still fire so the caller can log) - intended for the
    // startup check so offline / no-update is not user-visible.
    void checkForUpdates(bool silentOnNoUpdate);

    bool isSilentCheck() const { return m_silentOnNoUpdate; }

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
    void handleDownloadReply(QNetworkReply *reply, const QString &localPath);

    QNetworkAccessManager *m_nam;
    bool m_silentOnNoUpdate;
};

#endif // UPDATER_H
