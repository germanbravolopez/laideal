#include "mysortfilterproxymodel.h"
#include <QtWidgets>
#include <QMessageBox>

MySortFilterProxyModel::MySortFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
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
    QModelIndex index0 = sourceModel()->index(sourceRow, 0, sourceParent);
    QModelIndex index1 = sourceModel()->index(sourceRow, 1, sourceParent);
    QModelIndex index2 = sourceModel()->index(sourceRow, 2, sourceParent);

    return (sourceModel()->data(index0).toString().contains(filterRegularExpression())
            || sourceModel()->data(index1).toString().contains(filterRegularExpression()))
            && dateInRange(sourceModel()->data(index2).toDate());
}

bool MySortFilterProxyModel::lessThan(const QModelIndex &left,
                                      const QModelIndex &right) const
{
    QVariant leftData = sourceModel()->data(left);
    QVariant rightData = sourceModel()->data(right);

    if (left.column() == FECHA_COLUMN_IDX)
    {
        return QDate::fromString(leftData.toString(), "dd-MM-yyyy") < QDate::fromString(rightData.toString(), "dd-MM-yyyy");
    }
    else
    {
        return QString::localeAwareCompare(leftData.toString(), rightData.toString()) < 0;
    }
}

bool MySortFilterProxyModel::dateInRange(QDate date) const
{
    return (!minDate.isValid() || date > minDate)
            && (!maxDate.isValid() || date < maxDate);
}
