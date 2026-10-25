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

    // n_recibo is TEXT: "99999" must rank below "100000" (and above "999").
    void test_lessThan_nReciboNumericAcrossRollover()
    {
        QStandardItemModel model;
        const QStringList numbers = { "100000", "999", "99999", "1000" };
        for (int r = 0; r < numbers.size(); ++r)
            model.setItem(r, 0, new QStandardItem(numbers[r]));

        MySortFilterProxyModel proxy;
        proxy.table_name = "ingresos";
        proxy.setSourceModel(&model);
        proxy.sort(0, Qt::DescendingOrder);
        QStringList seen;
        for (int r = 0; r < proxy.rowCount(); ++r)
            seen << proxy.data(proxy.index(r, 0)).toString();
        QCOMPARE(seen, QStringList({ "100000", "99999", "1000", "999" }));
    }

    // Cantidad, tamano (decimal comma) and the rectified ticket number sort as numbers too.
    void test_lessThan_ingresosOtherNumericColumns()
    {
        QStandardItemModel model;
        const QStringList sizes = { "10", "9,5", "2" };
        for (int r = 0; r < sizes.size(); ++r) {
            model.setItem(r, 10, new QStandardItem(sizes[r]));   // size
            model.setItem(r, 8, new QStandardItem(sizes[r]));    // cantidad
            model.setItem(r, 22, new QStandardItem(sizes[r]));   // rectifies n_recibo
        }
        MySortFilterProxyModel proxy;
        proxy.table_name = "ingresos";
        proxy.setSourceModel(&model);
        for (int col : { 8, 10, 22 }) {
            proxy.sort(col, Qt::AscendingOrder);
            QCOMPARE(proxy.data(proxy.index(0, col)).toString(), QStringLiteral("2"));
            QCOMPARE(proxy.data(proxy.index(1, col)).toString(), QStringLiteral("9,5"));
            QCOMPARE(proxy.data(proxy.index(2, col)).toString(), QStringLiteral("10"));
        }
    }

    // fecha_anulacion (column 26) is a dd-MM-yyyy date, not text.
    void test_lessThan_fechaAnulacionChronological()
    {
        QStandardItemModel model;
        const QStringList dates = { "02-02-2026", "15-12-2025", "31-01-2026" };
        for (int r = 0; r < dates.size(); ++r)
            model.setItem(r, 26, new QStandardItem(dates[r]));
        MySortFilterProxyModel proxy;
        proxy.table_name = "ingresos";
        proxy.setSourceModel(&model);
        proxy.sort(26, Qt::AscendingOrder);
        QCOMPARE(proxy.data(proxy.index(0, 26)).toString(), QStringLiteral("15-12-2025"));
        QCOMPARE(proxy.data(proxy.index(1, 26)).toString(), QStringLiteral("31-01-2026"));
        QCOMPARE(proxy.data(proxy.index(2, 26)).toString(), QStringLiteral("02-02-2026"));
    }

    // The filter reaches Tam. / Serv. / Obs. (columns 10-12) but not the Verifactu metadata.
    void test_filter_ingresosSearchColumns()
    {
        QStandardItemModel model(3, 20);
        model.setItem(0, 10, new QStandardItem("talla-unica"));
        model.setItem(1, 11, new QStandardItem("tintado"));
        model.setItem(2, 12, new QStandardItem("manchas"));
        model.setItem(0, 15, new QStandardItem("secreto-csv"));
        MySortFilterProxyModel proxy;
        proxy.table_name = "ingresos";
        proxy.setSourceModel(&model);
        QCOMPARE(proxy.searchColumnCount(), 13);
        proxy.setNormalizedFilter("talla-unica");
        QCOMPARE(proxy.rowCount(), 1);
        proxy.setNormalizedFilter("tintado");
        QCOMPARE(proxy.rowCount(), 1);
        proxy.setNormalizedFilter("manchas");
        QCOMPARE(proxy.rowCount(), 1);
        proxy.setNormalizedFilter("secreto-csv");
        QCOMPARE(proxy.rowCount(), 0);
    }

    // setFilters() applies the regex and the normalized text together.
    void test_setFilters_singleInvalidation()
    {
        QStandardItemModel model;
        for (const QString &name : { "Ana", "Beatriz", "Carlos" })
            model.appendRow(new QStandardItem(name));
        MySortFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.setFilters(QRegularExpression("^C"), "car");
        QCOMPARE(proxy.rowCount(), 1);
        proxy.setFilters(QRegularExpression(), "ana");
        QCOMPARE(proxy.rowCount(), 1);
        proxy.setFilters(QRegularExpression("^B"), "ana");   // the regex alone decides
        QCOMPARE(proxy.rowCount(), 1);
        proxy.setFilters(QRegularExpression(), QString());
        QCOMPARE(proxy.rowCount(), 3);
    }

    // A case-sensitive regex narrows: the case-insensitive text must not add matches.
    void test_regexCaseSensitiveNarrows()
    {
        QStandardItemModel model;
        for (const QString &name : { "Ana", "ana", "ANA" })
            model.appendRow(new QStandardItem(name));
        MySortFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.setFilters(QRegularExpression("ana"), "ana");
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.index(0, 0).data().toString(), QStringLiteral("ana"));
    }

    // An invalid regex matches nothing, even when the plain text would match.
    void test_invalidRegexMatchesNothing()
    {
        QStandardItemModel model;
        for (const QString &name : { "a[b", "Ana" })
            model.appendRow(new QStandardItem(name));
        MySortFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.setFilters(QRegularExpression("a["), "a[");
        QCOMPARE(proxy.rowCount(), 0);
        proxy.setFilters(QRegularExpression(), "a[");
        QCOMPARE(proxy.rowCount(), 1);
    }

    // Invoice rows cannot be edited through the model, whatever the view allows.
    void test_ingresos_readOnlyAtModelLevel()
    {
        QStandardItemModel model(1, 3);
        model.setItem(0, 1, new QStandardItem("Ana"));
        MySortFilterProxyModel proxy;
        proxy.table_name = "ingresos";
        proxy.setSourceModel(&model);
        for (int col = 0; col < 3; ++col)
            QVERIFY(!(proxy.flags(proxy.index(0, col)) & Qt::ItemIsEditable));
        QVERIFY(!proxy.setData(proxy.index(0, 1), "Pepe"));
        QCOMPARE(model.item(0, 1)->text(), QStringLiteral("Ana"));

        // Other tables stay editable.
        MySortFilterProxyModel other;
        other.table_name = "prendas";
        other.setSourceModel(&model);
        QVERIFY(other.flags(other.index(0, 1)) & Qt::ItemIsEditable);
        QVERIFY(other.setData(other.index(0, 1), "Pepe"));
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
