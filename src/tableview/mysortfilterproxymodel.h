#ifndef MYSORTFILTERPROXYMODEL_H
#define MYSORTFILTERPROXYMODEL_H

#include <QDate>
#include <functional>
#include <QSortFilterProxyModel>

// INGRESOS_COL_* lives in sql_lite.h - this header used to also carry an
// INGRESOS_IDX_* duplicate set with diverging names. Consumers that need
// the ingresos column indices must include "sql_lite.h" directly.

#define GASTOS_IDX_ID          0
#define GASTOS_IDX_CLIENT      4
#define GASTOS_IDX_FECHA       5
#define GASTOS_IDX_IMPORTE     7
#define GASTOS_IDX_CONTAB      8

#define LIST_PRENDAS_IDX_NAME  0
#define LIST_PRENDAS_IDX_LIMP  1
#define LIST_PRENDAS_IDX_PLAN  2

class MySortFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    MySortFilterProxyModel(QObject *parent = nullptr);

    // Sets a plain-text filter that matches diacritic-insensitively.
    // column: column index to check, or -1 to check all columns (default).
    void setNormalizedFilter(const QString &normalizedText, int column = -1);

    // Strips Spanish/common diacritics from a string for locale-insensitive comparison.
    static QString removeDiacritics(const QString &text);

    QString table_name;

    // A gastos row locked by Contabilidad (edit_lock = 1) is read-only.
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool rowLocked(int proxyRow) const;

    // Checked before an edit is written: a non-empty message refuses it (editRefused).
    std::function<QString(int column, const QVariant &value)> editValidator;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

signals:
    void editRefused(const QString &message);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_normalizedFilterText;
    int m_filterColumn = -1;
};

#endif // MYSORTFILTERPROXYMODEL_H
