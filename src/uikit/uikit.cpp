#include "uikit.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QCollator>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>

#include <algorithm>

namespace UiKit {

const char *const COLOR_OK    = "#1B7F3B";
const char *const COLOR_WARN  = "#8A5300";
const char *const COLOR_ERROR = "#C62828";
const char *const COLOR_LINK  = "#0B57D0";

namespace {
const int kPanelMargin = 8;
const int kPrimaryMinWidth = 220;
const int kResultMinHeight = 64;
const int kDateMinWidth = 140;
const char *const kPrimaryProperty = "uikitPrimary";

// Consumes Return / Enter on its line edit after running the action.
class EnterFilter : public QObject
{
public:
    EnterFilter(QLineEdit *edit, std::function<void()> action)
        : QObject(edit), m_action(std::move(action)) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                // Once per press (holding the key must not print twice), and only for
                // input the field accepts, as QLineEdit's own returnPressed.
                auto *edit = qobject_cast<QLineEdit *>(watched);
                if (!key->isAutoRepeat() && (!edit || edit->hasAcceptableInput()))
                    m_action();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> m_action;
};
}

void onEnter(QLineEdit *edit, std::function<void()> action)
{
    edit->installEventFilter(new EnterFilter(edit, std::move(action)));
}

namespace {

// The dialog-wide Enter rule of setUpDialog (see uikit.h). A key reaches the dialog
// only when the focused widget did not use it: a line edit or spin box does not, a
// focused auto-default button presses itself and never gets here.
class DialogEnterFilter : public QObject
{
public:
    explicit DialogEnterFilter(QDialog *dialog) : QObject(dialog), m_dialog(dialog) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != m_dialog || event->type() != QEvent::KeyPress)
            return QObject::eventFilter(watched, event);
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter)
            return QObject::eventFilter(watched, event);
        if (key->isAutoRepeat())
            return true;
        // A focused button that ignored Enter (secondary buttons are not auto-default)
        // presses itself: Enter on Cerrar must never fire the primary action.
        if (auto *focused = qobject_cast<QAbstractButton *>(m_dialog->focusWidget());
                focused && focused->isEnabled()) {
            focused->click();
            return true;
        }
        // Only a text-entry widget turns Enter into the primary action; Enter on a table,
        // check box, list or fixed combo box must not trigger (or send, or void) anything.
        if (!isTextEntry(m_dialog->focusWidget()))
            return true;
        for (QPushButton *button : m_dialog->findChildren<QPushButton *>()) {
            // Not the primary of a child window parented to this dialog.
            if (button->window() == m_dialog && button->property(kPrimaryProperty).toBool()
                    && button->isVisible() && button->isEnabled()) {
                button->click();
                return true;
            }
        }
        return true;                            // no primary action: Enter in a field does nothing
    }

private:
    // A line edit (also the one inside a spin box or an editable combo box) or a spin box.
    static bool isTextEntry(const QWidget *widget)
    {
        if (!widget)
            return false;
        if (qobject_cast<const QLineEdit *>(widget) || qobject_cast<const QAbstractSpinBox *>(widget))
            return true;
        const auto *combo = qobject_cast<const QComboBox *>(widget);
        return combo && combo->isEditable();
    }

    QDialog *m_dialog;
};

} // namespace

void setUpDialog(QDialog *dialog, const QString &title, int minimumWidth)
{
    dialog->setWindowTitle(title);
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog->setWindowFlag(Qt::WindowMinimizeButtonHint, false);
    dialog->setMinimumWidth(minimumWidth);
    dialog->installEventFilter(new DialogEnterFilter(dialog));
}

QLabel *heading(const QString &text)
{
    QLabel *label = new QLabel(text);
    label->setObjectName(QStringLiteral("lblHeading"));
    emphasize(label, 5);
    return label;
}

void emphasize(QWidget *widget, qreal extraPoints)
{
    QFont font = widget->font();
    font.setPointSizeF(font.pointSizeF() + extraPoints);
    font.setBold(true);
    widget->setFont(font);
}

void lockDate(QDateEdit *edit, const QString &toolTip)
{
    edit->setReadOnly(true);
    edit->setButtonSymbols(QAbstractSpinBox::NoButtons);
    if (!toolTip.isEmpty())
        edit->setToolTip(toolTip);
}

QLabel *introPanel(const QString &text)
{
    QLabel *label = new QLabel(text);
    label->setObjectName(QStringLiteral("lblIntro"));
    label->setWordWrap(true);
    label->setFrameShape(QFrame::StyledPanel);
    label->setContentsMargins(kPanelMargin, kPanelMargin, kPanelMargin, kPanelMargin);
    return label;
}

QPushButton *primaryButton(const QString &text, const QString &objectName)
{
    QPushButton *button = new QPushButton(text);
    button->setObjectName(objectName);
    button->setDefault(true);
    button->setProperty(kPrimaryProperty, true);
    button->setMinimumWidth(kPrimaryMinWidth);
    QFont font = button->font();
    font.setBold(true);
    button->setFont(font);
    return button;
}

QPushButton *secondaryButton(const QString &text, const QString &objectName)
{
    QPushButton *button = new QPushButton(text);
    button->setObjectName(objectName);
    button->setAutoDefault(false);
    return button;
}

QDateEdit *dateEdit(const QDate &date, const QString &objectName)
{
    QDateEdit *edit = new QDateEdit(date);
    edit->setObjectName(objectName);
    edit->setCalendarPopup(true);
    edit->setDisplayFormat(QStringLiteral("dd-MM-yyyy"));
    edit->setMinimumWidth(kDateMinWidth);
    return edit;
}

QHBoxLayout *closeRow(QDialog *dialog, const QString &text)
{
    QHBoxLayout *row = new QHBoxLayout();
    row->addStretch();
    QPushButton *close = secondaryButton(text.isEmpty() ? QCoreApplication::translate("UiKit", "Cerrar") : text,
                                           QStringLiteral("btnClose"));
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    row->addWidget(close);
    return row;
}

QStringList sortedNames(QStringList names)
{
    QCollator collator(QLocale(QLocale::Spanish, QLocale::Spain));
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(), collator);
    return names;
}

QString okHtml(const QString &text)    { return QStringLiteral("<b style='color:%1'>").arg(COLOR_OK) + text + "</b>"; }
QString warnHtml(const QString &text)  { return QStringLiteral("<b style='color:%1'>").arg(COLOR_WARN) + text + "</b>"; }
QString errorHtml(const QString &text) { return QStringLiteral("<b style='color:%1'>").arg(COLOR_ERROR) + text + "</b>"; }

QString fileLinkHtml(const QString &file, const QString &label)
{
    // Percent-encoded and HTML-escaped, so a path with an apostrophe or & keeps the link whole.
    const QString href = QString::fromLatin1(QUrl::fromLocalFile(file).toEncoded()).toHtmlEscaped();
    return label.toHtmlEscaped() + ": <a href=\"" + href + "\">"
           + QFileInfo(file).fileName().toHtmlEscaped() + "</a>";
}

AmountSpinBox::AmountSpinBox(QWidget *parent)
    : QDoubleSpinBox(parent)
{
    setLocale(QLocale(QLocale::Spanish, QLocale::Spain));
    setGroupSeparatorShown(false);
}

// "." is the decimal key unless a comma is already there, then it is a thousands dot ("1.234,56").
static QString withDecimalComma(QString text)
{
    if (text.contains(QLatin1Char(',')))
        return text.remove(QLatin1Char('.'));
    return text.replace(QLatin1Char('.'), QLatin1Char(','));
}

QValidator::State AmountSpinBox::validate(QString &input, int &pos) const
{
    input = withDecimalComma(input);
    return QDoubleSpinBox::validate(input, pos);
}

double AmountSpinBox::valueFromText(const QString &text) const
{
    return QDoubleSpinBox::valueFromText(withDecimalComma(text));
}

ResultPanel::ResultPanel(const QString &initialText, QWidget *parent)
    : QLabel(parent)
{
    setObjectName(QStringLiteral("lblResult"));
    setWordWrap(true);
    setFrameShape(QFrame::StyledPanel);
    setContentsMargins(kPanelMargin, kPanelMargin, kPanelMargin, kPanelMargin);
    setMinimumHeight(kResultMinHeight);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setTextFormat(Qt::RichText);
    setTextInteractionFlags(Qt::TextBrowserInteraction);
    setOpenExternalLinks(true);
    showInfo(initialText);
}

void ResultPanel::showInfo(const QString &plainText)
{
    setText(plainText.toHtmlEscaped().replace('\n', QStringLiteral("<br>")));
}

} // namespace UiKit
