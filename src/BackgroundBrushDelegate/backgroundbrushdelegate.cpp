#include "backgroundbrushdelegate.h"
#include <QPainter>

BackgroundBrushDelegate::BackgroundBrushDelegate(int columnIdx, QObject *parent)
    : QStyledItemDelegate(parent), m_columnIdx(columnIdx)
{}

void BackgroundBrushDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    if (index.column() == m_columnIdx)
    {
        QString text = index.data(Qt::DisplayRole).toString();
        QBrush backgroundBrush;
        QColor textColor;

        if (text == "SI" || text == "Recogido")
        {
            backgroundBrush = QColor(Qt::green).lighter(130); // Adjust intensity (150 in this case)
            textColor = Qt::black; // Set the text color to black for green background
        }
        else if (text == "NO" || text == "En tienda")
        {
            backgroundBrush = QColor(Qt::red).lighter(130); // Adjust intensity (150 in this case)
            textColor = Qt::white; // Set the text color to white for red background
        }
        else
        {
            backgroundBrush = option.backgroundBrush;
            textColor = option.palette.color(QPalette::Text);
        }

        painter->save();
        painter->fillRect(option.rect, backgroundBrush);
        painter->setPen(textColor);
        painter->drawText(option.rect, Qt::AlignCenter, text); // Draw the text centered in the cell
        painter->restore();
    }
}
