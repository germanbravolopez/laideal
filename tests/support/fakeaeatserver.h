#ifndef FAKEAEATSERVER_H
#define FAKEAEATSERVER_H

// A local stand-in for the AEAT VERI*FACTU SOAP service, for the direct client's
// tests: plain HTTP on 127.0.0.1 (no TLS, no certificate), pointed at with
// AeatDirectBackend::setEndpointOverride(server.url()). Like AEAT it registers
// every new record (Correcto, with a CSV), answers a record it already holds with
// the duplicate reply (Incorrecto 3000 + RegistroDuplicado), reports the wait time
// before the next submission, and answers queries from what it registered.
// Replies follow the official response schemas (see tests/fixtures/aeat-responses).

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QString>

class QTcpServer;
class QTcpSocket;

class FakeAeatServer : public QObject
{
    Q_OBJECT

public:
    struct SentRecord {
        QString operation;        // Alta / Anulacion
        QString invoiceNumber;
        QString issueDate;
        QString total;            // ImporteTotal (Alta)
        QString hash;             // the record's own Huella
        QString previousHash;     // Huella in its chain block (empty for the first)
        bool    afterRejection = false;
    };
    struct Request {
        QString           kind;   // RegFactu / Consulta
        QByteArray        body;
        QList<SentRecord> records;
        QString           queriedNumber;
        QDateTime         receivedAt;
    };
    struct Reply {
        QByteArray body;          // empty: the default AEAT-like answer
        int  status = 200;
        int  delayMs = 0;
        bool drop = false;        // close the connection without answering
        bool dropAfterRegistering = false;   // register the records, then lose the answer
    };

    explicit FakeAeatServer(QObject *parent = nullptr);
    ~FakeAeatServer() override;

    bool start();
    QString url() const;

    void setWaitSeconds(int seconds) { m_waitSeconds = seconds; }
    // The next record of `invoiceNumber` is rejected with this code (once).
    void rejectNext(const QString &invoiceNumber, const QString &code, const QString &description);
    // The next request gets `reply` instead of the default answer.
    void enqueue(const Reply &reply) { m_scripted.enqueue(reply); }

    QList<Request> requests() const { return m_requests; }
    QList<Request> submissions() const;
    QList<SentRecord> sentRecords() const;
    // Records AEAT holds as registered, keyed "Alta:30837".
    QHash<QString, SentRecord> registered() const { return m_registered; }

    static QByteArray faultReply(const QString &code, const QString &text);

private:
    void onNewConnection();
    void handle(QTcpSocket *socket);
    QByteArray answer(const Request &request);

    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QQueue<Reply> m_scripted;
    QList<Request> m_requests;
    QHash<QString, SentRecord> m_registered;
    QHash<QString, QString> m_registeredRequestId;
    QHash<QString, QPair<QString, QString>> m_rejectNext;
    int m_waitSeconds = 0;
    int m_counter = 0;
};

#endif // FAKEAEATSERVER_H
