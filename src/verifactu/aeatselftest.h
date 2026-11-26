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
#include <QTimer>

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
    // Its backend sends every pending record of the issuer: the app settles those too.
    void recordSettled(const QString &invoiceId, bool cancellation, const VerifactuResult &result);

private:
    AeatDirectBackend *m_backend = nullptr;
    QString m_invoiceNumber;
    QDate   m_issueDate;
    QString m_pendingRequest;
    QTimer  m_settleTimer;
    QMetaObject::Connection m_settled;

    void fail(const char *step, const QString &text);
    bool awaitSettlement(bool cancellation, const VerifactuResult &result);
    void submit();
    void registrationAnswered(const VerifactuResult &r);
    void query();
    void cancel();
    void cancellationAnswered(const VerifactuResult &r);
};

#endif // AEATSELFTEST_H
