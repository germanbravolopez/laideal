// Tests for the SettingsDialog Verifactu tab: the service key is masked by
// default and the "Mostrar" toggle only switches how it is displayed - the text
// (and so the value saved on accept) is unchanged. AppSettings is pointed at a
// throwaway file via loadFrom(), so the real ~/.laideal_settings.json is never
// read or written. The dialog is never accepted (accept() would save).

#include <QtTest>
#include <QAbstractButton>
#include <QComboBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QLineEdit>
#include <QTemporaryDir>

#include "appsettings.h"
#include "settingsdialog.h"

class TestSettingsDialog : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        const QString path = m_dir.filePath("settings.json");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
        f.close();
        AppSettings::instance()->loadFrom(path);
        AppSettings::instance()->setVerifactuServiceKey("secret-key-123");
    }

    void test_serviceKeyMaskedByDefaultAndToggles()
    {
        SettingsDialog dlg;
        auto *key    = dlg.findChild<QLineEdit *>("serviceKeyEdit");
        auto *reveal = dlg.findChild<QAbstractButton *>("serviceKeyRevealButton");
        QVERIFY(key);
        QVERIFY(reveal);
        QCOMPARE(key->echoMode(), QLineEdit::Password);
        QCOMPARE(key->text(), QStringLiteral("secret-key-123"));

        reveal->click();
        QCOMPARE(key->echoMode(), QLineEdit::Normal);
        QCOMPARE(reveal->text(), QStringLiteral("Ocultar"));
        QCOMPARE(key->text(), QStringLiteral("secret-key-123"));   // display only

        reveal->click();
        QCOMPARE(key->echoMode(), QLineEdit::Password);
        QCOMPARE(reveal->text(), QStringLiteral("Mostrar"));
    }

    void test_serviceKeyMaskedAgainOnReopen()
    {
        {
            SettingsDialog first;
            first.findChild<QAbstractButton *>("serviceKeyRevealButton")->click();
        }
        SettingsDialog second;
        QCOMPARE(second.findChild<QLineEdit *>("serviceKeyEdit")->echoMode(), QLineEdit::Password);
    }

    // Direct AEAT connection: its group shows only for that connection, a store
    // certificate disables the file fields, the self-test button sends the form's
    // values, and saving stores the choice (the file password DPAPI-encrypted).
    void test_directAeatConnection()
    {
        const QString thumbprint(40, 'A');
        SettingsDialog dlg;
        dlg.setCertificateChoices({ qMakePair(QStringLiteral("PRUEBA (caduca el 01-01-2030)"), thumbprint) });
        auto *connection = dlg.findChild<QComboBox *>("cbConnection");
        auto *certificate = dlg.findChild<QComboBox *>("cbCertificate");
        auto *file = dlg.findChild<QLineEdit *>("leCertificateFile");
        auto *password = dlg.findChild<QLineEdit *>("leCertificatePassword");
        QVERIFY(connection && certificate && file && password);
        dlg.show();
        auto *tabs = dlg.findChild<QTabWidget *>();
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == QLatin1String("Verifactu"))
                tabs->setCurrentIndex(i);
        QCOMPARE(connection->currentData().toString(), QStringLiteral("irenesolutions"));   // default
        QVERIFY(!certificate->isVisible());
        connection->setCurrentIndex(connection->findData("aeat"));
        QVERIFY(certificate->isVisible());
        QCOMPARE(certificate->count(), 2);                             // the store certificate + "Archivo..."
        QVERIFY(certificate->currentData().toString().isEmpty());      // nothing chosen yet: file
        QVERIFY(file->isEnabled() && password->isEnabled());
        certificate->setCurrentIndex(certificate->findData(thumbprint));
        QVERIFY(!file->isEnabled() && !password->isEnabled());
        QCOMPARE(password->echoMode(), QLineEdit::Password);

        QSignalSpy spy(&dlg, &SettingsDialog::aeatSelfTestRequested);
        dlg.findChild<QPushButton *>("btnAeatSelfTest")->click();
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy[0][2].toString(), thumbprint);

        password->setText("clave-del-pfx");
        QMetaObject::invokeMethod(&dlg, "accept");
        AppSettings *s = AppSettings::instance();
        QVERIFY(s->verifactuDirectAeat());
        QCOMPARE(s->aeatCertificateThumbprint(), thumbprint);
        QCOMPARE(s->aeatCertificatePassword(), QStringLiteral("clave-del-pfx"));
        QFile saved(m_dir.filePath("settings.json"));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QByteArray json = saved.readAll();
#ifdef Q_OS_WIN
        QVERIFY(!json.contains("clave-del-pfx"));                     // never in plain text
#endif
        QVERIFY(s->aeatInstallationNumber().startsWith("LAIDEAL-"));
        QCOMPARE(s->aeatInstallationNumber(), s->aeatInstallationNumber());   // created once

        SettingsDialog reopened;
        reopened.setCertificateChoices({ qMakePair(QStringLiteral("PRUEBA"), thumbprint) });
        QCOMPARE(reopened.findChild<QComboBox *>("cbConnection")->currentData().toString(), QStringLiteral("aeat"));
        QCOMPARE(reopened.findChild<QComboBox *>("cbCertificate")->currentData().toString(), thumbprint);
        s->setVerifactuDirectAeat(false);
    }
};

QTEST_MAIN(TestSettingsDialog)
#include "test_settingsdialog.moc"
