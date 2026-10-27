#ifndef TEXTCOLORDELEGATE_H
#define TEXTCOLORDELEGATE_H

#include <QStyledItemDelegate>

class QTableView; // Forward declaration

// Ingresos columns only: it reads the row's estado columns by their ingresos position.
class TextColorDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    // The view argument is unused; kept for the existing call sites.
    explicit TextColorDelegate(QTableView* tableView, QObject* parent = nullptr);

    // Colour a "SI"/"NO"/"Recogido"/"En tienda" cell. A cancelled garment row
    // (estado "Anulado", or verifactu_estado "ANULADA") renders green everywhere,
    // so its "NO" pagado no longer reads as an outstanding debt.
    enum class TextColor { Default, Green, Red };
    static TextColor classify(const QString &cellText, const QString &rowEstado,
                              const QString &rowVerifactuEstado = QString());

    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;
};

#endif // TEXTCOLORDELEGATE_H
