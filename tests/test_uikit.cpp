// UiKit's Enter rules: the dialog-wide one installed by setUpDialog (Enter in a field
// presses the primary button, or nothing without one; Enter on a focused button
// presses that button; a held key never repeats) and onEnter for a field of its own.

#include <QtTest>
#include <QDialog>
#include <QIntValidator>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "uikit.h"

class TestUiKit : public QObject
{
    Q_OBJECT

private:
    struct Form {
        QDialog dialog;
        QLineEdit *field = nullptr;
        QSpinBox *spin = nullptr;
        QPushButton *other = nullptr;       // a plain QPushButton: auto-default in a QDialog
        QPushButton *primary = nullptr;
        int otherClicks = 0;
        int primaryClicks = 0;

        explicit Form(bool withPrimary)
        {
            UiKit::setUpDialog(&dialog, "Prueba");
            auto *layout = new QVBoxLayout(&dialog);
            other = new QPushButton("Buscar");
            layout->addWidget(other);
            field = new QLineEdit;
            layout->addWidget(field);
            spin = new QSpinBox;
            layout->addWidget(spin);
            QObject::connect(other, &QPushButton::clicked, [this] { ++otherClicks; });
            if (withPrimary) {
                primary = UiKit::primaryButton("Guardar", "btnSave");
                layout->addWidget(primary);
                QObject::connect(primary, &QPushButton::clicked, [this] { ++primaryClicks; });
            }
            dialog.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        }
    };

    static void press(QWidget *w, int key = Qt::Key_Return, bool autoRepeat = false)
    {
        w->setFocus();
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, QString("\r"), autoRepeat);
        QApplication::sendEvent(w, &event);
    }

private slots:
    // The amount field shows the decimal comma whatever the language and takes "," or
    // "." as the decimal key, so the numeric keypad works either way.
    void test_amountSpinBox_commaShownEitherKeyTyped()
    {
        UiKit::AmountSpinBox box;
        box.setDecimals(2);
        box.setRange(-1000, 1000);
        box.setValue(12.5);
        QCOMPARE(box.text(), QStringLiteral("12,50"));
        for (const QString &typed : { QStringLiteral("7.25"), QStringLiteral("7,25") }) {
            box.selectAll();
            QTest::keyClicks(&box, typed);
            box.interpretText();
            QCOMPARE(box.value(), 7.25);
        }
        QCOMPARE(box.text(), QStringLiteral("7,25"));
    }

    // Enter in a field presses the primary button, once, and never another one.
    void test_enterInFieldPressesPrimary()
    {
        Form f(true);
        press(f.field);
        press(f.spin, Qt::Key_Enter);
        QCOMPARE(f.primaryClicks, 2);
        QCOMPARE(f.otherClicks, 0);
    }

    // Without a primary button Enter in a field does nothing (Qt pressed its default:
    // the first auto-default button, here Buscar).
    void test_noPrimaryNothingPressed()
    {
        Form f(false);
        press(f.field);
        press(f.spin);
        QCOMPARE(f.otherClicks, 0);
    }

    // Enter on a focused button presses that button.
    void test_enterOnFocusedButton()
    {
        Form f(true);
        press(f.other);
        QCOMPARE(f.otherClicks, 1);
        QCOMPARE(f.primaryClicks, 0);
    }

    // A held key does not repeat the primary action; a disabled primary is not pressed.
    void test_autoRepeatAndDisabledPrimary()
    {
        Form f(true);
        press(f.field, Qt::Key_Return, true);
        QCOMPARE(f.primaryClicks, 0);
        f.primary->setEnabled(false);
        press(f.field);
        QCOMPARE(f.primaryClicks, 0);
        QCOMPARE(f.otherClicks, 0);
        // With two primary buttons (one per mode), the enabled one is pressed.
        auto *second = UiKit::primaryButton("Cobrar", "btnPay");
        f.dialog.layout()->addWidget(second);
        int secondClicks = 0;
        QObject::connect(second, &QPushButton::clicked, [&secondClicks] { ++secondClicks; });
        second->show();
        press(f.field);
        QCOMPARE(secondClicks, 1);
        QCOMPARE(f.primaryClicks, 0);
    }

    // onEnter: the field's own action instead of the primary button; not repeated;
    // not run while the input is not acceptable.
    void test_onEnter()
    {
        Form f(true);
        int searches = 0;
        UiKit::onEnter(f.field, [&searches] { ++searches; });
        f.field->setValidator(new QIntValidator(1, 9999, f.field));
        f.field->setText("12");
        press(f.field);
        press(f.field, Qt::Key_Enter);
        press(f.field, Qt::Key_Return, true);
        QCOMPARE(searches, 2);
        QCOMPARE(f.primaryClicks, 0);
        f.field->setText("");                                   // not acceptable for the validator
        press(f.field);
        QCOMPARE(searches, 2);
        QCOMPARE(f.primaryClicks, 0);
    }
};

QTEST_MAIN(TestUiKit)
#include "test_uikit.moc"
