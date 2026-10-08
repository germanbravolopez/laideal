#include "applanguage.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QTranslator>

namespace AppLanguage {

QString installerChoice()
{
#ifdef Q_OS_WIN
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\La Ideal"), QSettings::NativeFormat);
    return reg.value(QStringLiteral("Language")).toString();
#else
    return QString();
#endif
}

QString initialLanguage(const QString &installerChoice)
{
    return installerChoice == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("es");
}

void applyQtTranslations(const QString &language)
{
    static QTranslator qtTranslator;
    static bool installed = false;
    if (language == QLatin1String("en")) {
        // English is Qt's source language: removing the catalogue is enough.
        if (installed)
            installed = !QCoreApplication::removeTranslator(&qtTranslator);
        return;
    }
    if (installed)
        return;
    // Bundled as a resource: the release deploys with windeployqt --no-translations.
    if (qtTranslator.load(QStringLiteral(":/i18n/qtbase_es.qm")))
        installed = QCoreApplication::installTranslator(&qtTranslator);
    else
        qWarning() << "AppLanguage: could not load bundled qtbase_es translations";
}

QString releaseNotesResource(const QString &language)
{
    return language == QLatin1String("en") ? QStringLiteral(":/docs/releases_notes.txt")
                                           : QStringLiteral(":/docs/releases_notes_es.txt");
}

} // namespace AppLanguage
