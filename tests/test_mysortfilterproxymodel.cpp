// Tests for MySortFilterProxyModel's diacritic-insensitive text filter. The
// filter text is expected pre-normalised + lower-cased by the caller (that is
// how RecogPrendas / Listado feed it), so the tests mirror that: they build the
// needle with removeDiacritics(...).toLower() and assert the proxy matches the
// accented source rows.

#include <QtTest>
#include <QStandardItemModel>

#include "mysortfilterproxymodel.h"

class TestMySortFilterProxyModel : public QObject
{
    Q_OBJECT

private:
    static QString needle(const QString &raw)
    {
        return MySortFilterProxyModel::removeDiacritics(raw).toLower();
    }

private slots:
    void test_removeDiacritics()
    {
        QCOMPARE(MySortFilterProxyModel::removeDiacritics(QStringLiteral("José")),
                 QStringLiteral("Jose"));
        QCOMPARE(MySortFilterProxyModel::removeDiacritics(QStringLiteral("Begoña")),
                 QStringLiteral("Begona"));
        QCOMPARE(MySortFilterProxyModel::removeDiacritics(QStringLiteral("áéíóú ü ñ")),
                 QStringLiteral("aeiou u n"));
        // No diacritics -> unchanged.
        QCOMPARE(MySortFilterProxyModel::removeDiacritics(QStringLiteral("Ana")),
                 QStringLiteral("Ana"));
    }

    void test_filterMatchesAccentInsensitively()
    {
        QStandardItemModel model(0, 1);
        for (const QString &name : { QStringLiteral("José"),
                                     QStringLiteral("Ana"),
                                     QStringLiteral("Begoña") })
            model.appendRow(new QStandardItem(name));

        MySortFilterProxyModel proxy;
        proxy.setSourceModel(&model);

        // ASCII needle matches the accented row ("jose" -> "José").
        proxy.setNormalizedFilter(needle(QStringLiteral("jose")));
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.data(proxy.index(0, 0)).toString(), QStringLiteral("José"));

        // Accent-free needle matches an accented "ñ" name.
        proxy.setNormalizedFilter(needle(QStringLiteral("begona")));
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.data(proxy.index(0, 0)).toString(), QStringLiteral("Begoña"));

        // Accented needle also matches (caller normalises it the same way).
        proxy.setNormalizedFilter(needle(QStringLiteral("begoña")));
        QCOMPARE(proxy.rowCount(), 1);

        // Empty filter shows everything.
        proxy.setNormalizedFilter(QString());
        QCOMPARE(proxy.rowCount(), 3);

        // No match.
        proxy.setNormalizedFilter(needle(QStringLiteral("zzz")));
        QCOMPARE(proxy.rowCount(), 0);
    }

    // lessThan() keys on table_name + column. ingresos column indices come from
    // sql_lite.h (INGRESOS_COL_*); used here as literals to avoid the dependency:
    // 1 = cliente, 3 = fecha_pago, 5 = importe.
    void test_lessThan_ingresosDateChronological()
    {
        QStandardItemModel model;
        // Lexicographic order of these would be 02-, 15-, 31-; chronological is
        // 15-12-2025 < 31-01-2026 < 02-02-2026.
        const QStringList dates = { "31-01-2026", "02-02-2026", "15-12-2025" };
        for (int r = 0; r < dates.size(); ++r)
            model.setItem(r, 3, new QStandardItem(dates[r]));

        MySortFilterProxyModel proxy;
        proxy.table_name = "ingresos";
        proxy.setSourceModel(&model);
        proxy.sort(3, Qt::AscendingOrder);

        QCOMPARE(proxy.data(proxy.index(0, 3)).toString(), QStringLiteral("15-12-2025"));
        QCOMPARE(proxy.data(proxy.index(1, 3)).toString(), QStringLiteral("31-01-2026"));
        QCOMPARE(proxy.data(proxy.index(2, 3)).toString(), QStringLiteral("02-02-2026"));
    }

    void test_lessThan_ingresosImporteNumeric()
    {
        QStandardItemModel model;
        // Lexicographically "100" < "50" < "9"; numerically 9 < 50 < 100.
        const QStringList importes = { "100", "9", "50" };
        for (int r = 0; r < importes.size(); ++r)
            model.setItem(r, 5, new QStandardItem(importes[r]));

        MySortFilterProxyModel proxy;
        proxy.table_name = "ingresos";
        proxy.setSourceModel(&model);
        proxy.sort(5, Qt::AscendingOrder);

        QCOMPARE(proxy.data(proxy.index(0, 5)).toString(), QStringLiteral("9"));
        QCOMPARE(proxy.data(proxy.index(1, 5)).toString(), QStringLiteral("50"));
        QCOMPARE(proxy.data(proxy.index(2, 5)).toString(), QStringLiteral("100"));
    }

    void test_lessThan_defaultStringColumn()
    {
        QStandardItemModel model;
        const QStringList names = { "banana", "apple", "cherry" };
        for (int r = 0; r < names.size(); ++r)
            model.setItem(r, 1, new QStandardItem(names[r])); // cliente -> locale string compare

        MySortFilterProxyModel proxy;
        proxy.table_name = "ingresos";
        proxy.setSourceModel(&model);
        proxy.sort(1, Qt::AscendingOrder);

        QCOMPARE(proxy.data(proxy.index(0, 1)).toString(), QStringLiteral("apple"));
        QCOMPARE(proxy.data(proxy.index(1, 1)).toString(), QStringLiteral("banana"));
        QCOMPARE(proxy.data(proxy.index(2, 1)).toString(), QStringLiteral("cherry"));
    }
};

QTEST_GUILESS_MAIN(TestMySortFilterProxyModel)
#include "test_mysortfilterproxymodel.moc"
