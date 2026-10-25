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

    // The ingresos IVA rate is a fixed 21 %: a legacy taxes.iva_rate in the JSON
    // is ignored (and not rewritten), whatever its value.
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

    // A plaintext service key on disk is encrypted at rest on load: the getter
    // still returns plaintext, and the file is rewritten with the encrypted value.
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
};

QTEST_GUILESS_MAIN(TestAppSettings)
#include "test_appsettings.moc"
