// Qt Test suite for the post-update self-check (`laideal.exe --self-check`,
// src/app/selfcheck.h, SelfCheckDialog): the pure pieces - newest backup, the test
// settings copy, the schema / data checks, the report - and one whole run of the window
// against a throwaway "shop" folder (live DB, its backups, the live settings), checking
// that the live files are left exactly as they were, the copies are deleted and the
// report carries no shop data.

#include <QtTest>
#include "aeatstore.h"
#include <QCryptographicHash>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "appsettings.h"
#include "commandline.h"
#include "e2efixture.h"
#include "selfcheck.h"
#include "selfcheckdialog.h"
#include "sql_lite.h"
#include "testschema.h"

class TestSelfCheck : public QObject
{
    Q_OBJECT

    static QByteArray digest(const QString &file)
    {
        QFile f(file);
        return f.open(QIODevice::ReadOnly) ? QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256)
                                           : QByteArray();
    }

    // A shop database: an unpaid ticket, a paid one AEAT holds (Q1 2026), a gasto and Q1 closed.
    static bool seedShop(const QString &file)
    {
        bool ok = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "seed");
            db.setDatabaseName(file);
            ok = TestSchema::create(db)
                && E2e::seedGarment(db, "100", "h100", "12.00", "10-03-2026")
                && E2e::seedSentGarment(db, "99", "h99", "24.20", "15-02-2026", "A-SELFCHECK")
                && E2e::exec(db, "UPDATE ingresos SET verifactu_url_qr = 'https://prewww2.aeat.es/x?numserie=99' "
                                 "WHERE n_recibo = '99'")
                && E2e::exec(db, "INSERT INTO gastos (id, n_factura, servicio, descripcion, empresa, fecha, iva, importe) "
                                 "VALUES (1, 'G1', 'Luz', '', 'Proveedor', '20-02-2026', '21', '60.50')")
                && closeQuarter(db, 1, 2026, "contabilidad_trimestral_2026_1.pdf");
            db.close();
        }
        QSqlDatabase::removeDatabase("seed");
        return ok;
    }

private slots:
    // The check is asked for once, on the first start of a newer version, never on a first install.
    void test_isFirstStartAfterUpdate()
    {
        QVERIFY(SelfCheck::isFirstStartAfterUpdate("11.2", "12.0"));
        QVERIFY(!SelfCheck::isFirstStartAfterUpdate("12.0", "12.0"));
        QVERIFY(!SelfCheck::isFirstStartAfterUpdate(QString(), "12.0"));
    }

    void test_newestBackup()
    {
        QTemporaryDir dir;
        QCOMPARE(SelfCheck::newestBackup(dir.path()), QString());
        for (const char *name : { "laideal_2026-01-05_090000.db", "laideal_2026-10-18_083000.db", "other.db" }) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        QCOMPARE(QFileInfo(SelfCheck::newestBackup(dir.path())).fileName(), QStringLiteral("laideal_2026-10-18_083000.db"));
    }

    // The copy runs on the database copy, the direct client in pre-production and no
    // gateway key; everything else (printer, reports folder) stays as in the shop.
    void test_testSettings()
    {
        const QByteArray live = R"({"database":{"path":"C:/shop/laideal.db"},"print":{"enable":"Yes","printer":"TM"},
            "verifactu":{"connection":"irenesolutions","service_key":"secret","aeat_direct_production":true,
                         "aeat_certificate_password":"pfx-secret"}})";
        const QJsonObject o = QJsonDocument::fromJson(SelfCheck::testSettings(live, "C:\\tmp\\copy.db")).object();
        QCOMPARE(o["database"].toObject()["path"].toString(), QStringLiteral("C:/tmp/copy.db"));
        QCOMPARE(o["verifactu"].toObject()["connection"].toString(), QStringLiteral("aeat"));
        QCOMPARE(o["verifactu"].toObject()["aeat_direct_production"].toBool(true), false);
        QCOMPARE(o["verifactu"].toObject()["service_key"].toString(), QString());
        QCOMPARE(o["verifactu"].toObject()["aeat_certificate_password"].toString(), QString());
        QCOMPARE(o["backup"].toObject()["enabled"].toBool(true), false);
        QCOMPARE(o["print"].toObject()["printer"].toString(), QStringLiteral("TM"));
        QVERIFY(SelfCheck::testSettings("not json", "x.db").isEmpty());
    }

    void test_checkSchema_passesAndFailsOnAFailedMigration()
    {
        QTemporaryDir dir;
        QVERIFY(seedShop(dir.filePath("shop.db")));
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "schema");
        db.setDatabaseName(dir.filePath("shop.db"));
        const SelfCheck::Check ok = SelfCheck::checkSchema(db);
        QCOMPARE(ok.status, SelfCheck::Check::Pass);
        QCOMPARE(ok.id, QStringLiteral("M1"));

        // Without gastos the accounting-closures migration cannot carry the old flags over.
        QSqlDatabase broken = QSqlDatabase::addDatabase("QSQLITE", "broken");
        broken.setDatabaseName(dir.filePath("broken.db"));
        QVERIFY(broken.open());
        QSqlQuery(broken).exec("CREATE TABLE ingresos (n_recibo TEXT, fecha_pago TEXT, importe TEXT, pagado TEXT, "
                               "estado TEXT, edit_lock TEXT, hash TEXT)");
        broken.close();
        const SelfCheck::Check bad = SelfCheck::checkSchema(broken);
        QCOMPARE(bad.status, SelfCheck::Check::Fail);
        QVERIFY2(bad.reason.contains("migración"), qPrintable(bad.reason));
        db = QSqlDatabase();
        broken = QSqlDatabase();
        QSqlDatabase::removeDatabase("schema");
        QSqlDatabase::removeDatabase("broken");
    }

    // M1d: on a database the app already migrated, a second run changes nothing; on an
    // unmigrated legacy one the "second" run still adds columns, which the check reports.
    void test_checkMigrationsTwice_changesNothingOnAMigratedDatabase()
    {
        QTemporaryDir dir;
        QVERIFY(seedShop(dir.filePath("shop.db")));
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "twice");
        db.setDatabaseName(dir.filePath("shop.db"));
        migrateDatabase(db);   // as M1 does first: the seed dropped the guard triggers to shape its rows
        const SelfCheck::Check ok = SelfCheck::checkMigrationsTwice(db);
        QCOMPARE(ok.id, QStringLiteral("M1d"));
        QVERIFY2(ok.status == SelfCheck::Check::Pass, qPrintable(ok.reason));

        db.setDatabaseName(dir.filePath("legacy.db"));
        QVERIFY(db.open());
        QVERIFY(QSqlQuery(db).exec("CREATE TABLE ingresos (n_recibo TEXT, cliente TEXT, fecha_recepcion TEXT, "
                                   "fecha_pago TEXT, fecha_recogida TEXT, importe TEXT, pagado TEXT, estado TEXT, "
                                   "cantidad TEXT, prenda TEXT, size TEXT, servicio TEXT, observaciones TEXT, "
                                   "edit_lock TEXT NOT NULL, hash TEXT)"));
        QVERIFY(QSqlQuery(db).exec("CREATE TABLE gastos (id INTEGER PRIMARY KEY, n_factura TEXT, servicio TEXT, "
                                   "descripcion TEXT, empresa TEXT, fecha TEXT, iva TEXT, importe TEXT, edit_lock TEXT)"));
        QVERIFY(QSqlQuery(db).exec("CREATE TABLE prendas (nombre TEXT, precio_limpieza TEXT, precio_plancha TEXT)"));
        db.close();
        const SelfCheck::Check changed = SelfCheck::checkMigrationsTwice(db);
        QCOMPARE(changed.status, SelfCheck::Check::Fail);
        QVERIFY2(changed.reason.contains("modifica"), qPrintable(changed.reason));
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase("twice");
    }

    void test_checkSettings_readableFileAndExistingDatabase()
    {
        QTemporaryDir dir;
        const QString db = dir.filePath("shop.db");
        QFile dbFile(db);
        QVERIFY(dbFile.open(QIODevice::WriteOnly));
        dbFile.close();
        const QString good = dir.filePath("settings.json");
        QFile f(good);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"database\":{}}");
        f.close();
        QCOMPARE(SelfCheck::checkSettings(good, db).status, SelfCheck::Check::Pass);
        QCOMPARE(SelfCheck::checkSettings(dir.filePath("missing.json"), db).status, SelfCheck::Check::Fail);
        QCOMPARE(SelfCheck::checkSettings(good, dir.filePath("gone.db")).status, SelfCheck::Check::Fail);
        QCOMPARE(SelfCheck::checkSettings(good, QString()).status, SelfCheck::Check::Fail);
        QFile bad(dir.filePath("bad.json"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not json");
        bad.close();
        QCOMPARE(SelfCheck::checkSettings(bad.fileName(), db).status, SelfCheck::Check::Fail);
    }

    // Under 3 days passes (boundary included); older, or a name without a time, fails.
    void test_checkBackupAge_threeDayBoundary()
    {
        const QDateTime now(QDate(2026, 10, 20), QTime(12, 0));
        const auto age = [&now](const QString &name) { return SelfCheck::checkBackupAge("C:/b/" + name, now); };
        QCOMPARE(age("laideal_2026-10-20_080000.db").status, SelfCheck::Check::Pass);
        QCOMPARE(age("laideal_2026-10-17_120000.db").status, SelfCheck::Check::Pass);   // exactly 3 days
        const SelfCheck::Check old = age("laideal_2026-10-17_115959.db");
        QCOMPARE(old.status, SelfCheck::Check::Fail);
        QVERIFY2(old.reason.contains("3 días"), qPrintable(old.reason));
        QCOMPARE(age("laideal_2026-09-01_090000.db").status, SelfCheck::Check::Fail);
        QCOMPARE(age("other.db").status, SelfCheck::Check::Fail);
        QCOMPARE(SelfCheck::checkBackupAge(QString(), now).status, SelfCheck::Check::Fail);
    }

    // M1e counts unreadable ingresos importes and gastos IVA values; the reason names the tickets
    // (not the suppliers), and a trailing-dot amount such as "6." is readable.
    void test_checkMoney_countsUnreadableAmountsAndIva()
    {
        QTemporaryDir dir;
        QVERIFY(seedShop(dir.filePath("shop.db")));
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "money");
        db.setDatabaseName(dir.filePath("shop.db"));
        QVERIFY(SelfCheck::checkMoney(db).status == SelfCheck::Check::Pass);

        QVERIFY(E2e::exec(db, "INSERT INTO ingresos (n_recibo, importe, edit_lock) VALUES ('7', '1.234,50', '0')"));
        QVERIFY(E2e::exec(db, "INSERT INTO ingresos (n_recibo, importe, edit_lock) VALUES ('8', 'abc', '0')"));
        QVERIFY(E2e::exec(db, "INSERT INTO ingresos (n_recibo, importe, edit_lock) VALUES ('9', '12,5', '0')"));   // readable
        QVERIFY(E2e::exec(db, "INSERT INTO ingresos (n_recibo, importe, edit_lock) VALUES ('10', '6.', '0')"));    // readable
        QVERIFY(E2e::exec(db, "INSERT INTO gastos (id, n_factura, iva, importe) VALUES (2, 'G2', '7,5', '10.00')"));
        QVERIFY(E2e::exec(db, "INSERT INTO gastos (id, n_factura, iva, importe) VALUES (3, 'G3', '', '10.00')"));
        QVERIFY(E2e::exec(db, "INSERT INTO gastos (id, n_factura, iva, importe) VALUES (4, 'G4', '10', '10.00')"));  // fine
        const SelfCheck::Check bad = SelfCheck::checkMoney(db);
        QCOMPARE(bad.status, SelfCheck::Check::Fail);
        QVERIFY2(bad.reason.startsWith("2 importe(s)") && bad.reason.contains("2 gasto(s)")
                 && bad.reason.endsWith("(tickets: 7, 8)"), qPrintable(bad.reason));
        QVERIFY(!bad.reason.contains("Proveedor") && !bad.reason.contains("G2"));
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase("money");
    }

    // M1c recomputes the stored AEAT chain: none or a valid one passes, an altered record fails
    // without naming the invoice.
    void test_checkChain()
    {
        QTemporaryDir dir;
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "chain");
        db.setDatabaseName(dir.filePath("chain.db"));
        QCOMPARE(SelfCheck::checkChain(db).status, SelfCheck::Check::Pass);
        QVERIFY(AeatStore(db, "pruebas").ensureSchema());
        QVERIFY(db.open());
        QVERIFY(QSqlQuery(db).exec("INSERT INTO aeat_records (environment, kind, issuer_nif, invoice_number, "
                                   "issue_date, hash, xml, generated_at, state) VALUES ('pruebas', 'alta', "
                                   "'89890001K', '77', '10-10-2026', 'AB', '<RegistroAlta><Huella>AB</Huella>"
                                   "</RegistroAlta>', '', 'ACEPTADO')"));
        db.close();
        const SelfCheck::Check bad = SelfCheck::checkChain(db);
        QCOMPARE(bad.status, SelfCheck::Check::Fail);
        QVERIFY2(bad.reason.contains("registro 1 de pruebas") && !bad.reason.contains("77"), qPrintable(bad.reason));
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase("chain");
    }

    // The newest ticket, the invoice to print and the newest closed quarter's figures.
    void test_checkData_factsOfTheShop()
    {
        QTemporaryDir dir;
        QVERIFY(seedShop(dir.filePath("shop.db")));
        QVERIFY(QDir().mkpath(dir.filePath("Contabilidad")));
        QFile pdf(dir.filePath("Contabilidad/contabilidad_trimestral_2026_1.pdf"));
        QVERIFY(pdf.open(QIODevice::WriteOnly));
        pdf.close();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "data");
            db.setDatabaseName(dir.filePath("shop.db"));
            SelfCheck::DataFacts facts;
            const SelfCheck::Check c = SelfCheck::checkData(db, dir.filePath("Contabilidad"), facts);
            QCOMPARE(c.status, SelfCheck::Check::Pass);
            QCOMPARE(facts.lastTicket, QStringLiteral("100"));
            QCOMPARE(facts.printableTicket, QStringLiteral("99"));
            QVERIFY(facts.printableQrUrl.contains("numserie=99"));
            QCOMPARE(facts.closedQuarter, 1);
            QCOMPARE(facts.closedYear, 2026);
            QCOMPARE(facts.income, 24.20);
            QCOMPARE(facts.expenses, 60.50);
            QVERIFY(facts.reportFile.endsWith("contabilidad_trimestral_2026_1.pdf"));
            QVERIFY(!c.reason.contains("100") && !c.reason.contains("24"));   // no shop data reported
        }
        QSqlDatabase::removeDatabase("data");
    }

    // The backup can be a day old: the factura is reprinted only while the live database
    // (read-only) still holds the invoice as sent.
    void test_invoiceStillSent()
    {
        QTemporaryDir dir;
        const QString live = dir.filePath("live.db");
        QVERIFY(seedShop(live));
        const QByteArray before = digest(live);
        QVERIFY(SelfCheck::invoiceStillSent(live, "99", 0));
        QVERIFY(!SelfCheck::invoiceStillSent(live, "99", 1));
        QVERIFY(!SelfCheck::invoiceStillSent(dir.filePath("missing.db"), "99", 0));
        QVERIFY(!QFileInfo::exists(dir.filePath("missing.db")));          // read-only: not created
        QCOMPARE(digest(live), before);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "cancel");
            db.setDatabaseName(live);
            QVERIFY(E2e::exec(db, "UPDATE ingresos SET verifactu_estado = 'ANULADA' WHERE n_recibo = '99'"));
            db.close();
        }
        QSqlDatabase::removeDatabase("cancel");
        QVERIFY(!SelfCheck::invoiceStillSent(live, "99", 0));
    }

    // A rectificativa (own number, estado Rectificativa, paid) is never the one printed
    // through the invoice template: with the original invoice gone, nothing is offered.
    void test_checkData_printableTicketIgnoresRectificativas()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("shop.db");
        QVERIFY(seedShop(file));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "rect");
            db.setDatabaseName(file);
            QVERIFY(E2e::seedSentGarment(db, "R-1", "hR1", "5.00", "16-02-2026", "A-RECT"));
            QVERIFY(E2e::exec(db, "UPDATE ingresos SET estado = 'Rectificativa', verifactu_rectifies_n_recibo = '99', "
                                  "verifactu_url_qr = 'https://prewww2.aeat.es/x?numserie=R-1' WHERE n_recibo = 'R-1'"));
            QVERIFY(E2e::exec(db, "UPDATE ingresos SET verifactu_estado = 'ANULADA' WHERE n_recibo = '99'"));
            db.close();
        }
        QSqlDatabase::removeDatabase("rect");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "data");
            db.setDatabaseName(file);
            SelfCheck::DataFacts facts;
            SelfCheck::checkData(db, dir.filePath("Contabilidad"), facts);
            QVERIFY2(facts.printableTicket.isEmpty(), qPrintable(facts.printableTicket));
        }
        QSqlDatabase::removeDatabase("data");
    }

    // `--settings` with no file after it is an error, never the shop's own settings.
    void test_commandLine_danglingSettingsSwitch()
    {
        bool dangling = false;
        QCOMPARE(CommandLine::switchValue({ "laideal.exe", "--settings", "a.json" }, "--settings", &dangling), QStringLiteral("a.json"));
        QVERIFY(!dangling);
        QVERIFY(CommandLine::switchValue({ "laideal.exe", "--self-check", "--settings" }, "--settings", &dangling).isEmpty());
        QVERIFY(dangling);
        QVERIFY(CommandLine::switchValue({ "laideal.exe" }, "--settings", &dangling).isEmpty());
        QVERIFY(!dangling);
    }

    void test_reportText()
    {
        SelfCheck::Check a{ "M1", "Base", SelfCheck::Check::Pass, "ok" };
        SelfCheck::Check b{ "M3", "Impresora", SelfCheck::Check::Fail, "no", "sin papel" };
        const QString text = SelfCheck::reportText({ a, b }, "12.0", QDateTime(QDate(2026, 10, 19), QTime(9, 30)));
        QVERIFY(text.contains("La Ideal 12.0"));
        QVERIFY(text.contains("19-10-2026 09:30"));
        QVERIFY(text.contains("M1 Base: CORRECTO"));
        QVERIFY(text.contains("M3 Impresora: FALLO"));
        QVERIFY(text.contains("Nota: sin papel"));
        QVERIFY(text.contains("1 comprobación(es) con fallo"));
    }

    // A whole run as the shop user does it, printing off: the copies are made from the
    // newest backup, the questions are answered, the report is written, the copies are
    // deleted, and the live database and settings are byte for byte unchanged.
    void test_dialog_wholeRunLeavesTheShopUntouched()
    {
        QTemporaryDir shop;
        const QString liveDb = shop.filePath("laideal.db");
        QVERIFY(seedShop(liveDb));
        QVERIFY(QDir().mkpath(shop.filePath("backups")));
        QVERIFY(QFile::copy(liveDb, shop.filePath("backups/laideal_" + QDateTime::currentDateTime().addDays(-1).toString("yyyy-MM-dd_HHmmss") + ".db")));
        QFile older(shop.filePath("backups/laideal_2026-01-01_090000.db"));   // not a database: never picked
        QVERIFY(older.open(QIODevice::WriteOnly));
        older.close();
        const QString liveSettings = shop.filePath("settings.json");
        QJsonObject root;
        root["database"] = QJsonObject{ { "path", liveDb } };
        root["print"] = QJsonObject{ { "enable", "No" } };
        root["reports"] = QJsonObject{ { "root", shop.filePath("reports") } };
        root["verifactu"] = QJsonObject{ { "connection", "irenesolutions" }, { "service_key", "" } };
        QFile settingsFile(liveSettings);
        QVERIFY(settingsFile.open(QIODevice::WriteOnly));
        settingsFile.write(QJsonDocument(root).toJson());
        settingsFile.close();
        QVERIFY(AppSettings::instance()->loadFrom(liveSettings));
        const QByteArray dbBefore = digest(liveDb), settingsBefore = digest(liveSettings);

        QTemporaryDir out;
        QString workDir;
        const QString stale = QDir::tempPath() + "/laideal_comprobacion_stale";   // a crashed run's copies
        QVERIFY(QDir().mkpath(stale));
        QFile staleCopy(stale + "/laideal_comprobacion.db");
        QVERIFY(staleCopy.open(QIODevice::WriteOnly));
        staleCopy.close();
        {
            SelfCheckDialog dialog(liveSettings);
            dialog.setReportFolder(out.path());
            dialog.start();
            auto *question = dialog.findChild<QGroupBox *>();
            for (int answered = 0; question && question->isVisibleTo(&dialog) && answered < 10; ++answered)
                dialog.findChild<QPushButton *>("btnYes")->click();
            QCOMPARE(dialog.checks().size(), 10);   // M1a, M1b, M1, M1d, M1e, M1c, M2, M2b, M3, M5
            for (const SelfCheck::Check &c : dialog.checks()) {
                if (c.id == QLatin1String("M3"))
                    QCOMPARE(c.status, SelfCheck::Check::Skip);   // printing off
                else
                    QVERIFY2(c.status == SelfCheck::Check::Pass, qPrintable(c.id + ": " + c.reason));
            }
            QFile report(dialog.reportFile());
            QVERIFY(report.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(report.readAll());
            QVERIFY2(text.contains("Resultado: todo correcto."), qPrintable(text));
            QVERIFY(dialog.allPassed());
            QVERIFY(!text.contains("Cliente E2E") && !text.contains("24,20") && !text.contains("A-SELFCHECK"));
            workDir = QFileInfo(AppSettings::instance()->dbPath()).absolutePath();
        }
        QVERIFY2(!QFileInfo::exists(workDir), qPrintable(workDir));   // the copies are gone
        QVERIFY(!QFileInfo::exists(stale));
        QCOMPARE(digest(liveDb), dbBefore);
        QCOMPARE(digest(liveSettings), settingsBefore);
    }

    // Closing the window before the questions are answered still leaves a report, with the
    // unanswered checks "SIN HACER" and a failing result (the exit code of --self-check).
    void test_dialog_closeBeforeAnsweringWritesPartialReport()
    {
        QTemporaryDir shop;
        const QString liveDb = shop.filePath("laideal.db");
        QVERIFY(seedShop(liveDb));
        QVERIFY(QDir().mkpath(shop.filePath("backups")));
        QVERIFY(QFile::copy(liveDb, shop.filePath("backups/laideal_" + QDateTime::currentDateTime().addDays(-1).toString("yyyy-MM-dd_HHmmss") + ".db")));
        const QString liveSettings = shop.filePath("settings.json");
        QJsonObject root;
        root["database"] = QJsonObject{ { "path", liveDb } };
        root["print"] = QJsonObject{ { "enable", "No" } };
        root["reports"] = QJsonObject{ { "root", shop.filePath("reports") } };
        QFile settingsFile(liveSettings);
        QVERIFY(settingsFile.open(QIODevice::WriteOnly));
        settingsFile.write(QJsonDocument(root).toJson());
        settingsFile.close();
        QVERIFY(AppSettings::instance()->loadFrom(liveSettings));

        QTemporaryDir out;
        SelfCheckDialog dialog(liveSettings);
        dialog.setReportFolder(out.path());
        dialog.start();
        QVERIFY(dialog.reportFile().isEmpty());                          // questions still open
        dialog.reject();
        QVERIFY(!dialog.allPassed());
        QFile report(dialog.reportFile());
        QVERIFY(report.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(report.readAll());
        QVERIFY2(text.contains("SIN HACER"), qPrintable(text));
        QVERIFY2(text.contains("con fallo o sin hacer"), qPrintable(text));
    }
};

QTEST_MAIN(TestSelfCheck)
#include "test_selfcheck.moc"
