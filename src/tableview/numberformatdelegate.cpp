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
        // Anything that is not an amount is refused: the cell keeps its value.
        if (!isAmount(line->text()))
            return;
        model->setData(index, storedNumber(line->text()), Qt::EditRole);
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
