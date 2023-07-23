#include "textcolordelegate.h"
#include <QApplication>
#include <QPainter>
#include <QTableView>

TextColorDelegate::TextColorDelegate(QTableView* tableView, QObject* parent) : QStyledItemDelegate(parent), m_tableView(tableView) {}

void TextColorDelegate::initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const {
    QStyledItemDelegate::initStyleOption(option, index);

    // Get the text of the cell
    QString text = index.data().toString();

    // Set the color based on the text
    QColor textColor;
    if (text == "SI" || text == "Recogido") {
        textColor = QColor(0, 180, 0); // Darker green
    } else if (text == "NO" || text == "En tienda") {
        textColor = QColor(210, 0, 0); // Darker red
    } else {
        // Use the default text color for other values
        textColor = option->palette.text().color();
    }

    // Set the text color
    option->palette.setColor(QPalette::Text, textColor);
}

void TextColorDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    QStyledItemDelegate::paint(painter, option, index);
}
