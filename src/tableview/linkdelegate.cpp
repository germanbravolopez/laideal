#include "linkdelegate.h"

void LinkDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    if (!index.data().toString().isEmpty()) {
        option->palette.setColor(QPalette::Text, Qt::blue);
        option->font.setUnderline(true);
    }
}
