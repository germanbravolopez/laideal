#ifndef MODALDRIVER_H
#define MODALDRIVER_H

// Operates the application's own modal dialogs (PayDialog, the Verifactu dialog,
// the AEAT comparison dialog) the way an operator would. A screen that opens a
// dialog with exec() blocks the test until the dialog closes, so the scenario
// registers what to do with it beforehand: expect(match, act) runs `act` once on
// the first visible window `match` accepts. Message boxes are left to
// ModalAutoCloser.

#include <QApplication>
#include <QTimer>
#include <QWidget>
#include <functional>

class ModalDriver : public QObject
{
public:
    using Match = std::function<bool(QWidget *)>;
    using Act   = std::function<void(QWidget *)>;

    explicit ModalDriver(QObject *parent = nullptr) : QObject(parent)
    {
        m_timer.setInterval(30);
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            if (m_busy || m_steps.isEmpty())
                return;
            for (QWidget *w : QApplication::topLevelWidgets()) {
                if (!w->isVisible())
                    continue;
                for (int i = 0; i < m_steps.size(); ++i) {
                    if (!m_steps[i].match(w))
                        continue;
                    const Act act = m_steps.takeAt(i).act;
                    m_busy = true;      // act may spin a nested event loop
                    act(w);
                    m_busy = false;
                    ++m_handled;
                    return;
                }
            }
        });
        m_timer.start();
    }

    void expect(Match match, Act act) { m_steps.append({ std::move(match), std::move(act) }); }
    static Match named(const QString &objectName)
    {
        return [objectName](QWidget *w) { return w->objectName() == objectName; };
    }
    template <typename T> static Match ofType()
    {
        return [](QWidget *w) { return qobject_cast<T *>(w) != nullptr; };
    }

    int handled() const { return m_handled; }
    int pending() const { return m_steps.size(); }
    void clear() { m_steps.clear(); m_handled = 0; }

private:
    struct Step { Match match; Act act; };
    QTimer m_timer;
    QList<Step> m_steps;
    int m_handled = 0;
    bool m_busy = false;
};

#endif // MODALDRIVER_H
