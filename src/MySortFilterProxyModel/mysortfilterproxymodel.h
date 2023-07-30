#ifndef MYSORTFILTERPROXYMODEL_H
#define MYSORTFILTERPROXYMODEL_H

#include <QDate>
#include <QSortFilterProxyModel>

#define GASTOS_IDX_ID          0
#define GASTOS_IDX_CLIENT      4
#define GASTOS_IDX_FECHA       5
#define GASTOS_IDX_IMPORTE     7
#define GASTOS_IDX_CONTAB      8

#define INGRESOS_IDX_DATE_RCP  2
#define INGRESOS_IDX_DATE_PAY  3
#define INGRESOS_IDX_DATE_PKU  4
#define INGRESOS_IDX_IMPORTE   5
#define INGRESOS_IDX_SIZE      6

#define LIST_PRENDAS_IDX_NAME  0
#define LIST_PRENDAS_IDX_LIMP  1
#define LIST_PRENDAS_IDX_PLAN  2

class MySortFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    MySortFilterProxyModel(QObject *parent = nullptr);

    QDate filterMinimumDate() const { return minDate; }
    void setFilterMinimumDate(QDate date);

    QDate filterMaximumDate() const { return maxDate; }
    void setFilterMaximumDate(QDate date);

    QString table_name;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    bool dateInRange(QDate date) const;

    QDate minDate;
    QDate maxDate;
};

#endif // MYSORTFILTERPROXYMODEL_H
