#include "uikit.h"

#include <QDateEdit>
#include <QDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QUrl>

namespace UiKit {

namespace {
const int kPanelMargin = 8;
const int kPrimaryMinWidth = 220;
const int kResultMinHeight = 64;
const int kDateMinWidth = 140;
}

void setUpDialog(QDialog *dialog, const QString &title, int minimumWidth)
{
    dialog->setWindowTitle(title);
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog->setWindowFlag(Qt::WindowMinimizeButtonHint, false);
    dialog->setMinimumWidth(minimumWidth);
}

QLabel *heading(const QString &text)
{
    QLabel *label = new QLabel(text);
    label->setObjectName(QStringLiteral("lblHeading"));
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() + 5);
    font.setBold(true);
    label->setFont(font);
    return label;
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
    QPushButton *close = secondaryButton(text, QStringLiteral("btnClose"));
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    row->addWidget(close);
    return row;
}

QString okHtml(const QString &text)    { return "<b style='color:green'>" + text + "</b>"; }
QString warnHtml(const QString &text)  { return "<b style='color:#b26a00'>" + text + "</b>"; }
QString errorHtml(const QString &text) { return "<b style='color:red'>" + text + "</b>"; }

QString fileLinkHtml(const QString &file, const QString &label)
{
    return label + ": <a href='" + QUrl::fromLocalFile(file).toString() + "'>"
           + QFileInfo(file).fileName().toHtmlEscaped() + "</a>";
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
