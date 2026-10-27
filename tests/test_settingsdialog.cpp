// Tests for the SettingsDialog Verifactu tab: the service key is masked by
// default and the "Mostrar" toggle only switches how it is displayed - the text
// (and so the value saved on accept) is unchanged. The direct-AEAT group and the
// production confirmations are covered too. AppSettings is pointed at a throwaway
// file via loadFrom(), so the real ~/.laideal_settings.json is never read or written.

#include <QtTest>
#include <QScopeGuard>
#include <QAbstractButton>
#include <QComboBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QMessageBox>
#include <QCheckBox>
#include <QDateEdit>
#include <QTimer>
#include <QApplication>
#include <QLineEdit>
#include <QLabel>
#include <QTemporaryDir>

#include "appsettings.h"
#include "settingsdialog.h"

class TestSettingsDialog : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_dbFile;

    static void answerBoxes(QMessageBox::StandardButton button, int *seen = nullptr)
    {
        QTimer::singleShot(0, [button, seen] {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(w)) {
                    if (seen) ++*seen;
                    if (QAbstractButton *b = box->button(button)) b->click(); else box->reject();
                }
        });
    }

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
        // An existing database file, so accepting never asks about a missing one.
        m_dbFile = m_dir.filePath("shop.db");
        QFile db(m_dbFile);
        QVERIFY(db.open(QIODevice::WriteOnly));
        db.close();
        AppSettings::instance()->setDbPath(m_dbFile);
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

    // The direct connection's own production switch: off by default (pre-production);
    // turning it on or off asks for confirmation, and leaving production is refused
    // while production records are unsent.
    void test_directProductionNeedsConfirmation()
    {
        AppSettings *s = AppSettings::instance();
        s->setVerifactuDirectAeat(true);
        s->setAeatDirectProduction(false);
        const auto answer = [](QMessageBox::StandardButton button) {
            QTimer::singleShot(0, [button] {
                for (QWidget *w : QApplication::topLevelWidgets())
                    if (auto *box = qobject_cast<QMessageBox *>(w))
                        if (QAbstractButton *b = box->button(button)) b->click(); else box->reject();
            });
        };
        {
            SettingsDialog dlg;
            auto *production = dlg.findChild<QCheckBox *>("chkDirectProduction");
            QVERIFY(production && !production->isChecked());
            production->setChecked(true);
            answer(QMessageBox::No);
            QMetaObject::invokeMethod(&dlg, "accept");
            QVERIFY(!s->aeatDirectProduction());
        }
        {
            SettingsDialog dlg;
            dlg.findChild<QCheckBox *>("chkDirectProduction")->setChecked(true);
            answer(QMessageBox::Yes);
            QMetaObject::invokeMethod(&dlg, "accept");
            QVERIFY(s->aeatDirectProduction());
        }
        // Leaving production: refused while production records are unsent, then confirmed.
        {
            SettingsDialog pending;
            pending.setDirectPendingCount(2);
            QVERIFY(pending.findChild<QCheckBox *>("chkDirectProduction")->isChecked());
            pending.findChild<QCheckBox *>("chkDirectProduction")->setChecked(false);
            answer(QMessageBox::Ok);
            QMetaObject::invokeMethod(&pending, "accept");
            QVERIFY(s->aeatDirectProduction());
        }
        {
            SettingsDialog leaving;
            leaving.setDirectPendingCount(0);
            leaving.findChild<QCheckBox *>("chkDirectProduction")->setChecked(false);
            answer(QMessageBox::No);
            QMetaObject::invokeMethod(&leaving, "accept");
            QVERIFY(s->aeatDirectProduction());
        }
        SettingsDialog reopened;
        reopened.setDirectPendingCount(0);
        reopened.findChild<QCheckBox *>("chkDirectProduction")->setChecked(false);
        answer(QMessageBox::Yes);
        QMetaObject::invokeMethod(&reopened, "accept");
        QVERIFY(!s->aeatDirectProduction());
        s->setVerifactuDirectAeat(false);
    }

    // The gateway's PRODUCCIÓN box is a production switch too: confirmed going in and out.
    void test_gatewayProductionNeedsConfirmation()
    {
        AppSettings *s = AppSettings::instance();
        const auto restore = qScopeGuard([s] { s->setVerifactuProduction(false); });
        s->setVerifactuDirectAeat(false);
        s->setVerifactuProduction(false);
        const auto answer = [](QMessageBox::StandardButton button) {
            QTimer::singleShot(0, [button] {
                for (QWidget *w : QApplication::topLevelWidgets())
                    if (auto *box = qobject_cast<QMessageBox *>(w))
                        if (QAbstractButton *b = box->button(button)) b->click(); else box->reject();
            });
        };
        {
            SettingsDialog dlg;
            dlg.findChild<QCheckBox *>("chkProduction")->setChecked(true);
            answer(QMessageBox::No);
            QMetaObject::invokeMethod(&dlg, "accept");
            QVERIFY(!s->verifactuProduction());
        }
        {
            SettingsDialog dlg;
            dlg.findChild<QCheckBox *>("chkProduction")->setChecked(true);
            answer(QMessageBox::Yes);
            QMetaObject::invokeMethod(&dlg, "accept");
            QVERIFY(s->verifactuProduction());
        }
        SettingsDialog leaving;
        leaving.findChild<QCheckBox *>("chkProduction")->setChecked(false);
        answer(QMessageBox::Yes);
        QMetaObject::invokeMethod(&leaving, "accept");
        QVERIFY(!s->verifactuProduction());
    }

    // Leaving the direct client never keeps its production flag: coming back would
    // otherwise skip the "Pasar a PRODUCCIÓN" question.
    void test_leavingDirectNeverKeepsProduction()
    {
        AppSettings *s = AppSettings::instance();
        const auto restore = qScopeGuard([s] { s->setVerifactuDirectAeat(false); s->setAeatDirectProduction(false); });
        s->setVerifactuDirectAeat(true);
        s->setAeatDirectProduction(true);
        SettingsDialog dlg;
        dlg.setDirectPendingCount(0);
        auto *connection = dlg.findChild<QComboBox *>("cbConnection");
        connection->setCurrentIndex(connection->findData(QStringLiteral("irenesolutions")));
        QVERIFY(dlg.findChild<QCheckBox *>("chkDirectProduction")->isChecked());   // hidden, still ticked
        QTimer::singleShot(0, [] {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(w))
                    box->button(QMessageBox::Yes)->click();
        });
        QMetaObject::invokeMethod(&dlg, "accept");
        QVERIFY(!s->verifactuDirectAeat());
        QVERIFY(!s->aeatDirectProduction());
    }

    // The startup-recovery floor date shows as dd-MM-yyyy, a bad stored value shows the
    // default, and accepting stores ISO.
    void test_pendingRecoveryFloorRoundTrip()
    {
        AppSettings *s = AppSettings::instance();
        const QString before = s->verifactuPendingRecoveryFloorDate();
        const auto restore = qScopeGuard([s, before] { s->setVerifactuPendingRecoveryFloorDate(before); });
        s->setVerifactuPendingRecoveryFloorDate(QStringLiteral("2026-10-05"));
        {
            SettingsDialog dlg;
            auto *floor = dlg.findChild<QDateEdit *>();
            QVERIFY(floor);
            QCOMPARE(floor->date(), QDate(2026, 10, 5));
            floor->setDate(QDate(2026, 11, 2));
            QMetaObject::invokeMethod(&dlg, "accept");
            QCOMPARE(s->verifactuPendingRecoveryFloorDate(), QStringLiteral("2026-11-02"));
        }
        s->setVerifactuPendingRecoveryFloorDate(QStringLiteral("garbage"));
        SettingsDialog fallback;
        QCOMPARE(fallback.findChild<QDateEdit *>()->date(),
                 QDate::fromString(AppSettings::defaultPendingRecoveryFloorDate(), Qt::ISODate));
    }

    // "Probar conexión" sends the form's values trimmed.
    void test_testConnectionSignalCarriesTrimmedValues()
    {
        SettingsDialog dlg;
        QLineEdit *key = dlg.findChild<QLineEdit *>("serviceKeyEdit");
        QVERIFY(key);
        key->setText("  spaced-key  ");
        QPushButton *test = nullptr;
        for (QPushButton *b : dlg.findChildren<QPushButton *>())
            if (b->text().startsWith(QLatin1String("Probar conex")))
                test = b;
        QVERIFY(test);
        QSignalSpy spy(&dlg, &SettingsDialog::testConnectionRequested);
        test->click();
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy[0][2].toString(), QStringLiteral("spaced-key"));
    }

    // While the shop's own direct client works in pre-production the self-test is off: the
    // tickets are the test, and both would send the same pre-production outbox at once.
    void test_selfTestOffWhileTheShopIsInPreProduction()
    {
        AppSettings *s = AppSettings::instance();
        const auto restore = qScopeGuard([s] { s->setVerifactuDirectAeat(false); s->setAeatDirectProduction(false); });
        s->setVerifactuDirectAeat(true);
        s->setAeatDirectProduction(false);
        {
            SettingsDialog dlg;
            QVERIFY(!dlg.findChild<QPushButton *>("btnAeatSelfTest")->isEnabled());
        }
        s->setAeatDirectProduction(true);
        {
            SettingsDialog dlg;
            QVERIFY(dlg.findChild<QPushButton *>("btnAeatSelfTest")->isEnabled());
        }
        s->setVerifactuDirectAeat(false);
        SettingsDialog gateway;
        QVERIFY(gateway.findChild<QPushButton *>("btnAeatSelfTest")->isEnabled());
    }

    // Leaving the direct connection is refused while it has unsent records.
    void test_leavingDirectRefusedWithPendingRecords()
    {
        AppSettings *s = AppSettings::instance();
        s->setVerifactuDirectAeat(true);
        {
            SettingsDialog dlg;
            dlg.setDirectPendingCount(3);
            auto *connection = dlg.findChild<QComboBox *>("cbConnection");
            connection->setCurrentIndex(connection->findData("irenesolutions"));
            QTimer::singleShot(0, [] {
                for (QWidget *w : QApplication::topLevelWidgets())
                    if (auto *box = qobject_cast<QMessageBox *>(w))
                        box->accept();
            });
            QMetaObject::invokeMethod(&dlg, "accept");
            QVERIFY(s->verifactuDirectAeat());                           // not saved
        }
        SettingsDialog none;
        none.setDirectPendingCount(0);
        auto *connection = none.findChild<QComboBox *>("cbConnection");
        connection->setCurrentIndex(connection->findData("irenesolutions"));
        QMetaObject::invokeMethod(&none, "accept");
        QVERIFY(!s->verifactuDirectAeat());
    }

    // Accepting a database path that is not an existing file asks first; No keeps the
    // dialog open and the old path.
    void test_missingDatabasePathAsksBeforeSaving()
    {
        AppSettings *s = AppSettings::instance();
        const auto restore = qScopeGuard([s, this] {
            SettingsDialog::setMissingDbConfirm({});
            s->setDbPath(m_dbFile);
        });
        int asked = 0;
        QString askedPath;
        bool answer = false;
        SettingsDialog::setMissingDbConfirm([&](const QString &path) { ++asked; askedPath = path; return answer; });
        const QString missing = m_dir.filePath("typo.db");
        {
            SettingsDialog dlg;
            for (QLineEdit *le : dlg.findChildren<QLineEdit *>())
                if (le->text() == m_dbFile) le->setText(missing);
            QMetaObject::invokeMethod(&dlg, "accept");
            QCOMPARE(asked, 1);
            QCOMPARE(askedPath, missing);
            QCOMPARE(s->dbPath(), m_dbFile);                 // No: not saved, dialog still open
            QVERIFY(dlg.result() != QDialog::Accepted);
            answer = true;
            QMetaObject::invokeMethod(&dlg, "accept");
            QCOMPARE(asked, 2);
            QCOMPARE(s->dbPath(), missing);                  // Yes: kept anyway
            QCOMPARE(dlg.result(), int(QDialog::Accepted));
        }
        s->setDbPath(m_dbFile);
        SettingsDialog existing;
        QMetaObject::invokeMethod(&existing, "accept");
        QCOMPARE(asked, 2);                                  // an existing file is never questioned
    }

    // A saved certificate that the Windows store no longer lists stays selected, is shown as
    // missing and survives accepting; picking another one replaces it.
    void test_missingCertificateThumbprintIsKept()
    {
        AppSettings *s = AppSettings::instance();
        const auto restore = qScopeGuard([s] { s->setAeatCertificateThumbprint(QString()); });
        const QString gone(40, 'B');
        const QString other(40, 'C');
        s->setAeatCertificateThumbprint(gone);
        {
            SettingsDialog dlg;
            dlg.setCertificateChoices({ qMakePair(QStringLiteral("OTRO"), other) });
            auto *certificate = dlg.findChild<QComboBox *>("cbCertificate");
            QCOMPARE(certificate->currentData().toString(), gone);
            QVERIFY(certificate->currentText().contains(QStringLiteral("no encontrado en el almacén")));
            QMetaObject::invokeMethod(&dlg, "accept");
            QCOMPARE(s->aeatCertificateThumbprint(), gone);
        }
        {
            SettingsDialog dlg;
            dlg.setCertificateChoices({ qMakePair(QStringLiteral("OTRO"), other) });
            auto *certificate = dlg.findChild<QComboBox *>("cbCertificate");
            certificate->setCurrentIndex(certificate->findData(other));
            QMetaObject::invokeMethod(&dlg, "accept");
            QCOMPARE(s->aeatCertificateThumbprint(), other);
        }
        // Listed again: shown by its label, no "missing" placeholder left.
        s->setAeatCertificateThumbprint(gone);
        SettingsDialog listed;
        listed.setCertificateChoices({ qMakePair(QStringLiteral("VIGENTE"), gone) });
        auto *certificate = listed.findChild<QComboBox *>("cbCertificate");
        QCOMPARE(certificate->currentText(), QStringLiteral("VIGENTE"));
        QCOMPARE(certificate->count(), 2);
    }

    // A floor date after today shows a warning (it does not block saving).
    void test_futureFloorDateWarns()
    {
        AppSettings *s = AppSettings::instance();
        const QString before = s->verifactuPendingRecoveryFloorDate();
        const auto restore = qScopeGuard([s, before] { s->setVerifactuPendingRecoveryFloorDate(before); });
        s->setVerifactuPendingRecoveryFloorDate(QStringLiteral("2026-09-01"));
        {
            SettingsDialog dlg;
            auto *warning = dlg.findChild<QLabel *>("lblFloorFuture");
            auto *floor = dlg.findChild<QDateEdit *>();
            QVERIFY(warning && floor);
            floor->setDate(QDate::currentDate());
            QVERIFY(warning->isHidden());
            floor->setDate(QDate::currentDate().addDays(1));
            QVERIFY(!warning->isHidden());
            floor->setDate(QDate::currentDate().addDays(-1));
            QVERIFY(warning->isHidden());
            floor->setDate(QDate::currentDate().addDays(30));
            QMetaObject::invokeMethod(&dlg, "accept");       // not blocked
            QCOMPARE(s->verifactuPendingRecoveryFloorDate(), QDate::currentDate().addDays(30).toString(Qt::ISODate));
        }
        SettingsDialog reopened;                              // warned from the start
        QVERIFY(!reopened.findChild<QLabel *>("lblFloorFuture")->isHidden());
    }

    // A failed encryption warns on accept; the values are still saved.
    void test_encryptionFailureWarns()
    {
        AppSettings *s = AppSettings::instance();
        const auto restore = qScopeGuard([s] { AppSettings::forceEncryptionFailure(false); s->setVerifactuServiceKey("secret-key-123"); });
        AppSettings::forceEncryptionFailure(true);
        SettingsDialog dlg;
        dlg.findChild<QLineEdit *>("serviceKeyEdit")->setText("brand-new-key");
        int seen = 0;
        answerBoxes(QMessageBox::Ok, &seen);
        QMetaObject::invokeMethod(&dlg, "accept");
        QCOMPARE(seen, 1);
        QCOMPARE(s->verifactuServiceKey(), QStringLiteral("brand-new-key"));
    }

    // A stored key that no longer decrypts is flagged in the dialog.
    void test_unreadableKeyIsFlagged()
    {
        {
            SettingsDialog dlg;
            QVERIFY(dlg.findChild<QLabel *>("lblKeyUnreadable")->isHidden());
        }
        const QString path = m_dir.filePath("unreadable.json");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"verifactu":{"service_key":"dpapi:v1:","aeat_certificate_password":"dpapi:v1:"},"database":{"path":")" + m_dbFile.toUtf8() + R"("}})");
        f.close();
        const auto restore = qScopeGuard([this] { AppSettings::instance()->loadFrom(m_dir.filePath("settings.json")); });
        AppSettings::instance()->loadFrom(path);
        SettingsDialog dlg;
        QVERIFY(!dlg.findChild<QLabel *>("lblKeyUnreadable")->isHidden());
        QVERIFY(!dlg.findChild<QLabel *>("lblCertPasswordUnreadable")->isHidden());
        QVERIFY(dlg.findChild<QLineEdit *>("serviceKeyEdit")->text().isEmpty());
    }
};

QTEST_MAIN(TestSettingsDialog)
#include "test_settingsdialog.moc"
