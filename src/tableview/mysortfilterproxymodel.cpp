#include "mysortfilterproxymodel.h"
#include "ingresos_schema.h"
#include "sql_lite.h"
#include "uikit.h"
#include <algorithm>
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

QDate MySortFilterProxyModel::parseFecha(const QString &text)
{
    const QStringView s = QStringView(text).trimmed();
    if (s.size() != 10 || s[2] != QLatin1Char('-') || s[5] != QLatin1Char('-'))
        return QDate();
    bool dayOk = false, monthOk = false, yearOk = false;
    const int day = s.mid(0, 2).toInt(&dayOk);
    const int month = s.mid(3, 2).toInt(&monthOk);
    const int year = s.mid(6, 4).toInt(&yearOk);
    return dayOk && monthOk && yearOk ? QDate(year, month, day) : QDate();
}

MySortFilterProxyModel::Schema MySortFilterProxyModel::schemaForTable(const QString &tableName)
{
    if (tableName == QLatin1String("ingresos")) return Schema::Ingresos;
    if (tableName == QLatin1String("gastos"))   return Schema::Gastos;
    if (tableName == QLatin1String("prendas"))  return Schema::Prendas;
    return Schema::Generic;
}

bool MySortFilterProxyModel::rowLocked(int proxyRow) const
{
    if (schema != Schema::Gastos)
        return false;
    return m_closedQuarters.contains(quarterKey(parseFecha(index(proxyRow, GASTOS_IDX_FECHA).data().toString())));
}

void MySortFilterProxyModel::setClosedQuarters(const QSet<int> &quarterKeys)
{
    if (quarterKeys == m_closedQuarters)
        return;
    m_closedQuarters = quarterKeys;
    if (rowCount() > 0)   // the lock follows the fecha, so the views re-read the flags
        emit dataChanged(index(0, GASTOS_IDX_FECHA), index(rowCount() - 1, GASTOS_IDX_FECHA));
}

Qt::ItemFlags MySortFilterProxyModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QSortFilterProxyModel::flags(index);
    // Invoice rows have no edit path at all (Verifactu req. 1): not only a view flag.
    if (schema == Schema::Ingresos)
        f &= ~Qt::ItemIsEditable;
    else if (schema == Schema::Gastos && index.column() == GASTOS_IDX_ID)
        f &= ~Qt::ItemIsEditable;   // the primary key is never typed over
    else if (index.isValid() && rowLocked(index.row()))
        f &= ~Qt::ItemIsEditable;
    return f;
}

bool MySortFilterProxyModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (schema == Schema::Ingresos) {
        qWarning() << "MySortFilterProxyModel: write to ingresos refused (column" << index.column() << ")";
        return false;
    }
    if (schema == Schema::Gastos && index.column() == GASTOS_IDX_ID)
        return false;
    QVariant stored = value;
    if (role == Qt::EditRole && editValidator) {
        const QString refusal = editValidator(index, value);
        if (!refusal.isEmpty()) {
            emit editRefused(refusal);
            return false;
        }
    }
    // A padded date would be locked correctly but fail the year filter and the sort.
    if (role == Qt::EditRole && schema == Schema::Gastos && index.column() == GASTOS_IDX_FECHA)
        stored = value.toString().trimmed();
    if (QSortFilterProxyModel::setData(index, stored, role))
        return true;
    if (role == Qt::EditRole && schema == Schema::Gastos)
        emit editRefused(UiKit::errorHtml("No se pudo guardar el cambio."));
    return false;
}

QString MySortFilterProxyModel::searchKey(const QString &text)
{
    return removeDiacritics(text).toLower().simplified();
}

void MySortFilterProxyModel::setSearchText(const QString &text, int column)
{
    m_searchWords = searchKey(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    m_filterColumn = column;
    invalidateFilter();
}

int MySortFilterProxyModel::searchColumnCount() const
{
    const int columns = sourceModel() ? sourceModel()->columnCount() : 0;
    // ingresos: up to Observaciones; the columns after it hold Verifactu metadata
    // (csv / hash / url / ...) that is not user-facing and must not be searched.
    const int limit = schema == Schema::Ingresos ? INGRESOS_COL_OBSERVACIONES + 1 : 10;
    return qMin(columns, limit);
}

double MySortFilterProxyModel::numericValue(const QString &text, bool *ok)
{
    return QString(text).trimmed().replace(',', '.').toDouble(ok);
}

bool MySortFilterProxyModel::filterAcceptsRow(int sourceRow,
                                              const QModelIndex &sourceParent) const
{
    if (m_searchWords.isEmpty())
        return true;
    QStringList cells;
    const int colCount = searchColumnCount();
    for (int c = 0; c < colCount; ++c)
        if (m_filterColumn == -1 || c == m_filterColumn)
            cells << searchKey(sourceModel()->index(sourceRow, c, sourceParent).data().toString());
    for (const QString &word : m_searchWords) {
        const bool found = std::any_of(cells.cbegin(), cells.cend(),
                                       [&word](const QString &cell) { return cell.contains(word); });
        if (!found)
            return false;
    }
    return true;
}

bool MySortFilterProxyModel::lessThan(const QModelIndex &left,
                                      const QModelIndex &right) const
{
    QVariant leftData = sourceModel()->data(left);
    QVariant rightData = sourceModel()->data(right);

    const int col = left.column();
    const bool isDate = (schema == Schema::Gastos && col == GASTOS_IDX_FECHA)
                        || (schema == Schema::Ingresos
                            && (col == INGRESOS_COL_FECHA_RECEPCION || col == INGRESOS_COL_FECHA_PAGO
                                || col == INGRESOS_COL_FECHA_RECOGIDA || col == INGRESOS_COL_FECHA_ANULACION));
    if (isDate) {
        // An empty or invalid date is an invalid QDate and sorts first.
        return parseFecha(leftData.toString()) < parseFecha(rightData.toString());
    }

    const bool isNumber = (schema == Schema::Gastos && (col == GASTOS_IDX_IMPORTE || col == GASTOS_IDX_ID))
                          || (schema == Schema::Ingresos
                              && (col == INGRESOS_COL_N_RECIBO || col == INGRESOS_COL_IMPORTE
                                  || col == INGRESOS_COL_CANTIDAD || col == INGRESOS_COL_SIZE
                                  || col == INGRESOS_COL_VERIFACTU_RECTIFIES_N_RECIBO))
                          || (schema == Schema::Prendas && (col == LIST_PRENDAS_IDX_LIMP || col == LIST_PRENDAS_IDX_PLAN));
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
