#ifndef APPLANGUAGE_H
#define APPLANGUAGE_H

#include <QString>

// UI language ("es" / "en"). For now only Qt's standard dialogs and the release
// notes follow it; the app's own strings are Spanish literals until the full
// translation pass (tracker: "Full app translation").
namespace AppLanguage {

// Language picked in the Inno Setup installer, which records it in
// HKCU\Software\La Ideal\Language. Empty when absent or not on Windows.
QString installerChoice();

// Language for settings that have none yet: the installer's choice when it is
// "en", else "es" (the shop's language).
QString initialLanguage(const QString &installerChoice);

// Installs (es) or removes (en) Qt's bundled Spanish catalogue for the standard
// dialogs. Takes effect live: dialogs opened afterwards use the new language.
void applyQtTranslations(const QString &language);

// Bundled release-notes resource (Ayuda -> Notas de la version) for the language.
QString releaseNotesResource(const QString &language);

} // namespace AppLanguage

#endif // APPLANGUAGE_H
