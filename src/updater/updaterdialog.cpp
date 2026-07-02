#include "updaterdialog.h"
#include "updater.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QProcess>
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>

UpdaterDialog::UpdaterDialog(Updater *updater,
                             const QString &latestVersion,
                             const QString &releaseNotes,
                             const QUrl &installerUrl,
                             QWidget *parent)
    : QDialog(parent)
    , m_updater(updater)
    , m_installerUrl(installerUrl)
    , m_latestVersion(latestVersion)
{
    setWindowTitle(tr("Actualización disponible"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);

    m_header = new QLabel(this);
    m_header->setTextFormat(Qt::RichText);
    m_header->setWordWrap(true);
    m_header->setText(tr(
        "<p>Hay una nueva versión disponible:</p>"
        "<p>&nbsp;&nbsp;Versión actual: <b>%1</b><br/>"
        "&nbsp;&nbsp;Nueva versión: <b>%2</b></p>"
        "<p><b>Cambios:</b></p>")
        .arg(Updater::currentVersion(), latestVersion));
    layout->addWidget(m_header);

    m_notes = new QTextEdit(this);
    m_notes->setReadOnly(true);
    m_notes->setPlainText(releaseNotes);
    m_notes->setMinimumHeight(200);
    layout->addWidget(m_notes);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(false);
    layout->addWidget(m_progress);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_btnLater  = new QPushButton(tr("Más tarde"), this);
    m_btnUpdate = new QPushButton(tr("Actualizar ahora"), this);
    m_btnUpdate->setDefault(true);
    btnRow->addWidget(m_btnLater);
    btnRow->addWidget(m_btnUpdate);
    layout->addLayout(btnRow);

    connect(m_btnLater,  &QPushButton::clicked, this, &QDialog::reject);
    connect(m_btnUpdate, &QPushButton::clicked, this, &UpdaterDialog::onUpdateClicked);

    connect(m_updater, &Updater::downloadProgress, this, &UpdaterDialog::onDownloadProgress);
    connect(m_updater, &Updater::downloadFinished, this, &UpdaterDialog::onDownloadFinished);
    connect(m_updater, &Updater::downloadFailed,   this, &UpdaterDialog::onDownloadFailed);
}

void UpdaterDialog::onUpdateClicked()
{
    m_btnUpdate->setEnabled(false);
    m_btnLater->setEnabled(false);
    m_progress->setVisible(true);
    m_header->setText(tr(
        "<p>Descargando <b>laideal %1</b>...</p>"
        "<p>La aplicación se cerrará automáticamente cuando arranque el instalador.</p>")
        .arg(m_latestVersion));

    m_updater->downloadInstaller(m_installerUrl);
}

void UpdaterDialog::onDownloadProgress(qint64 received, qint64 total)
{
    if (total <= 0) {
        m_progress->setRange(0, 0); // indeterminate
        return;
    }
    m_progress->setRange(0, 100);
    m_progress->setValue(static_cast<int>((received * 100) / total));
}

void UpdaterDialog::onDownloadFinished(const QString &localPath)
{
    qDebug() << "UpdaterDialog: launching installer" << localPath;

    // The installer is per-user (laideal.iss: PrivilegesRequired=lowest), so it
    // runs non-elevated and needs no UAC. Inno Setup extracts its bootstrap to
    // %TEMP% and executes it there; on a hardened %TEMP% that denies execute this
    // fails ("Unable to execute file in the temporary directory. Error 5"). Point
    // TMP/TEMP at the execute-allowed per-user folder the installer already lives
    // in - inherited by the child because the launch is NOT elevated (elevation
    // would reset the environment back to the hardened default %TEMP%).
    // CloseApplications=yes in laideal.iss handles files still open under {app}.
    const QByteArray launchDir = QFileInfo(localPath).absolutePath().toLocal8Bit();
    qputenv("TMP", launchDir);
    qputenv("TEMP", launchDir);
    const bool started = QProcess::startDetached(localPath, {});
    if (!started) {
        QMessageBox::critical(this, tr("Error"),
            tr("No se pudo iniciar el instalador descargado:\n%1\n\n"
               "Puedes ejecutarlo manualmente desde esa ruta.").arg(localPath));
        m_btnLater->setEnabled(true);
        m_btnLater->setText(tr("Cerrar"));
        return;
    }

    accept();
    QCoreApplication::quit();
}

void UpdaterDialog::onDownloadFailed(const QString &error)
{
    QMessageBox::warning(this, tr("Descarga fallida"),
        tr("No se pudo descargar el instalador:\n%1").arg(error));
    m_progress->setVisible(false);
    m_btnUpdate->setEnabled(true);
    m_btnLater->setEnabled(true);
    m_header->setText(tr(
        "<p>Hay una nueva versión disponible:</p>"
        "<p>&nbsp;&nbsp;Versión actual: <b>%1</b><br/>"
        "&nbsp;&nbsp;Nueva versión: <b>%2</b></p>"
        "<p><b>Cambios:</b></p>")
        .arg(Updater::currentVersion(), m_latestVersion));
}
