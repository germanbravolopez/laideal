// Tests for AppSettings. The dpapi* static delegators over the file-local
// encrypt/decrypt functions cover the security-relevant secret-at-rest logic
// without touching the real ~/.laideal_settings.json. The JSON-reading getters
// (derived report paths, ivaRate, and the encrypt-at-rest migration) are driven
// through the loadFrom(path) injection seam against a throwaway file in a
// QTemporaryDir, so they assert deterministically without the user's settings.

#include <QtTest>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "appsettings.h"
#include "applanguage.h"

class TestAppSettings : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    // Write `json` to a uniquely-named file in the temp dir and load the singleton
    // from it. Returns the file path so a test can re-read what was persisted.
    QString loadJson(const QByteArray &json)
    {
        static int n = 0;
        const QString path = m_dir.filePath(QString("settings_%1.json").arg(n++));
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(json);
        f.close();
        AppSettings::instance()->loadFrom(path);
        return path;
    }

    QJsonObject rawRoot(const QString &path)
    {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        return QJsonDocument::fromJson(f.readAll()).object();
    }

    // Re-read a key off the persisted JSON file (verifactu.service_key by default).
    QString rawValue(const QString &path, const QString &group, const QString &key)
    {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        return root.value(group).toObject().value(key).toString();
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void test_encryptedMarker()
    {
        QVERIFY(AppSettings::dpapiIsEncrypted("dpapi:v1:anything"));
        QVERIFY(!AppSettings::dpapiIsEncrypted("plain"));
        QVERIFY(!AppSettings::dpapiIsEncrypted(QString()));
    }

    void test_encryptDecryptRoundTrip()
    {
        const QString plain = "verifactu-service-key-AbC123!";
        const QString enc = AppSettings::dpapiEncrypt(plain);
#ifdef Q_OS_WIN
        // On Windows the value is real ciphertext carrying the marker.
        QVERIFY2(AppSettings::dpapiIsEncrypted(enc), qPrintable(enc));
        QVERIFY(enc != plain);
#endif
        // Round-trips on every platform (non-Windows is a passthrough).
        QCOMPARE(AppSettings::dpapiDecrypt(enc), plain);
    }

    void test_legacyPlaintextPassthrough()
    {
        // A stored value without the marker is treated as legacy plaintext and
        // returned unchanged (it gets re-encrypted on the next launch).
        QCOMPARE(AppSettings::dpapiDecrypt("legacy_plaintext_key"),
                 QStringLiteral("legacy_plaintext_key"));
    }

    void test_emptyEncrypt()
    {
        QCOMPARE(AppSettings::dpapiEncrypt(QString()), QString());
    }

    // Derived report paths are reportsRoot() + a hardcoded subdir.
    void test_derivedReportPaths()
    {
        const QString path = loadJson(R"({"reports":{"root":"/srv/reports"}})");
        Q_UNUSED(path);
        AppSettings *s = AppSettings::instance();
        QCOMPARE(s->reportsRoot(), QStringLiteral("/srv/reports"));
        QCOMPARE(s->contabilidadPath(),    QStringLiteral("/srv/reports/Contabilidad"));
        QCOMPARE(s->listadosPrendasPath(), QStringLiteral("/srv/reports/Listados/Prendas"));
        QCOMPARE(s->listadosGastosPath(),  QStringLiteral("/srv/reports/Listados/Gastos"));
    }

    // First-run language: the installer's choice when it is English, else Spanish.
    void test_initialLanguageFromInstaller()
    {
        QCOMPARE(AppLanguage::initialLanguage("en"), QStringLiteral("en"));
        QCOMPARE(AppLanguage::initialLanguage("es"), QStringLiteral("es"));
        QCOMPARE(AppLanguage::initialLanguage(""), QStringLiteral("es"));      // no installer value
        QCOMPARE(AppLanguage::initialLanguage("fr"), QStringLiteral("es"));    // unknown -> shop default
    }

    void test_releaseNotesResourcePerLanguage()
    {
        QCOMPARE(AppLanguage::releaseNotesResource("en"), QStringLiteral(":/docs/releases_notes.txt"));
        QCOMPARE(AppLanguage::releaseNotesResource("es"), QStringLiteral(":/docs/releases_notes_es.txt"));
    }

    // Both release-notes files list the same versions in the same order, so the
    // Spanish history (installer + Ayuda) never falls behind the English one.
    void test_releaseNotesFilesHaveTheSameVersions()
    {
        auto versions = [](const QString &path) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                return QStringList();
            QStringList out;
            const QRegularExpression header(QStringLiteral("^(r?\\d+\\.\\d+)\\s*$"));
            for (const QString &line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
                const QRegularExpressionMatch m = header.match(line);
                if (m.hasMatch())
                    out << m.captured(1);
            }
            return out;
        };
        const QStringList en = versions(QStringLiteral(LAIDEAL_SOURCE_DIR "/releases_notes.txt"));
        const QStringList es = versions(QStringLiteral(LAIDEAL_SOURCE_DIR "/releases_notes_es.txt"));
        QVERIFY(!en.isEmpty());
        QCOMPARE(es, en);
    }

    void test_ivaRateIsFixedAndIgnoresJson()
    {
        loadJson(R"({"taxes":{"iva_rate":10.0}})");
        QCOMPARE(AppSettings::instance()->ivaRate(), 21.0);

        loadJson(R"({"reports":{"root":"/x"}})");
        QCOMPARE(AppSettings::instance()->ivaRate(), 21.0);
    }

    // The direct client's NumeroInstalacion and chain marker are written to the file at
    // once: every record carries the number, so a crash before another save must not
    // give the next run a different one.
    void test_installationNumberAndChainMarkerPersistAtOnce()
    {
        const QString path = loadJson(R"({"database":{"path":"x.db"}})");
        const QString number = AppSettings::instance()->aeatInstallationNumber();
        QVERIFY(number.startsWith("LAIDEAL-"));
        QCOMPARE(rawValue(path, "verifactu", "aeat_installation"), number);
        AppSettings::instance()->setAeatChainMarker("30839|ABC");
        QCOMPARE(rawValue(path, "verifactu", "aeat_chain_marker"), QStringLiteral("30839|ABC"));
        AppSettings::instance()->loadFrom(path);                       // the next run
        QCOMPARE(AppSettings::instance()->aeatInstallationNumber(), number);
    }

    // A settings file that cannot be read is kept aside before defaults apply: the next
    // save would otherwise overwrite the only copy of the database path and credentials.
    void test_corruptFileKeptAside()
    {
        const QString path = loadJson("{ this is not json");
        QVERIFY(!QFile::exists(path));
        const QStringList aside = QDir(m_dir.path()).entryList({ QFileInfo(path).fileName() + ".corrupt-*" });
        QCOMPARE(aside.size(), 1);
        QFile kept(m_dir.filePath(aside.first()));
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), QByteArray("{ this is not json"));
    }

    // A failed save is remembered for the start-up message, and cleared by the next good one.
    void test_lastSaveErrorReported()
    {
        const QString path = loadJson(R"({"database":{"path":"a.db"}})");
        QVERIFY(AppSettings::instance()->save());
        QVERIFY(AppSettings::instance()->lastSaveError().isEmpty());
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkpath(path));                                        // a folder where the file goes
        QVERIFY(!AppSettings::instance()->save());
        QVERIFY(!AppSettings::instance()->lastSaveError().isEmpty());
        QVERIFY(QDir(path).removeRecursively());
        QVERIFY(AppSettings::instance()->save());
        QVERIFY(AppSettings::instance()->lastSaveError().isEmpty());
    }

    // The save replaces the file in one step and leaves no temporary file behind.
    void test_saveIsAtomic()
    {
        const QString path = loadJson(R"({"database":{"path":"a.db"}})");
        AppSettings::instance()->setDbPath("b.db");
        QVERIFY(AppSettings::instance()->save());
        QCOMPARE(QDir(m_dir.path()).entryList({ QFileInfo(path).fileName() + "*" }).size(), 1);
        QCOMPARE(QJsonDocument::fromJson([&path] { QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll(); }())
                     .object().value("database").toObject().value("path").toString(), QStringLiteral("b.db"));
    }

    // A plaintext service key on disk is encrypted at rest on load: the getter
    // still returns plaintext, and the file is rewritten with the encrypted value.
    void test_encryptSecretsAtRestMigration()
    {
        const QString plain = "plaintext-service-key-XyZ987";
        const QString path = loadJson(
            QString(R"({"verifactu":{"service_key":"%1"}})").arg(plain).toUtf8());

        // Caller always sees plaintext.
        QCOMPARE(AppSettings::instance()->verifactuServiceKey(), plain);

        // And the at-rest value was migrated.
        const QString stored = rawValue(path, "verifactu", "service_key");
#ifdef Q_OS_WIN
        // On Windows the stored value is real DPAPI ciphertext carrying the marker.
        QVERIFY2(AppSettings::dpapiIsEncrypted(stored), qPrintable(stored));
        QVERIFY(stored != plain);
#endif
        // Decrypting the stored value reproduces the plaintext on every platform.
        QCOMPARE(AppSettings::dpapiDecrypt(stored), plain);
    }

    // An already-encrypted key is left as-is (no double-encryption) and still
    // decrypts to the original.
    void test_encryptedKeyNotReEncrypted()
    {
        const QString plain = "already-encrypted-key-Q1";
        const QString enc = AppSettings::dpapiEncrypt(plain);
        const QString path = loadJson(
            QString(R"({"verifactu":{"service_key":"%1"}})").arg(enc).toUtf8());

        QCOMPARE(AppSettings::instance()->verifactuServiceKey(), plain);
        // The stored value is byte-for-byte the one we wrote (not re-wrapped).
        QCOMPARE(rawValue(path, "verifactu", "service_key"), enc);
    }

    // An empty file gets every default, and they are persisted.
    void test_defaultsOnEmptyFile()
    {
        const QString path = loadJson("{}");
        AppSettings *s = AppSettings::instance();
        QVERIFY(!s->language().isEmpty());
        QVERIFY(!s->businessName().isEmpty());
        QVERIFY(!s->verifactuProduction());
        QVERIFY(!s->verifactuDirectAeat());
        QVERIFY(!s->aeatDirectProduction());
        QVERIFY(s->verifactuPendingRecoveryEnabled());
        QCOMPARE(s->verifactuPendingRecoveryFloorDate(), QStringLiteral("2026-09-01"));
        QCOMPARE(s->verifactuPendingRecoveryFloorDate(), AppSettings::defaultPendingRecoveryFloorDate());
        QVERIFY(s->checkUpdatesOnStartup());
        QVERIFY(s->backupEnabled());
        QCOMPARE(s->paperWidthMm(), 80);
        QVERIFY(s->reportsRoot().endsWith("/Tintoreria"));
        s->save();
        const QJsonObject root = rawRoot(path);
        QCOMPARE(root.value("verifactu").toObject().value("environment").toString(), QStringLiteral("TESTING"));
        QCOMPARE(root.value("print").toObject().value("paper_width_mm").toInt(), 80);
        QVERIFY(root.value("backup").toObject().value("enabled").toBool());
    }

    // A flag the user turned off is never flipped back to its default.
    void test_defaultsDoNotOverrideExplicitFalse()
    {
        const QString path = loadJson(R"({"backup":{"enabled":false},"updater":{"check_on_startup":false},
                                          "verifactu":{"pending_recovery_enabled":false}})");
        AppSettings *s = AppSettings::instance();
        QVERIFY(!s->backupEnabled());
        QVERIFY(!s->checkUpdatesOnStartup());
        QVERIFY(!s->verifactuPendingRecoveryEnabled());
        QVERIFY(s->save());
        s->loadFrom(path);
        QVERIFY(!s->backupEnabled());
        QVERIFY(!s->checkUpdatesOnStartup());
        QVERIFY(!s->verifactuPendingRecoveryEnabled());
    }

    // Values of the wrong JSON type fall back to the defaults instead of misreading.
    void test_wrongTypesFallBackToDefaults()
    {
        loadJson(R"({"updater":{"check_on_startup":"yes"},"print":{"paper_width_mm":"58","enable":true}})");
        AppSettings *s = AppSettings::instance();
        QVERIFY(s->checkUpdatesOnStartup());
        QCOMPARE(s->paperWidthMm(), 80);
        QVERIFY(!s->enablePrinting());
    }

    // Print flags are "Yes"/"No" strings, the paper width a JSON number.
    void test_printFlagsAndPaperWidthFormat()
    {
        const QString path = loadJson("{}");
        AppSettings *s = AppSettings::instance();
        s->setEnablePrinting(true);
        s->setUseStatusApi(false);
        s->setPaperWidthMm(58);
        QVERIFY(s->save());
        const QJsonObject print = rawRoot(path).value("print").toObject();
        QCOMPARE(print.value("enable").toString(), QStringLiteral("Yes"));
        QCOMPARE(print.value("use_status_api").toString(), QStringLiteral("No"));
        QVERIFY(print.value("paper_width_mm").isDouble());
        QCOMPARE(print.value("paper_width_mm").toInt(), 58);
        s->loadFrom(path);
        QVERIFY(s->enablePrinting());
        QVERIFY(!s->useStatusApi());
        QCOMPARE(s->paperWidthMm(), 58);
    }

    // Production only for the exact PRODUCTION string; the direct connection only for "aeat".
    void test_environmentAndConnectionFlags()
    {
        const QString path = loadJson(R"({"verifactu":{"environment":"production","connection":"AEAT"}})");
        AppSettings *s = AppSettings::instance();
        QVERIFY(!s->verifactuProduction());
        QVERIFY(!s->verifactuDirectAeat());
        s->setVerifactuProduction(true);
        s->setVerifactuDirectAeat(true);
        s->setAeatDirectProduction(true);
        QVERIFY(s->save());
        const QJsonObject v = rawRoot(path).value("verifactu").toObject();
        QCOMPARE(v.value("environment").toString(), QStringLiteral("PRODUCTION"));
        QCOMPARE(v.value("connection").toString(), QStringLiteral("aeat"));
        QVERIFY(v.value("aeat_direct_production").toBool());
    }

    // Legacy report paths fold into reports.root and the obsolete keys disappear.
    void test_migrateLegacyReportPaths()
    {
        QString path = loadJson(R"({"reports":{"contabilidad_path":"/r/Contabilidad"},"print":{"template_path":"t"},
                                    "app":{"icon_path":"i"}})");
        QCOMPARE(AppSettings::instance()->reportsRoot(), QStringLiteral("/r"));
        QVERIFY(AppSettings::instance()->save());
        QJsonObject root = rawRoot(path);
        QVERIFY(!root.value("reports").toObject().contains("contabilidad_path"));
        QVERIFY(!root.value("print").toObject().contains("template_path"));
        QVERIFY(!root.value("app").toObject().contains("icon_path"));

        loadJson(R"({"reports":{"listados_prendas_path":"/x/Listados/Prendas"}})");
        QCOMPARE(AppSettings::instance()->reportsRoot(), QStringLiteral("/x"));
        loadJson(R"({"reports":{"listados_gastos_path":"/y/Listados/Gastos"}})");
        QCOMPARE(AppSettings::instance()->reportsRoot(), QStringLiteral("/y"));
        loadJson(R"({"reports":{"contabilidad_path":"/z/Other"}})");        // suffix does not match: its parent
        QVERIFY(AppSettings::instance()->reportsRoot().endsWith("/z"));
        loadJson(R"({"reports":{"root":"/keep","contabilidad_path":"/r/Contabilidad"}})");   // idempotent
        QCOMPARE(AppSettings::instance()->reportsRoot(), QStringLiteral("/keep"));
    }

    // Every setter survives a save and a reload, and no temporary file is left.
    void test_saveLoadRoundTrip()
    {
        const QString path = loadJson("{}");
        AppSettings *s = AppSettings::instance();
        s->setDbPath("C:/db/x.db");
        s->setBusinessName("Name");
        s->setBusinessPhone("958 000 000");
        s->setVerifactuNif("12345678Z");
        s->setVerifactuName("Company");
        s->setVerifactuServiceKey("key \u00f1 with spaces");
        s->setAeatCertificatePassword("pw-1");
        s->setAeatCertificateThumbprint("AB12");
        s->setBackupLastTime("2026-10-26T10:00:00");
        QVERIFY(s->save());
        s->loadFrom(path);
        QCOMPARE(s->dbPath(), QStringLiteral("C:/db/x.db"));
        QCOMPARE(s->businessName(), QStringLiteral("Name"));
        QCOMPARE(s->businessPhone(), QStringLiteral("958 000 000"));
        QCOMPARE(s->verifactuNif(), QStringLiteral("12345678Z"));
        QCOMPARE(s->verifactuName(), QStringLiteral("Company"));
        QCOMPARE(s->verifactuServiceKey(), QStringLiteral("key \u00f1 with spaces"));
        QCOMPARE(s->aeatCertificatePassword(), QStringLiteral("pw-1"));
        QCOMPARE(s->aeatCertificateThumbprint(), QStringLiteral("AB12"));
        QCOMPARE(s->backupLastTime(), QStringLiteral("2026-10-26T10:00:00"));
    }

    // The certificate password is encrypted at rest like the service key.
    void test_aeatCertificatePasswordEncryptedAtRest()
    {
        const QString path = loadJson(R"({"verifactu":{"aeat_certificate_password":"pfx-secret"}})");
        QCOMPARE(AppSettings::instance()->aeatCertificatePassword(), QStringLiteral("pfx-secret"));
        const QString stored = rawValue(path, "verifactu", "aeat_certificate_password");
#ifdef Q_OS_WIN
        QVERIFY2(AppSettings::dpapiIsEncrypted(stored), qPrintable(stored));
#endif
        QCOMPARE(AppSettings::dpapiDecrypt(stored), QStringLiteral("pfx-secret"));
        AppSettings::instance()->setAeatCertificatePassword("another");
        QVERIFY(AppSettings::instance()->save());
        QCOMPARE(AppSettings::dpapiDecrypt(rawValue(path, "verifactu", "aeat_certificate_password")),
                 QStringLiteral("another"));
    }

#ifdef Q_OS_WIN
    // An undecryptable blob (copied from another Windows user) reads as empty.
    void test_decryptFailureClearsField()
    {
        loadJson(R"({"verifactu":{"service_key":"dpapi:v1:AAAA"}})");
        QVERIFY(AppSettings::instance()->verifactuServiceKey().isEmpty());
        QVERIFY(AppSettings::dpapiDecrypt("dpapi:v1:").isEmpty());
    }
#endif

    // Loading a second file forgets the first one's values.
    void test_loadFromResetsState()
    {
        loadJson(R"({"database":{"path":"a.db"},"business":{"phone":"111"}})");
        QCOMPARE(AppSettings::instance()->businessPhone(), QStringLiteral("111"));
        loadJson(R"({"database":{"path":"b.db"}})");
        QCOMPARE(AppSettings::instance()->dbPath(), QStringLiteral("b.db"));
        QVERIFY(AppSettings::instance()->businessPhone().isEmpty());
    }

    // A blank floor date takes the default; a garbage one is returned as stored (consumers tolerate it).
    void test_pendingRecoveryFloorDefaultAndInvalid()
    {
        loadJson(R"({"verifactu":{"pending_recovery_floor_date":""}})");
        QCOMPARE(AppSettings::instance()->verifactuPendingRecoveryFloorDate(), QStringLiteral("2026-09-01"));
        loadJson(R"({"verifactu":{"pending_recovery_floor_date":"garbage"}})");
        QCOMPARE(AppSettings::instance()->verifactuPendingRecoveryFloorDate(), QStringLiteral("garbage"));
    }

    // Read-only load (--self-check): the same values, but a plaintext secret is not
    // rewritten, a corrupt file is not moved, a missing file is not created, save() fails.
    void test_loadReadOnlyNeverWrites()
    {
        const QString plainPath = m_dir.filePath("ro_plain.json");
        const QByteArray plainJson = R"({"verifactu":{"service_key":"plain-key"}})";
        {
            QFile f(plainPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(plainJson);
        }
        AppSettings *s = AppSettings::instance();
        QVERIFY(s->loadReadOnly(plainPath));
        QCOMPARE(s->verifactuServiceKey(), QStringLiteral("plain-key"));
        QVERIFY(!s->save());
        QVERIFY(!s->lastSaveError().isEmpty());
        QFile again(plainPath);
        QVERIFY(again.open(QIODevice::ReadOnly));
        QCOMPARE(again.readAll(), plainJson);

        const QString badPath = m_dir.filePath("ro_bad.json");
        {
            QFile f(badPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{ not json");
        }
        QVERIFY(!s->loadReadOnly(badPath));
        QVERIFY(QFile::exists(badPath));
        QCOMPARE(QDir(m_dir.path()).entryList({ "ro_bad.json.corrupt-*" }).size(), 0);

        const QString missing = m_dir.filePath("ro_missing.json");
        QVERIFY(!s->loadReadOnly(missing));
        QVERIFY(!QFile::exists(missing));
        QVERIFY(!s->language().isEmpty());                 // defaults still apply in memory

        // back to the normal pipeline: it writes again
        const QString path = loadJson("{}");
        QVERIFY(s->save());
        QVERIFY(QFile::exists(path));
    }
};

QTEST_GUILESS_MAIN(TestAppSettings)
#include "test_appsettings.moc"
