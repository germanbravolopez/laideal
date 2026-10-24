#include "mysortfilterproxymodel.h"
#include "ingresos_schema.h"
#include "sql_lite.h"
#include <QtWidgets>
#include <QMessageBox>
#include <QDebug>

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

bool MySortFilterProxyModel::rowLocked(int proxyRow) const
{
    if (table_name != QLatin1String("gastos"))
        return false;
    const QDate fecha = QDate::fromString(index(proxyRow, GASTOS_IDX_FECHA).data().toString().trimmed(), "dd-MM-yyyy");
    return closedQuarters.contains(quarterKey(fecha));
}

Qt::ItemFlags MySortFilterProxyModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QSortFilterProxyModel::flags(index);
    // Invoice rows have no edit path at all (Verifactu req. 1): not only a view flag.
    if (table_name == QLatin1String("ingresos"))
        f &= ~Qt::ItemIsEditable;
    else if (index.isValid() && rowLocked(index.row()))
        f &= ~Qt::ItemIsEditable;
    return f;
}

bool MySortFilterProxyModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (table_name == QLatin1String("ingresos")) {
        qWarning() << "MySortFilterProxyModel: write to ingresos refused (column" << index.column() << ")";
        return false;
    }
    if (role == Qt::EditRole && editValidator) {
        const QString refusal = editValidator(index, value);
        if (!refusal.isEmpty()) {
            emit editRefused(refusal);
            return false;
        }
    }
    return QSortFilterProxyModel::setData(index, value, role);
}

void MySortFilterProxyModel::setNormalizedFilter(const QString &normalizedText, int column)
{
    m_normalizedFilterText = normalizedText;
    m_filterColumn = column;
    invalidateFilter();
}

void MySortFilterProxyModel::setFilters(const QRegularExpression &regex, const QString &normalizedText, int column)
{
    m_ownRegex = regex;
    m_normalizedFilterText = normalizedText;
    m_filterColumn = column;
    invalidateFilter();   // one pass for both
}

int MySortFilterProxyModel::searchColumnCount() const
{
    const int columns = sourceModel() ? sourceModel()->columnCount() : 0;
    // ingresos: up to Observaciones; the columns after it hold Verifactu metadata
    // (csv / hash / url / ...) that is not user-facing and must not be searched.
    const int limit = table_name == QLatin1String("ingresos") ? INGRESOS_COL_OBSERVACIONES + 1 : 10;
    return qMin(columns, limit);
}

double MySortFilterProxyModel::numericValue(const QString &text, bool *ok)
{
    return QString(text).trimmed().replace(',', '.').toDouble(ok);
}

bool MySortFilterProxyModel::filterAcceptsRow(int sourceRow,
                                              const QModelIndex &sourceParent) const
{
    const QRegularExpression re = m_ownRegex.pattern().isEmpty() ? filterRegularExpression() : m_ownRegex;
    const bool hasRegex = !re.pattern().isEmpty();
    const bool hasNormalized = !m_normalizedFilterText.isEmpty();

    if (!hasRegex && !hasNormalized)
        return true;

    const int colCount = searchColumnCount();
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

    const int col = left.column();
    const bool isDate = (table_name == "gastos" && col == GASTOS_IDX_FECHA)
                        || (table_name == "ingresos"
                            && (col == INGRESOS_COL_FECHA_RECEPCION || col == INGRESOS_COL_FECHA_PAGO
                                || col == INGRESOS_COL_FECHA_RECOGIDA || col == INGRESOS_COL_FECHA_ANULACION));
    if (isDate) {
        // An empty or invalid date is an invalid QDate and sorts first.
        return QDate::fromString(leftData.toString().trimmed(), "dd-MM-yyyy")
               < QDate::fromString(rightData.toString().trimmed(), "dd-MM-yyyy");
    }

    const bool isNumber = (table_name == "gastos" && (col == GASTOS_IDX_IMPORTE || col == GASTOS_IDX_ID))
                          || (table_name == "ingresos"
                              && (col == INGRESOS_COL_N_RECIBO || col == INGRESOS_COL_IMPORTE
                                  || col == INGRESOS_COL_CANTIDAD || col == INGRESOS_COL_SIZE
                                  || col == INGRESOS_COL_VERIFACTU_RECTIFIES_N_RECIBO))
                          || (table_name == "prendas" && (col == LIST_PRENDAS_IDX_LIMP || col == LIST_PRENDAS_IDX_PLAN));
    if (isNumber) {
        bool leftOk = false, rightOk = false;
        const double l = numericValue(leftData.toString(), &leftOk);
        const double r = numericValue(rightData.toString(), &rightOk);
        if (leftOk && rightOk)
            return l < r;
        if (leftOk != rightOk)
            return !leftOk;   // blank / non-numeric first, like an empty date
    }
    return QString::localeAwareCompare(leftData.toString(), rightData.toString()) < 0;
}
