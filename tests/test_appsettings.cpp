// Tests for AppSettings' secret-at-rest helpers (Windows DPAPI). These are the
// dpapi* static delegators over the file-local encrypt/decrypt functions, so the
// security-relevant logic is covered without reading or writing the real
// ~/.laideal_settings.json. The derived-path / ivaRate getters are not covered
// here - they read the singleton's JSON and need a settings-injection seam
// (tracked separately).

#include <QtTest>

#include "appsettings.h"

class TestAppSettings : public QObject
{
    Q_OBJECT

private slots:
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
};

QTEST_GUILESS_MAIN(TestAppSettings)
#include "test_appsettings.moc"
