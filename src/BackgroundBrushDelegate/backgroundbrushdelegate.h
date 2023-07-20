#ifndef BACKGROUNDBRUSHDELEGATE_H
#define BACKGROUNDBRUSHDELEGATE_H

#include <QStyledItemDelegate>

class BackgroundBrushDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit BackgroundBrushDelegate(int columnIdx, QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    int m_columnIdx;
};

#endif // BACKGROUNDBRUSHDELEGATE_H
