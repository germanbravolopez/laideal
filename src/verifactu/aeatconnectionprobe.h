#ifndef AEATCONNECTIONPROBE_H
#define AEATCONNECTIONPROBE_H

// "Probar conexión" of the direct AEAT client: loads the chosen certificate and asks
// AEAT for the issuer's invoices of the current month. A query only - no record is
// generated or sent and the database is not touched - so it can run at any time, also
// while the shop's own direct client is working (the self-test cannot: it registers
// and cancels an invoice through the same outbox).

#include <QObject>
#include <QString>

#include "aeatcertificate.h"
#include "aeattransport.h"

class AeatConnectionProbe : public QObject
{
    Q_OBJECT

public:
    struct Settings {
        bool    testEnvironment = true;      // AEAT pre-production vs production (a query has no tax effect)
        QString issuerNif;
        QString issuerName;
        QString certificateThumbprint;       // from the Windows personal store, or else
        QString certificatePath;             // a .pfx / .p12 file
        QString certificatePassword;
    };

    explicit AeatConnectionProbe(const Settings &settings, QObject *parent = nullptr);

    // Reports each step on progress() and ends with finished().
    void run();

    // AEAT's answer to the query as one line for the user; `ok` false when it is not a
    // query reply (no connection, a refused certificate, a SOAP fault).
    static QString verdict(const AeatTransport::Response &response, bool *ok);

signals:
    void progress(bool ok, const QString &text);
    void finished(bool ok);

private:
    Settings        m_settings;
    AeatCertificate m_certificate;
    AeatTransport  *m_transport;

    void fail(const QString &text);
};

#endif // AEATCONNECTIONPROBE_H
