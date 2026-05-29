#include "mysortfilterproxymodel.h"
#include "ingresos_schema.h"
#include <QtWidgets>
#include <QMessageBox>

MySortFilterProxyModel::MySortFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

QString MySortFilterProxyModel::removeDiacritics(const QString &text)
{
    // Decompose to NFD so accented chars split into base + combining mark,
    // then strip all combining (non-spacing) marks - handles á,é,í,ó,ú,ñ,ü, etc.
    QString nfd = text.normalized(QString::NormalizationForm_D);
    QString result;
    result.reserve(nfd.size());
    for (const QChar &c : nfd) {
        if (c.category() != QChar::Mark_NonSpacing)
            result += c;
    }
    return result;
}

void MySortFilterProxyModel::setNormalizedFilter(const QString &normalizedText, int column)
{
    m_normalizedFilterText = normalizedText;
    m_filterColumn = column;
    invalidateFilter();
}

void MySortFilterProxyModel::setFilterMinimumDate(QDate date)
{
    minDate = date;
    invalidateFilter();
}

void MySortFilterProxyModel::setFilterMaximumDate(QDate date)
{
    maxDate = date;
    invalidateFilter();
}

bool MySortFilterProxyModel::filterAcceptsRow(int sourceRow,
                                              const QModelIndex &sourceParent) const
{
    const QRegularExpression re = filterRegularExpression();
    const bool hasRegex = !re.pattern().isEmpty();
    const bool hasNormalized = !m_normalizedFilterText.isEmpty();

    if (!hasRegex && !hasNormalized)
        return true;

    // Search only the first 10 source columns. ingresos has 24 columns but the
    // verifactu_* metadata (csv / xml / hash / url / estado / error / rectifies / ...)
    // lives past column 14 and is not user-facing - we'd surface noise (and risk
    // leaking the chained hash into the visible filter) by scanning it.
    int colCount = qMin(sourceModel()->columnCount(), 10);
    for (int c = 0; c < colCount; ++c) {
        QModelIndex idx = sourceModel()->index(sourceRow, c, sourceParent);
        QString cellText = sourceModel()->data(idx).toString();

        if (hasRegex && cellText.contains(re))
            return true;

        if (hasNormalized && (m_filterColumn == -1 || c == m_filterColumn)) {
            if (removeDiacritics(cellText).toLower().contains(m_normalizedFilterText))
                return true;
        }
    }
    return false;
}

bool MySortFilterProxyModel::lessThan(const QModelIndex &left,
                                      const QModelIndex &right) const
{
    QVariant leftData = sourceModel()->data(left);
    QVariant rightData = sourceModel()->data(right);

    if ((table_name == "gastos" && left.column() == GASTOS_IDX_FECHA) ||
        (table_name == "ingresos" &&
                     (left.column() == INGRESOS_COL_FECHA_RECEPCION ||
                      left.column() == INGRESOS_COL_FECHA_PAGO ||
                      left.column() == INGRESOS_COL_FECHA_RECOGIDA)))
        return QDate::fromString(leftData.toString(), "dd-MM-yyyy") < QDate::fromString(rightData.toString(), "dd-MM-yyyy");
    else if ((table_name == "gastos"   && (left.column() == GASTOS_IDX_IMPORTE || left.column() == GASTOS_IDX_ID)) ||
             (table_name == "ingresos" && left.column() == INGRESOS_COL_IMPORTE) ||
             (table_name == "prendas"  && (left.column() == LIST_PRENDAS_IDX_LIMP || left.column() == LIST_PRENDAS_IDX_PLAN)))
        return leftData.toFloat() < rightData.toFloat();
    else
        return QString::localeAwareCompare(leftData.toString(), rightData.toString()) < 0;
}

bool MySortFilterProxyModel::dateInRange(QDate date) const
{
    return (!minDate.isValid() || date > minDate)
            && (!maxDate.isValid() || date < maxDate);
}
