#include "mainwindow.h"
#include "version.h"
#include "appsettings.h"
#include "applanguage.h"
#include "settingsdialog.h"
#include "sql_lite.h"
#include "applogger.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle("fusion");

    AppLogger::install();

    AppSettings *settings = AppSettings::instance();
    // `--settings <file>`: run with another settings file (a test run on the shop PC,
    // e.g. the direct AEAT client against a copy of the database), never touching the
    // shop's own ~/.laideal_settings.json.
    const QStringList args = a.arguments();
    const int settingsArg = args.indexOf(QStringLiteral("--settings"));
    if (settingsArg > 0 && settingsArg + 1 < args.size()) {
        settings->loadFrom(args.at(settingsArg + 1));
        qWarning() << "main: using the settings file" << args.at(settingsArg + 1);
    } else {
        settings->load();
    }

    // Localize Qt's built-in strings (standard dialog buttons, etc.) to the configured
    // language; Configuracion re-applies it live when the setting changes.
    AppLanguage::applyQtTranslations(settings->language());

    // If the DB path is not set or the file is missing, prompt the user to
    // configure it before the main window is constructed. Without a valid
    // database the app cannot function.
    bool dbValid = !settings->dbPath().isEmpty() && QFile::exists(settings->dbPath());
    if (!dbValid) {
        SettingsDialog dlg;
        dlg.setWindowTitle(QObject::tr("Configuración inicial - se requiere la base de datos"));
        if (dlg.exec() != QDialog::Accepted
                || settings->dbPath().isEmpty()
                || !QFile::exists(settings->dbPath())) {
            qCritical() << "main: database path is not set or file does not exist after settings dialog";
            QMessageBox::critical(nullptr, QObject::tr("Error de configuración"),
                QObject::tr("La base de datos no está configurada o no existe.\n"
                            "La aplicación no puede continuar."));
            return 1;
        }
    }

    setDbPath(settings->dbPath());

    MainWindow w;
    w.setWindowState(Qt::WindowMaximized);
    if (settingsArg > 0)
        w.setWindowTitle(w.windowTitle() + QObject::tr(" - configuración de pruebas: %1").arg(args.value(settingsArg + 1)));

    QIcon icon(":/icons/laideal.ico");
    a.setWindowIcon(icon);
    w.setWindowIcon(icon);

    w.show();
    return a.exec();
}
