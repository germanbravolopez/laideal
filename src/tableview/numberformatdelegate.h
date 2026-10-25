#ifndef NUMBERFORMATDELEGATE_H
#define NUMBERFORMATDELEGATE_H

#include <QStyledItemDelegate>

// Shows a number column with two decimals and the decimal comma whatever the language
// (an empty cell stays empty), and stores an edited value with a dot ("2,5" is written
// "2.5"), the form every price and amount reader expects.
class NumberFormatDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit NumberFormatDelegate(QObject *parent = nullptr);
    QString displayText(const QVariant &value, const QLocale &locale) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
    // The text an edit stores: trimmed, the decimal comma replaced by a dot.
    static QString storedNumber(const QString &typed);
    // Whether typed text is an amount (optional sign, digits, optional "," or "." and
    // digits); setModelData refuses anything else without storing it.
    static bool isAmount(const QString &typed);
};

// A read-only decimal column kept as stored (a size such as "2.5") shown with the comma.
class DecimalCommaDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QString displayText(const QVariant &value, const QLocale &locale) const override;
};

#endif // NUMBERFORMATDELEGATE_H
