#ifndef TEXTCOLORDELEGATE_H
#define TEXTCOLORDELEGATE_H

#include <QStyledItemDelegate>

class QTableView; // Forward declaration

class TextColorDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit TextColorDelegate(QTableView* tableView, QObject* parent = nullptr);

    // Colour a "SI"/"NO"/"Recogido"/"En tienda" cell. A voided garment row
    // (its estado == "Anulado") renders green everywhere, so the "NO" pagado of
    // a cancelled receipt no longer reads as an outstanding debt.
    enum class TextColor { Default, Green, Red };
    static TextColor classify(const QString &cellText, const QString &rowEstado);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;

private:
    QTableView* m_tableView;
};

#endif // TEXTCOLORDELEGATE_H
