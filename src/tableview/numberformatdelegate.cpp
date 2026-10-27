#include "numberformatdelegate.h"
#include "sql_lite.h"

#include <QLineEdit>

NumberFormatDelegate::NumberFormatDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QString NumberFormatDelegate::displayText(const QVariant &value, const QLocale &) const
{
    const QString stored = value.toString().trimmed();
    return stored.isEmpty() ? QString() : displayMoney(stored);
}

void NumberFormatDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    if (auto *line = qobject_cast<QLineEdit *>(editor)) {
        line->setText(displayDecimal(index.data(Qt::EditRole).toString()));
        return;
    }
    QStyledItemDelegate::setEditorData(editor, index);
}

void NumberFormatDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    if (auto *line = qobject_cast<QLineEdit *>(editor)) {
        // An amount is stored in cents; other text goes to the model as typed, so its
        // validator refuses it with a message (or, when empty, clears the cell).
        const QString typed = line->text().trimmed();
        model->setData(index, isAmount(typed) ? storedNumber(typed) : typed, Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}

QString NumberFormatDelegate::storedNumber(const QString &typed)
{
    return moneyText(typed);   // "12," and "2,5" are stored as "12.00" and "2.50", like every amount writer
}

bool NumberFormatDelegate::isAmount(const QString &typed)
{
    return moneyParse(typed, nullptr);
}

QString DecimalCommaDelegate::displayText(const QVariant &value, const QLocale &) const
{
    return displayDecimal(value.toString());
}
