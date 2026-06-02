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
};

QTEST_GUILESS_MAIN(TestMySortFilterProxyModel)
#include "test_mysortfilterproxymodel.moc"
