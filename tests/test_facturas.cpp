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
#include "sql_lite.h"
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

private:
    // The dialog over a throwaway DB with one supplier and one service.
    struct Fixture
    {
        QTemporaryDir dir;
        QSqlDatabase db;
        QString conn;

        explicit Fixture(const QString &name) : conn(name)
        {
            db = QSqlDatabase::addDatabase("QSQLITE", conn);
            db.setDatabaseName(dir.filePath("f.db"));
            TestSchema::create(db);
            db.open();
            QSqlQuery q(db);
            q.exec("INSERT INTO proveedores (nombre) VALUES ('Acme')");
            q.exec("INSERT INTO proveedores (nombre) VALUES (\"O'Brien\")");
            q.exec("INSERT INTO servicios (nombre) VALUES ('Luz')");
            q.finish();
            db.close();
        }
        ~Fixture() { db = QSqlDatabase(); QSqlDatabase::removeDatabase(conn); }
    };

    static void fillForm(Facturas &form, const QString &fra, const QString &amount, const QDate &date = QDate::currentDate(),
                         const QString &empresa = "Acme", const QString &description = QString())
    {
        form.findChild<QLineEdit *>("leFra")->setText(fra);
        form.findChild<QDateEdit *>("deFecha")->setDate(date);
        form.findChild<QComboBox *>("cbEmpresa")->setCurrentText(empresa);
        form.findChild<QComboBox *>("cbServicio")->setCurrentText("Luz");
        form.findChild<QLineEdit *>("leDescripcion")->setText(description);
        form.findChild<QLineEdit *>("leImporte")->setText(amount);
    }

    static QString saveAndRead(Facturas &form)
    {
        form.findChild<QPushButton *>("btnSave")->click();
        return form.findChild<QLabel *>("lblResult")->text();
    }

private slots:
    // The combos are filled by the constructor: a window built without the populate calls works.
    void test_constructor_fillsTheCombos()
    {
        Fixture f("facturas_ctor");
        Facturas form(f.db);
        QCOMPARE(form.findChild<QComboBox *>("cbEmpresa")->count(), 2);
        QCOMPARE(form.findChild<QComboBox *>("cbServicio")->count(), 1);
        form.populateEmpresas();                                  // a second call does not duplicate
        QCOMPARE(form.findChild<QComboBox *>("cbEmpresa")->count(), 2);
    }

    void test_save_refusals_writeNothing()
    {
        Fixture f("facturas_refusals");
        Facturas form(f.db);
        const auto count = [&f]() { return scalar(f.db, "SELECT COUNT(*) FROM gastos"); };

        fillForm(form, "F-1", "10", QDate::currentDate(), "acme");           // case differs
        QVERIFY(saveAndRead(form).contains("no está en la lista"));
        fillForm(form, "F-1", "10", QDate::currentDate().addYears(-2));
        QVERIFY2(saveAndRead(form).contains("fecha de la factura no es válida"), "two years back");
        fillForm(form, "F-1", "10", QDate::currentDate().addYears(40));
        QVERIFY2(saveAndRead(form).contains("fecha de la factura no es válida"), "a mistyped year");
        QCOMPARE(count(), QStringLiteral("0"));

        const QDate today = QDate::currentDate();
        QVERIFY(closeQuarter(f.db, (today.month() + 2) / 3, today.year(), "q.pdf"));
        fillForm(form, "F-1", "10");
        QVERIFY(saveAndRead(form).contains("Trimestre bloqueado"));
        QCOMPARE(count(), QStringLiteral("0"));
    }

    void test_save_storesRowAndKeepsHostileTextLiteral()
    {
        Fixture f("facturas_store");
        Facturas form(f.db);
        const QString hostile = QStringLiteral("x'); DROP TABLE gastos;--");
        fillForm(form, "F-<b>1", "121,00", QDate::currentDate(), "O'Brien", hostile);
        const QString panel = saveAndRead(form);
        QVERIFY2(panel.contains("guardada"), qPrintable(panel));
        QVERIFY(panel.contains("F-&lt;b&gt;1") && !panel.contains("<b>1"));
        QCOMPARE(scalar(f.db, "SELECT id || '|' || n_factura || '|' || empresa || '|' || descripcion || '|' || fecha "
                              "|| '|' || iva || '|' || importe FROM gastos"),
                 "1|F-<b>1|O'Brien|" + hostile + "|" + QDate::currentDate().toString("dd-MM-yyyy") + "|21|121.00");

        // A gap and text-typed ids: the next id is the maximum plus one.
        QVERIFY(f.db.open());
        QVERIFY(QSqlQuery(f.db).exec("UPDATE gastos SET id = '5'"));
        f.db.close();
        fillForm(form, "F-2", "5");
        QVERIFY(saveAndRead(form).contains("guardada"));
        QCOMPARE(scalar(f.db, "SELECT MAX(id) FROM gastos"), QStringLiteral("6"));
    }

    void test_save_insertFailureIsReportedAndKeepsTheForm()
    {
        Fixture f("facturas_failure");
        Facturas form(f.db);
        fillForm(form, "F-1", "10");
        QVERIFY(f.db.open());
        QVERIFY(QSqlQuery(f.db).exec("ALTER TABLE gastos RENAME TO gastos_off"));
        f.db.close();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("insertExpenseInvoice: insert failed"));
        QVERIFY(saveAndRead(form).contains("No se pudo guardar"));
        QCOMPARE(form.findChild<QLineEdit *>("leFra")->text(), QStringLiteral("F-1"));
    }

    void test_resetAndTaxSplit()
    {
        Fixture f("facturas_split");
        Facturas form(f.db);
        auto *importe = form.findChild<QLineEdit *>("leImporte");
        auto *iva = form.findChild<QComboBox *>("cbIva");
        importe->setText("121,00");
        emit importe->textEdited("121,00");
        QCOMPARE(form.findChild<QLineEdit *>("leBase")->text(), QStringLiteral("100,00"));
        QCOMPARE(form.findChild<QLineEdit *>("leIva")->text(), QStringLiteral("21,00"));
        iva->setCurrentIndex(1);                                   // 10 %
        QCOMPARE(form.findChild<QLineEdit *>("leBase")->text(), QStringLiteral("110,00"));
        iva->setCurrentIndex(2);                                   // 0 %
        QCOMPARE(form.findChild<QLineEdit *>("leIva")->text(), QStringLiteral("0,00"));
        importe->setText("abc");
        emit importe->textEdited("abc");
        QVERIFY(form.findChild<QLineEdit *>("leBase")->text().isEmpty());

        fillForm(form, "F-1", "10");
        form.findChild<QPushButton *>("btnReset")->click();
        QVERIFY(form.findChild<QLineEdit *>("leFra")->text().isEmpty());
        QVERIFY(importe->text().isEmpty());
        QCOMPARE(iva->currentIndex(), 0);
        QCOMPARE(form.findChild<QLabel *>("lblResult")->text(), QStringLiteral("Formulario limpio."));
    }

    // What the dialog stores is what Contabilidad reads back, at each rate.
    void test_savedInvoice_isPickedUpByContabilidadListing()
    {
        Fixture f("facturas_contabilidad");
        Facturas form(f.db);
        const QDate today = QDate::currentDate();
        fillForm(form, "F-1", "121,00");
        QVERIFY(saveAndRead(form).contains("guardada"));
        fillForm(form, "F-2", "55");
        form.findChild<QComboBox *>("cbIva")->setCurrentIndex(1);
        QVERIFY(saveAndRead(form).contains("guardada"));
        bool ok = false;
        const QVector<ExpenseDetail> rows = expensesBetweenDates(f.db, today, today.addDays(1), &ok);
        QVERIFY(ok);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].iva, 21);
        QCOMPARE(rows[0].importe, 121.0);
        QCOMPARE(rows[1].iva, 10);
        QCOMPARE(rows[1].importe, 55.0);
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
