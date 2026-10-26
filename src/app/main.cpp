#include "mainwindow.h"
#include "version.h"
#include "appsettings.h"
#include "applanguage.h"
#include "settingsdialog.h"
#include "selfcheckdialog.h"
#include "instancelock.h"
#include "sql_lite.h"
#include "applogger.h"
#include "commandline.h"
#ifdef LAIDEAL_UI_TEST
#include "reporthtml.h"
#endif

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QTimer>

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
    bool danglingSettings = false;
    const QString settingsFile = CommandLine::switchValue(args, QStringLiteral("--settings"), &danglingSettings);
    if (danglingSettings) {
        qCritical() << "main: --settings needs a file";
        QMessageBox::critical(nullptr, QObject::tr("Error de configuración"),
            QObject::tr("Falta el archivo de ajustes después de --settings."));
        return 1;
    }
    if (!settingsFile.isEmpty()) {
        settings->loadFrom(settingsFile);
        qWarning() << "main: using the settings file" << settingsFile;
    } else {
        settings->load();
    }

#ifdef LAIDEAL_UI_TEST
    // UI smoke test build only (CMake LAIDEAL_UI_TEST, never a release): generated PDFs are
    // not opened in a viewer, and `--fake-aeat <url>` sends the direct AEAT client to a
    // local fake AEAT. Anything but 127.0.0.1 is refused.
    ReportHtml::setOpenGeneratedReports(false);
    const int fakeAeatArg = args.indexOf(QStringLiteral("--fake-aeat"));
    QString fakeAeatUrl;
    if (fakeAeatArg > 0 && fakeAeatArg + 1 < args.size()) {
        fakeAeatUrl = args.at(fakeAeatArg + 1);
        if (!fakeAeatUrl.startsWith(QLatin1String("http://127.0.0.1:"))) {
            qCritical() << "main: --fake-aeat must be http://127.0.0.1:<port>, not" << fakeAeatUrl;
            return 1;
        }
        VerifactuIntegration::setAeatEndpointOverride(fakeAeatUrl);
        qWarning() << "main: UI test build, AEAT replaced by the fake at" << fakeAeatUrl;
    }
#endif

    // Localize Qt's built-in strings (standard dialog buttons, etc.) to the configured
    // language; Configuracion re-applies it live when the setting changes.
    AppLanguage::applyQtTranslations(settings->language());

    QIcon icon(":/icons/laideal.ico");
    a.setWindowIcon(icon);

    // `--self-check`: the post-update check on a copy of the newest backup (selfcheck.h);
    // the shop's settings and database are only read.
    if (args.contains(QStringLiteral("--self-check"))) {
        SelfCheckDialog check(settings->filePath());
        QTimer::singleShot(0, &check, &SelfCheckDialog::start);
        check.exec();
        return check.allPassed() ? 0 : 1;
    }

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

    const std::unique_ptr<QLockFile> instance = InstanceLock::acquire(settings->dbPath());
    if (!instance) {
        QMessageBox::information(nullptr, QObject::tr("La Ideal ya está abierto"),
            QObject::tr("La Ideal ya está abierto con esta base de datos. Use la ventana que ya está abierta."));
        return 0;
    }

    MainWindow w;
    w.setWindowState(Qt::WindowMaximized);
    if (!settingsFile.isEmpty())
        w.setWindowTitle(w.windowTitle() + QObject::tr(" - configuración de pruebas: %1").arg(settingsFile));
#ifdef LAIDEAL_UI_TEST
    if (!fakeAeatUrl.isEmpty())
        w.setWindowTitle(w.windowTitle() + QStringLiteral(" - AEAT simulada (prueba de interfaz)"));
#endif

    w.setWindowIcon(icon);

    w.show();
    return a.exec();
}
