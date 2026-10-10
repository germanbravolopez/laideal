#include "fakeaeatserver.h"

#include <QPointer>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QXmlStreamReader>

namespace {

const QString kNs = QStringLiteral("https://www2.agenciatributaria.gob.es/static_files/common/internet/dep/aplicaciones/es/aeat/tike/cont/ws/");

QString envelope(const QString &payload)
{
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><env:Envelope xmlns:env=\"http://schemas.xmlsoap.org/soap/envelope/\">"
                          "<env:Header/><env:Body Id=\"Body\">%1</env:Body></env:Envelope>").arg(payload);
}

// Reads the records (or the queried number) out of a request body.
FakeAeatServer::Request parseRequest(const QByteArray &body)
{
    FakeAeatServer::Request request;
    request.body = body;
    request.receivedAt = QDateTime::currentDateTime();
    QXmlStreamReader r(body);
    QStringList path;
    FakeAeatServer::SentRecord record;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const QString name = r.name().toString();
            path << name;
            if (name == QLatin1String("RegFactuSistemaFacturacion"))
                request.kind = QStringLiteral("RegFactu");
            else if (name == QLatin1String("ConsultaFactuSistemaFacturacion"))
                request.kind = QStringLiteral("Consulta");
            else if (name == QLatin1String("RegistroAlta") || name == QLatin1String("RegistroAnulacion")) {
                record = FakeAeatServer::SentRecord();
                record.operation = name == QLatin1String("RegistroAlta") ? QStringLiteral("Alta") : QStringLiteral("Anulacion");
            }
        } else if (r.isEndElement()) {
            const QString name = path.takeLast();
            if (name == QLatin1String("RegistroAlta") || name == QLatin1String("RegistroAnulacion"))
                request.records << record;
        } else if (r.isCharacters() && !r.isWhitespace() && !path.isEmpty()) {
            const QString name = path.last();
            const QString text = r.text().toString();
            if (request.kind == QLatin1String("Consulta")) {
                if (name == QLatin1String("NumSerieFactura"))
                    request.queriedNumber = text;
                continue;
            }
            const bool inChain = path.contains(QStringLiteral("Encadenamiento"));
            if (inChain) {
                if (name == QLatin1String("Huella"))
                    record.previousHash = text;
                continue;
            }
            if (path.contains(QStringLiteral("SistemaInformatico")) || path.contains(QStringLiteral("FacturasRectificadas")))
                continue;
            if (name == QLatin1String("NumSerieFactura") || name == QLatin1String("NumSerieFacturaAnulada"))
                record.invoiceNumber = text;
            else if (name == QLatin1String("FechaExpedicionFactura") || name == QLatin1String("FechaExpedicionFacturaAnulada"))
                record.issueDate = text;
            else if (name == QLatin1String("ImporteTotal"))
                record.total = text;
            else if (name == QLatin1String("Huella"))
                record.hash = text;
            else if (name == QLatin1String("RechazoPrevio"))
                record.afterRejection = (text == QLatin1String("S"));
        }
    }
    return request;
}

} // namespace

FakeAeatServer::FakeAeatServer(QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &FakeAeatServer::onNewConnection);
}

FakeAeatServer::~FakeAeatServer() = default;

bool FakeAeatServer::start()
{
    return m_server->listen(QHostAddress::LocalHost, 0);
}

QString FakeAeatServer::url() const
{
    return QStringLiteral("http://127.0.0.1:%1/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP").arg(m_server->serverPort());
}

void FakeAeatServer::rejectNext(const QString &invoiceNumber, const QString &code, const QString &description)
{
    m_rejectNext.insert(invoiceNumber, { code, description });
}

QList<FakeAeatServer::Request> FakeAeatServer::submissions() const
{
    QList<Request> out;
    for (const Request &r : m_requests)
        if (r.kind == QLatin1String("RegFactu"))
            out << r;
    return out;
}

QList<FakeAeatServer::SentRecord> FakeAeatServer::sentRecords() const
{
    QList<SentRecord> out;
    for (const Request &r : submissions())
        out += r.records;
    return out;
}

QByteArray FakeAeatServer::faultReply(const QString &code, const QString &text)
{
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><env:Envelope xmlns:env=\"http://schemas.xmlsoap.org/soap/envelope/\">"
                          "<env:Body><env:Fault><faultcode>%1</faultcode><faultstring>%2</faultstring></env:Fault></env:Body></env:Envelope>")
        .arg(code, text).toUtf8();
}

void FakeAeatServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handle(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void FakeAeatServer::handle(QTcpSocket *socket)
{
    QByteArray &buf = m_buffers[socket];
    buf += socket->readAll();
    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;
    int contentLength = 0;
    for (const QByteArray &line : buf.left(headerEnd).split('\n')) {
        if (line.toLower().startsWith("content-length:"))
            contentLength = line.mid(15).trimmed().toInt();
    }
    if (buf.size() < headerEnd + 4 + contentLength)
        return;
    const Request request = parseRequest(buf.mid(headerEnd + 4, contentLength));
    m_buffers.remove(socket);
    m_requests << request;

    Reply reply;
    if (!m_scripted.isEmpty())
        reply = m_scripted.dequeue();
    if (reply.drop) {
        socket->abort();
        return;
    }
    const QByteArray body = reply.body.isEmpty() ? answer(request) : reply.body;
    if (reply.dropAfterRegistering) {
        socket->abort();
        return;
    }
    const QByteArray statusLine = reply.status == 200 ? QByteArray("200 OK") : QByteArray::number(reply.status) + " Error";
    QPointer<QTcpSocket> guard(socket);
    auto send = [guard, body, statusLine]() {
        if (!guard)
            return;
        guard->write("HTTP/1.1 " + statusLine + "\r\nContent-Type: text/xml; charset=utf-8\r\nContent-Length: "
                     + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        guard->disconnectFromHost();
    };
    if (reply.delayMs > 0)
        QTimer::singleShot(reply.delayMs, this, send);
    else
        send();
}

QByteArray FakeAeatServer::answer(const Request &request)
{
    if (request.kind == QLatin1String("Consulta")) {
        const SentRecord held = m_registered.value(QStringLiteral("Alta:") + request.queriedNumber);
        QString records;
        if (!held.invoiceNumber.isEmpty()) {
            const bool cancelled = m_registered.contains(QStringLiteral("Anulacion:") + request.queriedNumber);
            records = QStringLiteral(
                "<tikLRRC:RegistroRespuestaConsultaFactuSistemaFacturacion><tikLRRC:IDFactura><tik:IDEmisorFactura>89890001K</tik:IDEmisorFactura>"
                "<tik:NumSerieFactura>%1</tik:NumSerieFactura><tik:FechaExpedicionFactura>%2</tik:FechaExpedicionFactura></tikLRRC:IDFactura>"
                "<tikLRRC:DatosRegistroFacturacion><tikLRRC:ImporteTotal>%3</tikLRRC:ImporteTotal><tikLRRC:Huella>%4</tikLRRC:Huella>"
                "</tikLRRC:DatosRegistroFacturacion><tikLRRC:DatosPresentacion><tik:NIFPresentador>89890001K</tik:NIFPresentador>"
                "<tik:TimestampPresentacion>2026-10-10T10:00:05+02:00</tik:TimestampPresentacion><tik:IdPeticion>%5</tik:IdPeticion>"
                "</tikLRRC:DatosPresentacion><tikLRRC:EstadoRegistro><tikLRRC:TimestampUltimaModificacion>2026-10-10T10:00:05+02:00"
                "</tikLRRC:TimestampUltimaModificacion><tikLRRC:EstadoRegistro>%6</tikLRRC:EstadoRegistro></tikLRRC:EstadoRegistro>"
                "</tikLRRC:RegistroRespuestaConsultaFactuSistemaFacturacion>")
                .arg(held.invoiceNumber, held.issueDate, held.total, held.hash,
                     m_registeredRequestId.value(QStringLiteral("Alta:") + held.invoiceNumber),
                     cancelled ? QStringLiteral("Anulado") : QStringLiteral("Correcto"));
        }
        return envelope(QStringLiteral(
            "<tikLRRC:RespuestaConsultaFactuSistemaFacturacion xmlns:tikLRRC=\"%1RespuestaConsultaLR.xsd\" xmlns:tik=\"%1SuministroInformacion.xsd\">"
            "<tikLRRC:Cabecera><tik:IDVersion>1.0</tik:IDVersion><tik:ObligadoEmision><tik:NombreRazon>X</tik:NombreRazon><tik:NIF>89890001K</tik:NIF>"
            "</tik:ObligadoEmision></tikLRRC:Cabecera><tikLRRC:PeriodoImputacion><tikLRRC:Ejercicio>2026</tikLRRC:Ejercicio>"
            "<tikLRRC:Periodo>10</tikLRRC:Periodo></tikLRRC:PeriodoImputacion><tikLRRC:IndicadorPaginacion>N</tikLRRC:IndicadorPaginacion>"
            "<tikLRRC:ResultadoConsulta>%2</tikLRRC:ResultadoConsulta>%3</tikLRRC:RespuestaConsultaFactuSistemaFacturacion>")
            .arg(kNs, records.isEmpty() ? QStringLiteral("SinDatos") : QStringLiteral("ConDatos"), records)).toUtf8();
    }

    const QString requestId = QStringLiteral("2026101000%1").arg(++m_counter, 5, 10, QLatin1Char('0'));
    QString lines;
    int accepted = 0;
    for (const SentRecord &record : request.records) {
        const QString key = record.operation + QLatin1Char(':') + record.invoiceNumber;
        QString state = QStringLiteral("Correcto");
        QString extra;
        if (m_rejectNext.contains(record.invoiceNumber)) {
            const auto rejection = m_rejectNext.take(record.invoiceNumber);
            state = QStringLiteral("Incorrecto");
            extra = QStringLiteral("<tikR:CodigoErrorRegistro>%1</tikR:CodigoErrorRegistro>"
                                   "<tikR:DescripcionErrorRegistro>%2</tikR:DescripcionErrorRegistro>").arg(rejection.first, rejection.second);
        } else if (m_registered.contains(key)) {
            state = QStringLiteral("Incorrecto");
            extra = QStringLiteral("<tikR:CodigoErrorRegistro>3000</tikR:CodigoErrorRegistro>"
                                   "<tikR:DescripcionErrorRegistro>Registro de facturación duplicado.</tikR:DescripcionErrorRegistro>"
                                   "<tikR:RegistroDuplicado><tik:IdPeticionRegistroDuplicado>%1</tik:IdPeticionRegistroDuplicado>"
                                   "<tik:EstadoRegistroDuplicado>Correcta</tik:EstadoRegistroDuplicado></tikR:RegistroDuplicado>")
                        .arg(m_registeredRequestId.value(key));
        } else {
            m_registered.insert(key, record);
            m_registeredRequestId.insert(key, requestId);
            ++accepted;
        }
        lines += QStringLiteral("<tikR:RespuestaLinea><tikR:IDFactura><tik:IDEmisorFactura>89890001K</tik:IDEmisorFactura>"
                                "<tik:NumSerieFactura>%1</tik:NumSerieFactura><tik:FechaExpedicionFactura>%2</tik:FechaExpedicionFactura>"
                                "</tikR:IDFactura><tikR:Operacion><tik:TipoOperacion>%3</tik:TipoOperacion></tikR:Operacion>"
                                "<tikR:EstadoRegistro>%4</tikR:EstadoRegistro>%5</tikR:RespuestaLinea>")
                     .arg(record.invoiceNumber, record.issueDate, record.operation, state, extra);
    }
    const QString sendState = accepted == request.records.size() ? QStringLiteral("Correcto")
                              : accepted == 0                    ? QStringLiteral("Incorrecto")
                                                                 : QStringLiteral("ParcialmenteCorrecto");
    const QString csv = accepted > 0 ? QStringLiteral("<tikR:CSV>A-FAKE%1</tikR:CSV>").arg(m_counter, 10, 10, QLatin1Char('0')) : QString();
    return envelope(QStringLiteral(
        "<tikR:RespuestaRegFactuSistemaFacturacion xmlns:tikR=\"%1RespuestaSuministro.xsd\" xmlns:tik=\"%1SuministroInformacion.xsd\">"
        "%2<tikR:DatosPresentacion><tik:NIFPresentador>89890001K</tik:NIFPresentador><tik:TimestampPresentacion>2026-10-10T10:00:05+02:00"
        "</tik:TimestampPresentacion></tikR:DatosPresentacion><tikR:Cabecera><tik:ObligadoEmision><tik:NombreRazon>X</tik:NombreRazon>"
        "<tik:NIF>89890001K</tik:NIF></tik:ObligadoEmision></tikR:Cabecera><tikR:TiempoEsperaEnvio>%3</tikR:TiempoEsperaEnvio>"
        "<tikR:EstadoEnvio>%4</tikR:EstadoEnvio>%5</tikR:RespuestaRegFactuSistemaFacturacion>")
        .arg(kNs, csv, QString::number(m_waitSeconds), sendState, lines)).toUtf8();
}
