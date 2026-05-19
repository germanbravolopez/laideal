#include "mainwindow.h"
#include "version.h"
#include "appsettings.h"
#include "settingsdialog.h"
#include "sql_lite.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle("fusion");

    AppSettings *settings = AppSettings::instance();
    settings->load();

    // If the DB path is not set or the file is missing, prompt the user to
    // configure it before the main window is constructed. Without a valid
    // database the app cannot function.
    bool dbValid = !settings->dbPath().isEmpty() && QFile::exists(settings->dbPath());
    if (!dbValid) {
        SettingsDialog dlg;
        dlg.setWindowTitle(QObject::tr("Configuración inicial — se requiere la base de datos"));
        if (dlg.exec() != QDialog::Accepted
                || settings->dbPath().isEmpty()
                || !QFile::exists(settings->dbPath())) {
            QMessageBox::critical(nullptr, QObject::tr("Error de configuración"),
                QObject::tr("La base de datos no está configurada o no existe.\n"
                            "La aplicación no puede continuar."));
            return 1;
        }
    }

    setDbPath(settings->dbPath());

    MainWindow w;
    w.setWindowState(Qt::WindowMaximized);

    QString iconPath = settings->iconPath();
    if (!iconPath.isEmpty()) {
        QIcon icon(iconPath);
        a.setWindowIcon(icon);
        w.setWindowIcon(icon);
    }

    w.show();
    return a.exec();
}
