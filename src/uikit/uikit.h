#ifndef UIKIT_H
#define UIKIT_H

// The shared look of the application's dialogs (the style of Generar contabilidad
// and Rectificar factura), in one place: window setup, the explanation panel at the
// top, the in-window result panel that replaces message boxes, the primary and
// secondary buttons and the closing row. A dialog is built in code from these
// pieces, so a change of style is made here once.

#include <QLabel>
#include <QString>
#include <QStringList>

#include <functional>

class QDate;
class QDateEdit;
class QDialog;
class QHBoxLayout;
class QLineEdit;
class QPushButton;

namespace UiKit {

// Window flags shared by every dialog: no "?" help button, no minimise (a dialog
// is never sent behind its window). Sets the title and a minimum width.
void setUpDialog(QDialog *dialog, const QString &title, int minimumWidth = 520);

// Window heading (bold, larger than the app font), for windows that list data.
QLabel *heading(const QString &text);

// Framed explanation at the top of a dialog: what it does, in one or two sentences.
QLabel *introPanel(const QString &text);

// The action the dialog exists for: bold, default (Enter), wide.
QPushButton *primaryButton(const QString &text, const QString &objectName);
// Any other action: never takes Enter from the primary one.
QPushButton *secondaryButton(const QString &text, const QString &objectName);
// Date field of every dialog: calendar popup, dd-MM-yyyy, wide enough for the date.
QDateEdit *dateEdit(const QDate &date, const QString &objectName);
// Enter in `edit` runs `action` and stops there. A plain returnPressed connection lets
// the key go on to the dialog's default button too, which then ran a second time
// (Imprimir printed twice) or ran another action (a search also pressed Añadir prenda).
void onEnter(QLineEdit *edit, std::function<void()> action);
// Right-aligned "Cerrar" (objectName btnClose) that closes the dialog.
QHBoxLayout *closeRow(QDialog *dialog, const QString &text = QStringLiteral("Cerrar"));

// Names for a list or combo box in Spanish alphabetical order: case-insensitive, an
// accented letter next to the plain one (Álvarez beside Alvarez, not after Z).
QStringList sortedNames(QStringList names);

// Result-panel phrases: green success, amber nothing done / attention, red error.
QString okHtml(const QString &text);
QString warnHtml(const QString &text);
QString errorHtml(const QString &text);
// "PDF: <name>" linking to the file, which the result panel opens on click.
QString fileLinkHtml(const QString &file, const QString &label = QStringLiteral("PDF"));

// Framed panel where a dialog reports every outcome (objectName lblResult), in
// place of message boxes. setText() takes rich text built with the helpers above;
// links open the file or URL.
class ResultPanel : public QLabel
{
public:
    explicit ResultPanel(const QString &initialText = QString(), QWidget *parent = nullptr);
    void showInfo(const QString &plainText);   // neutral text, newlines kept
};

} // namespace UiKit

#endif // UIKIT_H
