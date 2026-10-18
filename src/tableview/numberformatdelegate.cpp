#include "numberformatdelegate.h"

#include <QLineEdit>

NumberFormatDelegate::NumberFormatDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QString NumberFormatDelegate::displayText(const QVariant &value, const QLocale &locale) const
{
    QString formattedNum = locale.toString(value.toDouble(), 'f', 2);
    return formattedNum;
}

void NumberFormatDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    if (auto *line = qobject_cast<QLineEdit *>(editor)) {
        model->setData(index, storedNumber(line->text()), Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}

QString NumberFormatDelegate::storedNumber(const QString &typed)
{
    return typed.trimmed().replace(QLatin1Char(','), QLatin1Char('.'));
}
