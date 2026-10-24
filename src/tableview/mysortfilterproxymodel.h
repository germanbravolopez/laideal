#ifndef MYSORTFILTERPROXYMODEL_H
#define MYSORTFILTERPROXYMODEL_H

#include <QDate>
#include <QRegularExpression>
#include <QSet>
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

    // Sets the regex and the normalized text together with a single re-filter
    // (setFilterRegularExpression + setNormalizedFilter would filter twice).
    void setFilters(const QRegularExpression &regex, const QString &normalizedText, int column = -1);

    // Number of source columns the text filter looks at.
    int searchColumnCount() const;

    // Parses a stored number, accepting a decimal comma ("12,5").
    static double numericValue(const QString &text, bool *ok);

    // Strips Spanish/common diacritics from a string for locale-insensitive comparison.
    static QString removeDiacritics(const QString &text);

    QString table_name;

    // A gastos row whose fecha is in a quarter closed by Contabilidad is read-only.
    // closedQuarters holds quarterKey()s (sql_lite closedQuarterKeys), set by the view.
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool rowLocked(int proxyRow) const;
    QSet<int> closedQuarters;

    // Checked before an edit is written: a non-empty message refuses it (editRefused).
    std::function<QString(const QModelIndex &index, const QVariant &value)> editValidator;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

signals:
    void editRefused(const QString &message);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_normalizedFilterText;
    int m_filterColumn = -1;
    QRegularExpression m_ownRegex;   // set by setFilters(); wins over filterRegularExpression()
};

#endif // MYSORTFILTERPROXYMODEL_H
