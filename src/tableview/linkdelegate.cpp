#include "linkdelegate.h"
#include "uikit.h"

void LinkDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    if (!index.data().toString().isEmpty()) {
        option->palette.setColor(QPalette::Text, QColor(UiKit::COLOR_LINK));
        option->font.setUnderline(true);
    }
}
