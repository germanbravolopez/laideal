#ifndef UIKIT_H
#define UIKIT_H

// The shared look of the application's dialogs (the style of Generar contabilidad
// and Rectificar factura), in one place: window setup, the explanation panel at the
// top, the in-window result panel that replaces message boxes, the primary and
// secondary buttons and the closing row. A dialog is built in code from these
// pieces, so a change of style is made here once.

#include <QDoubleSpinBox>
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

// Call before the dialog is shown (changing a window flag hides a shown window).
// Window flags shared by every dialog: no "?" help button, no minimise (a dialog
// is never sent behind its window). Sets the title and a minimum width. Also the
// dialog's Enter rule: Enter in a text field (line edit, spin or date box, editable
// combo) presses the primary button of this dialog, or nothing when the dialog has none (Qt would press whichever button it made the default, e.g. a
// Buscar that resets the form); Enter on a focused button presses that button; a
// held key is not repeated; Enter on a table, list, check box or fixed combo does nothing. A field whose Enter means something else uses onEnter.
void setUpDialog(QDialog *dialog, const QString &title, int minimumWidth = 520);

// Window heading (bold, larger than the app font), for windows that list data.
QLabel *heading(const QString &text);

// Framed explanation at the top of a dialog: what it does, in one or two sentences.
QLabel *introPanel(const QString &text);

// The action the dialog exists for: bold, default (Enter in a field), wide.
QPushButton *primaryButton(const QString &text, const QString &objectName);
// Any other action: never takes Enter from the primary one.
QPushButton *secondaryButton(const QString &text, const QString &objectName);
// Date field of every dialog: calendar popup, dd-MM-yyyy, wide enough for the date.
QDateEdit *dateEdit(const QDate &date, const QString &objectName);
// Enter in `edit` runs `action` and stops there (once per press, and only while the
// field's input is acceptable). A plain returnPressed connection lets
// the key go on to the dialog's default button too, which then ran a second time
// (Imprimir printed twice) or ran another action (a search also pressed Añadir prenda).
void onEnter(QLineEdit *edit, std::function<void()> action);
// Right-aligned "Cerrar" (default text; objectName btnClose) that closes the dialog.
QHBoxLayout *closeRow(QDialog *dialog, const QString &text = QString());

// Names for a list or combo box in Spanish alphabetical order: case-insensitive, an
// accented letter next to the plain one (Álvarez beside Alvarez, not after Z).
QStringList sortedNames(QStringList names);

// Accessible text colours (at least 4.5:1 on white), shared by the result-panel phrases
// and the table delegates.
extern const char *const COLOR_OK;       // green
extern const char *const COLOR_WARN;     // amber
extern const char *const COLOR_ERROR;    // red
extern const char *const COLOR_LINK;     // blue

// Result-panel phrases: green success, amber nothing done / attention, red error.
// They take HTML and escape nothing: the caller runs user / AEAT text through toHtmlEscaped().
QString okHtml(const QString &text);
QString warnHtml(const QString &text);
QString errorHtml(const QString &text);
// "PDF: <name>" linking to the file, which the result panel opens on click (label is plain text).
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

// Amount field: shows the decimal comma whatever the language, and takes "," or "."
// as the decimal key (the numeric keypad sends either, depending on Windows). With a comma
// already typed or pasted, dots are thousands separators and dropped ("1.234,56").
class AmountSpinBox : public QDoubleSpinBox
{
public:
    explicit AmountSpinBox(QWidget *parent = nullptr);
    QValidator::State validate(QString &input, int &pos) const override;
    double valueFromText(const QString &text) const override;
};

} // namespace UiKit

#endif // UIKIT_H
