// Tests for the SettingsDialog Verifactu tab: the service key is masked by
// default and the "Mostrar" toggle only switches how it is displayed - the text
// (and so the value saved on accept) is unchanged. AppSettings is pointed at a
// throwaway file via loadFrom(), so the real ~/.laideal_settings.json is never
// read or written. The dialog is never accepted (accept() would save). The
// printer list is stubbed: QPrinterInfo blocks on CI runners without a spooler.

#include <QtTest>
#include <QAbstractButton>
#include <QLineEdit>
#include <QTemporaryDir>

#include "appsettings.h"
#include "settingsdialog.h"

static QStringList fakePrinterNames() { return { QStringLiteral("TM-T20III") }; }

class TestSettingsDialog : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        SettingsDialog::setPrinterNamesSource(fakePrinterNames);
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
};

QTEST_MAIN(TestSettingsDialog)
#include "test_settingsdialog.moc"
