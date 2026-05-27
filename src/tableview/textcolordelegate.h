#ifndef TEXTCOLORDELEGATE_H
#define TEXTCOLORDELEGATE_H

#include <QStyledItemDelegate>

class QTableView; // Forward declaration

class TextColorDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit TextColorDelegate(QTableView* tableView, QObject* parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;

private:
    QTableView* m_tableView;
};

#endif // TEXTCOLORDELEGATE_H
