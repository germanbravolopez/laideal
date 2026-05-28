#ifndef UPDATERDIALOG_H
#define UPDATERDIALOG_H

#include <QDialog>
#include <QString>
#include <QUrl>

class QLabel;
class QTextEdit;
class QProgressBar;
class QPushButton;
class Updater;

// Dialog shown when Updater::updateAvailable fires. Presents the new version,
// release notes (the GitHub release body), and an "Actualizar ahora" button.
// On confirm: drives Updater::downloadInstaller, swaps the dialog to a
// progress bar, launches the downloaded installer detached, and quits the app
// (qApp->quit()) so Inno Setup can replace the running files.
class UpdaterDialog : public QDialog
{
    Q_OBJECT

public:
    UpdaterDialog(Updater *updater,
                  const QString &latestVersion,
                  const QString &releaseNotes,
                  const QUrl &installerUrl,
                  QWidget *parent = nullptr);

private slots:
    void onUpdateClicked();
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished(const QString &localPath);
    void onDownloadFailed(const QString &error);

private:
    Updater *m_updater;
    QUrl m_installerUrl;
    QString m_latestVersion;

    QLabel       *m_header;
    QTextEdit    *m_notes;
    QProgressBar *m_progress;
    QPushButton  *m_btnUpdate;
    QPushButton  *m_btnLater;
};

#endif // UPDATERDIALOG_H
