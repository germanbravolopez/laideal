#ifndef MYSORTFILTERPROXYMODEL_H
#define MYSORTFILTERPROXYMODEL_H

#include <QDate>
#include <QSortFilterProxyModel>

#define FECHA_COLUMN_IDX 4

class MySortFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    MySortFilterProxyModel(QObject *parent = nullptr);

    QDate filterMinimumDate() const { return minDate; }
    void setFilterMinimumDate(QDate date);

    QDate filterMaximumDate() const { return maxDate; }
    void setFilterMaximumDate(QDate date);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    bool dateInRange(QDate date) const;

    QDate minDate;
    QDate maxDate;
};

#endif // MYSORTFILTERPROXYMODEL_H
