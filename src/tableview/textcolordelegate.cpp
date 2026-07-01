#include "textcolordelegate.h"
#include "ingresos_schema.h"
#include <QApplication>
#include <QPainter>
#include <QTableView>

TextColorDelegate::TextColorDelegate(QTableView* tableView, QObject* parent) : QStyledItemDelegate(parent), m_tableView(tableView) {}

TextColorDelegate::TextColor TextColorDelegate::classify(const QString &cellText, const QString &rowEstado)
{
    if (rowEstado == QLatin1String(INGRESOS_ESTADO_ANULADO))
        return TextColor::Green;
    if (cellText == "SI" || cellText == "Recogido")
        return TextColor::Green;
    if (cellText == "NO" || cellText == "En tienda")
        return TextColor::Red;
    return TextColor::Default;
}

void TextColorDelegate::initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const {
    QStyledItemDelegate::initStyleOption(option, index);

    const QString text      = index.data().toString();
    const QString rowEstado = index.sibling(index.row(), INGRESOS_COL_ESTADO).data().toString();

    QColor textColor;
    switch (classify(text, rowEstado)) {
    case TextColor::Green:   textColor = QColor(0, 180, 0);  break; // Darker green
    case TextColor::Red:     textColor = QColor(210, 0, 0);  break; // Darker red
    case TextColor::Default: textColor = option->palette.text().color(); break;
    }

    option->palette.setColor(QPalette::Text, textColor);
}

void TextColorDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    QStyledItemDelegate::paint(painter, option, index);
}
