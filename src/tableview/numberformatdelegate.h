#ifndef NUMBERFORMATDELEGATE_H
#define NUMBERFORMATDELEGATE_H

#include <QStyledItemDelegate>

// Shows a number column with two decimals in the locale's format, and stores an
// edited value with a dot as the decimal separator ("2,5" is written "2.5"), the
// form every price and amount reader expects.
class NumberFormatDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit NumberFormatDelegate(QObject *parent = nullptr);
    QString displayText(const QVariant &value, const QLocale &locale) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
    // The text an edit stores: trimmed, the decimal comma replaced by a dot.
    static QString storedNumber(const QString &typed);
};

#endif // NUMBERFORMATDELEGATE_H
