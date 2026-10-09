#ifndef MODALAUTOCLOSER_H
#define MODALAUTOCLOSER_H

// Closes any QMessageBox that a flow under test opens, shortly after it appears,
// and records its title and text. Headless there is nobody to click "Aceptar", so
// without this a single pop-up would block the suite until the CTest timeout. A
// scenario asserts on messages() to check that the right warning was shown.

#include <QApplication>
#include <QMessageBox>
#include <QStringList>
#include <QTimer>

class ModalAutoCloser : public QObject
{
public:
    explicit ModalAutoCloser(QObject *parent = nullptr) : QObject(parent)
    {
        m_timer.setInterval(30);
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            for (QWidget *w : QApplication::topLevelWidgets()) {
                auto *box = qobject_cast<QMessageBox *>(w);
                if (box && box->isVisible()) {
                    m_messages << box->windowTitle() + QStringLiteral(": ") + box->text();
                    box->done(QMessageBox::Ok);
                }
            }
        });
        m_timer.start();
    }

    QStringList messages() const { return m_messages; }
    bool sawMessageContaining(const QString &text) const
    {
        for (const QString &m : m_messages)
            if (m.contains(text, Qt::CaseInsensitive))
                return true;
        return false;
    }
    void clear() { m_messages.clear(); }

private:
    QTimer m_timer;
    QStringList m_messages;
};

#endif // MODALAUTOCLOSER_H
