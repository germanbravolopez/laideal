#include "fakeverifactuserver.h"

#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

FakeVerifactuServer::FakeVerifactuServer(QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &FakeVerifactuServer::onNewConnection);
}

FakeVerifactuServer::~FakeVerifactuServer() = default;

bool FakeVerifactuServer::start()
{
    return m_server->listen(QHostAddress::LocalHost, 0);
}

QString FakeVerifactuServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1/Kivu/Taxes/Verifactu/Invoices").arg(m_server->serverPort());
}

void FakeVerifactuServer::enqueue(const QString &endpoint, const Reply &reply)
{
    m_scripted[endpoint].enqueue(reply);
}

QList<FakeVerifactuServer::Request> FakeVerifactuServer::requestsTo(const QString &endpoint) const
{
    QList<Request> out;
    for (const Request &r : m_requests)
        if (r.endpoint == endpoint)
            out << r;
    return out;
}

void FakeVerifactuServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handle(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

// Minimal HTTP/1.1: wait for the headers and the Content-Length body, then answer
// once and close. The client (QNetworkAccessManager) only POSTs JSON here.
void FakeVerifactuServer::handle(QTcpSocket *socket)
{
    QByteArray &buf = m_buffers[socket];
    buf += socket->readAll();
    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;
    const QByteArray head = buf.left(headerEnd);
    int contentLength = 0;
    for (const QByteArray &line : head.split('\n')) {
        if (line.toLower().startsWith("content-length:"))
            contentLength = line.mid(15).trimmed().toInt();
    }
    if (buf.size() < headerEnd + 4 + contentLength)
        return;

    const QByteArray requestLine = head.left(head.indexOf('\r'));
    const QByteArray path = requestLine.split(' ').value(1);
    const QString endpoint = QString::fromLatin1(path.mid(path.lastIndexOf('/') + 1));
    const QByteArray body = buf.mid(headerEnd + 4, contentLength);
    m_buffers.remove(socket);
    m_requests << Request{endpoint, QJsonDocument::fromJson(body).object()};

    Reply reply;
    if (m_scripted.contains(endpoint) && !m_scripted[endpoint].isEmpty())
        reply = m_scripted[endpoint].dequeue();
    if (reply.drop) {
        socket->abort();
        return;
    }
    const QByteArray json = reply.body.isEmpty() ? defaultReply(endpoint) : reply.body;
    QPointer<QTcpSocket> guard(socket);
    auto send = [guard, json]() {
        if (!guard)
            return;
        guard->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                     + QByteArray::number(json.size()) + "\r\nConnection: close\r\n\r\n" + json);
        guard->disconnectFromHost();
    };
    if (reply.delayMs > 0)
        QTimer::singleShot(reply.delayMs, this, send);
    else
        send();
}

QByteArray FakeVerifactuServer::defaultReply(const QString &endpoint)
{
    if (endpoint == QLatin1String("Create") || endpoint == QLatin1String("Cancel"))
        return acceptedReply(QStringLiteral("A-FAKE%1").arg(++m_csvCounter, 4, 10, QLatin1Char('0')));
    if (endpoint == QLatin1String("GetFilteredList"))
        return R"({"Offset":0,"Count":0,"Items":[],"ResultCode":0,"ResultMessage":"Retrieved element filtered list."})";
    return R"({"ResultCode":0,"Return":""})";
}

// Small real image so the client's QR decode path runs as in production.
static QString tinyQrPngBase64()
{
    QImage img(21, 21, QImage::Format_Mono);
    img.fill(1);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    return QString::fromLatin1(png.toBase64());
}

QByteArray FakeVerifactuServer::acceptedReply(const QString &csv)
{
    QJsonObject ret;
    ret["CSV"] = csv;
    ret["ValidationUrl"] = QStringLiteral("https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR?fake=") + csv;
    ret["Xml"] = QStringLiteral("<r><sum1:Huella>ABCDEF0123456789</sum1:Huella></r>");
    ret["QrCode"] = tinyQrPngBase64();
    QJsonObject root;
    root["ResultCode"] = 0;
    root["Return"] = ret;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray FakeVerifactuServer::rejectedReply(const QString &errorCode, const QString &description)
{
    QJsonObject ret;
    ret["ErrorCode"] = errorCode;
    ret["ErrorDescription"] = description;
    QJsonObject root;
    root["ResultCode"] = 0;
    root["Return"] = ret;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray FakeVerifactuServer::queryReply(const QString &invoiceId, const QString &isoDate,
                                           double total, const QString &csv)
{
    QJsonObject item;
    item["InvoiceID"] = invoiceId;
    item["InvoiceDate"] = isoDate + QStringLiteral("T00:00:00");
    item["TotalAmount"] = total;
    item["IsRejected"] = false;
    item["StatusResponse"] = QStringLiteral("Correcto");
    item["ErrorCode"] = QJsonValue();
    item["CSV"] = csv;
    item["ValidationUrl"] = QStringLiteral("https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR?fake=") + csv;
    QJsonObject root;
    root["Count"] = 1;
    root["Items"] = QJsonArray{item};
    root["ResultCode"] = 0;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
