// Tests for Facturas' pure IVA split (gross / IVA-incluido amount -> base + cuota),
// extracted from on_le_importe_textEdited so it can be checked across rates.

#include <QtTest>
#include <QComboBox>
#include <QLabel>
#include <QDateEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "facturas.h"
#include "support/testschema.h"

class TestFacturas : public QObject
{
    Q_OBJECT

private:
    static bool near(double a, double b) { return qAbs(a - b) < 0.005; }

    static QString scalar(QSqlDatabase &db, const QString &sql)
    {
        db.open();
        QSqlQuery q(db);
        q.exec(sql);
        const QString v = q.next() ? q.value(0).toString() : QString();
        q.finish();
        db.close();
        return v;
    }

private slots:
    void test_parseGross()
    {
        double v = 0.0;
        QVERIFY(Facturas::parseGross("121,50", &v));
        QVERIFY(near(v, 121.5));
        QVERIFY(Facturas::parseGross(" 12.5 ", &v));
        QVERIFY(near(v, 12.5));
        QVERIFY(Facturas::parseGross("0,01"));
        for (const char *bad : { "", "  ", "abc", "inf", "nan", "-inf", "1e3", "-5", "+5", "0", "0,00",
                                 "1,2,3", "12 EUR", "1.234,56", "99999999999" })
            QVERIFY2(!Facturas::parseGross(bad), bad);
    }

    // Whole dialog on a throwaway DB: bad amounts and a repeated invoice write nothing,
    // the fields are stored trimmed, ids continue from the maximum.
    void test_save_validationAndDuplicates()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString conn = "facturas_save";
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
            db.setDatabaseName(dir.filePath("f.db"));
            QVERIFY(TestSchema::create(db));
            db.open();
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO proveedores (nombre) VALUES ('Acme')"));
            QVERIFY(q.exec("INSERT INTO servicios (nombre) VALUES ('Luz')"));
            QVERIFY(q.exec("INSERT INTO gastos (id, n_factura, servicio, empresa, fecha, iva, importe) "
                           "VALUES (7, 'OLD', 'Luz', 'Acme', '05-01-2020', '21', '10.00')"));
            q.finish();
            db.close();

            Facturas form(db);
            form.populateEmpresas();
            form.populateServicios();
            auto *result = form.findChild<QLabel *>("lblResult");
            const auto fill = [&form](const QString &fra, const QString &amount) {
                form.findChild<QLineEdit *>("leFra")->setText(fra);
                form.findChild<QDateEdit *>("deFecha")->setDate(QDate::currentDate());
                form.findChild<QComboBox *>("cbEmpresa")->setCurrentText("Acme");
                form.findChild<QComboBox *>("cbServicio")->setCurrentText("  Teléfono ");
                form.findChild<QLineEdit *>("leDescripcion")->setText("  linea  ");
                form.findChild<QLineEdit *>("leImporte")->setText(amount);
            };
            const auto save = [&form]() { form.findChild<QPushButton *>("btnSave")->click(); };
            const auto count = [&db]() { return scalar(db, "SELECT COUNT(*) FROM gastos"); };

            for (const char *bad : { "0", "-50", "inf", "nan", "1e3", "abc" }) {
                fill("F-1", bad);
                save();
                QVERIFY2(result->text().contains("importe total no es válido"), bad);
                QCOMPARE(count(), QStringLiteral("1"));
            }
            fill("   ", "10");
            save();
            QVERIFY(result->text().contains("Formulario incompleto"));

            fill(" F-1 ", "12,50");
            save();
            QVERIFY2(result->text().contains("guardada"), qPrintable(result->text()));
            QCOMPARE(scalar(db, "SELECT id || '|' || n_factura || '|' || servicio || '|' || descripcion "
                                "|| '|' || importe FROM gastos WHERE n_factura = 'F-1'"),
                     QStringLiteral("8|F-1|Teléfono|linea|12.50"));

            // Same supplier + number, even with different spacing: refused, nothing added.
            fill("F-1", "99");
            save();
            QVERIFY2(result->text().contains("Factura duplicada"), qPrintable(result->text()));
            QVERIFY(result->text().contains("12,50"));
            QCOMPARE(count(), QStringLiteral("2"));
        }
        QSqlDatabase::removeDatabase(conn);
    }

    void test_ivaSplit21()
    {
        QVERIFY(near(Facturas::taxBaseFromGross(121.0, 21.0), 100.0));
        QVERIFY(near(Facturas::taxAmountFromGross(121.0, 21.0), 21.0));
    }

    void test_ivaSplit10()
    {
        QVERIFY(near(Facturas::taxBaseFromGross(110.0, 10.0), 100.0));
        QVERIFY(near(Facturas::taxAmountFromGross(110.0, 10.0), 10.0));
    }

    void test_ivaZero()
    {
        QVERIFY(near(Facturas::taxBaseFromGross(50.0, 0.0), 50.0));
        QVERIFY(near(Facturas::taxAmountFromGross(50.0, 0.0), 0.0));
    }

    void test_basePlusCuotaReconstructsGross()
    {
        const double gross = 100.0, rate = 21.0; // non-round split
        QVERIFY(near(Facturas::taxBaseFromGross(gross, rate)
                     + Facturas::taxAmountFromGross(gross, rate), gross));
    }
};

QTEST_MAIN(TestFacturas)
#include "test_facturas.moc"
