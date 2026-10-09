#ifndef FAKEVERIFACTUSERVER_H
#define FAKEVERIFACTUSERVER_H

// A local stand-in for the IreneSolutions Verifactu REST service, so end-to-end
// tests never reach the real AEAT. It listens on 127.0.0.1 (random port), records
// every request (path + JSON body) and answers each endpoint either with a scripted
// reply queued by the test or with a default "accepted" reply shaped like the
// captured fixtures in test_verifactu_response. Point the app at it with
// VerifactuConfig::setEndpointOverride(server.baseUrl()).

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QString>

class QTcpServer;
class QTcpSocket;

class FakeVerifactuServer : public QObject
{
    Q_OBJECT

public:
    struct Reply {
        QByteArray body;        // JSON body; empty = the endpoint's default reply
        int  delayMs = 0;       // hold the reply back (simulates a slow AEAT)
        bool drop = false;      // close the connection without answering
    };
    struct Request {
        QString     endpoint;   // last path segment: Create, Cancel, GetFilteredList, GetQrCode
        QJsonObject json;
    };

    explicit FakeVerifactuServer(QObject *parent = nullptr);
    ~FakeVerifactuServer() override;

    bool start();
    QString baseUrl() const;    // http://127.0.0.1:<port>/Kivu/Taxes/Verifactu/Invoices

    // Next request to `endpoint` gets `reply`; later ones fall back to the default.
    void enqueue(const QString &endpoint, const Reply &reply);
    QList<Request> requests() const { return m_requests; }
    QList<Request> requestsTo(const QString &endpoint) const;
    void clear() { m_requests.clear(); m_scripted.clear(); m_csvCounter = 0; }

    // Reply builders shaped like the real service (see test_verifactu_response).
    static QByteArray acceptedReply(const QString &csv);
    static QByteArray rejectedReply(const QString &errorCode, const QString &description);
    static QByteArray queryReply(const QString &invoiceId, const QString &isoDate,
                                 double total, const QString &csv);

private:
    void onNewConnection();
    void handle(QTcpSocket *socket);
    QByteArray defaultReply(const QString &endpoint);

    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QHash<QString, QQueue<Reply>> m_scripted;
    QList<Request> m_requests;
    int m_csvCounter = 0;
};

#endif // FAKEVERIFACTUSERVER_H
