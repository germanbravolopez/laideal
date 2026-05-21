#ifndef LINKDELEGATE_H
#define LINKDELEGATE_H

#include <QStyledItemDelegate>

class LinkDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;
};

#endif // LINKDELEGATE_H
