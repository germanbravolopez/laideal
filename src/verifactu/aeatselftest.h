#ifndef AEATSELFTEST_H
#define AEATSELFTEST_H

// The proof of concept of the direct AEAT client, run from Configuración: always
// against AEAT's pre-production environment (no tax effect), with the owner's
// certificate. It checks the certificate, continues the test chain, registers a
// test invoice PRUEBA-<date and time> of 1,21 EUR, queries it back and cancels it,
// waiting the time AEAT asks between submissions. Every step is reported.

#include <QObject>
#include <QSqlDatabase>
#include <QString>

#include "aeatdirectbackend.h"

class AeatSelfTest : public QObject
{
    Q_OBJECT

public:
    AeatSelfTest(AeatDirectBackend::Config config, const QSqlDatabase &db, QObject *parent = nullptr);

    void run();
    QString invoiceNumber() const { return m_invoiceNumber; }

signals:
    void progress(bool ok, const QString &text);
    void finished(bool ok);

private:
    AeatDirectBackend *m_backend = nullptr;
    QString m_invoiceNumber;
    QDate   m_issueDate;
    QString m_pendingRequest;

    void fail(const QString &text);
    void submit();
    void query();
    void cancel();
};

#endif // AEATSELFTEST_H
