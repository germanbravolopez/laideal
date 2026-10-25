// The fake AEAT SOAP service of the e2e suites (support/fakeaeatserver) as a
// standalone process, for the UI smoke test: the UI-test build of laideal.exe is
// started with `--fake-aeat <url>` and registers its invoices here, never at AEAT.
//
//   fake_aeat_server --url-file <file> [--records-file <file>]
//
// Writes its URL (http://127.0.0.1:<port>) to --url-file once listening, and the
// records it holds as registered to --records-file (JSON) whenever they change.
// Runs until killed.

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

#include "fakeaeatserver.h"

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QByteArray recordsJson(const FakeAeatServer &server)
{
    QJsonArray records;
    const auto registered = server.registered();
    for (auto it = registered.constBegin(); it != registered.constEnd(); ++it) {
        records.append(QJsonObject{
            {"key", it.key()},
            {"operation", it->operation},
            {"invoiceNumber", it->invoiceNumber},
            {"issueDate", it->issueDate},
            {"total", it->total},
        });
    }
    return QJsonDocument(QJsonObject{{"requests", server.requests().size()}, {"registered", records}})
        .toJson(QJsonDocument::Indented);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const QString urlFile = args.value(args.indexOf("--url-file") + 1);
    const int recordsArg = args.indexOf("--records-file");
    const QString recordsFile = recordsArg > 0 ? args.value(recordsArg + 1) : QString();
    if (args.indexOf("--url-file") < 0 || urlFile.isEmpty()) {
        qCritical("usage: fake_aeat_server --url-file <file> [--records-file <file>]");
        return 2;
    }

    FakeAeatServer server;
    if (!server.start()) {
        qCritical("fake_aeat_server: cannot listen on 127.0.0.1");
        return 1;
    }
    if (!writeFile(urlFile, server.url().toUtf8()))
        return 1;
    qInfo().noquote() << "fake_aeat_server: listening at" << server.url();

    if (!recordsFile.isEmpty()) {
        auto *timer = new QTimer(&app);
        int lastRequests = -1;
        QObject::connect(timer, &QTimer::timeout, &app, [&server, recordsFile, &lastRequests]() {
            if (server.requests().size() == lastRequests)
                return;
            lastRequests = server.requests().size();
            writeFile(recordsFile, recordsJson(server));
        });
        timer->start(250);
    }
    return app.exec();
}
