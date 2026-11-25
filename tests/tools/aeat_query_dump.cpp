// Saves AEAT's raw reply to one query of the direct client, to see what the service
// really returns (e.g. the record of a cancelled invoice) and to refresh the fixtures in
// tests/fixtures/aeat-responses. A query only: nothing is registered. Always AEAT's TEST
// environment (the URL is fixed here), with the certificate of a settings file:
//
//   aeat_query_dump --settings <settings.json> --out <reply.xml> [--month yyyy-MM] [--invoice <number>]
//
// The settings file is the pre-production kit's (verifactu.nif, company_name and
// aeat_certificate_thumbprint). Run locally only: the reply holds the issuer's data.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "aeatcertificate.h"
#include "aeatrecord.h"
#include "aeattransport.h"

namespace {
const QString kTestUrl = QStringLiteral("https://prewww1.aeat.es/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP");

int fail(const QString &text)
{
    QTextStream(stderr) << text << Qt::endl;
    return 1;
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOptions({ { "settings", "Settings file with the issuer and its certificate.", "file" },
                        { "out", "File the reply is written to.", "file" },
                        { "month", "Month asked for (default: this month).", "yyyy-MM" },
                        { "invoice", "Only this invoice number.", "number" } });
    parser.addHelpOption();
    parser.process(app);
    if (!parser.isSet("settings") || !parser.isSet("out"))
        return fail(parser.helpText());

    QFile settingsFile(parser.value("settings"));
    if (!settingsFile.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Cannot read %1").arg(settingsFile.fileName()));
    const QJsonObject verifactu = QJsonDocument::fromJson(settingsFile.readAll()).object().value("verifactu").toObject();
    const QString nif = verifactu.value("nif").toString();
    const QString name = verifactu.value("company_name").toString();
    const QString thumbprint = verifactu.value("aeat_certificate_thumbprint").toString();
    if (nif.isEmpty() || name.isEmpty() || thumbprint.isEmpty())
        return fail(QStringLiteral("The settings need verifactu.nif, company_name and aeat_certificate_thumbprint"));

    const QDate month = parser.isSet("month") ? QDate::fromString(parser.value("month") + "-01", Qt::ISODate)
                                              : QDate::currentDate();
    if (!month.isValid())
        return fail(QStringLiteral("--month must be yyyy-MM"));

    AeatCertificate certificate;
    QString error;
    if (!certificate.loadFromStore(thumbprint, &error))
        return fail(QStringLiteral("Certificate: %1").arg(error));

    const QString envelope = AeatRecord::queryEnvelope(name, nif, month.year(), month.month(), parser.value("invoice"), {}, true);
    const AeatTransport::Response reply = AeatTransport::postBlocking(kTestUrl, envelope.toUtf8(), certificate.context(), 30000);
    if (!reply.error.isEmpty())
        return fail(QStringLiteral("AEAT: %1").arg(reply.error));

    QFile out(parser.value("out"));
    if (!out.open(QIODevice::WriteOnly) || out.write(reply.body) != reply.body.size())
        return fail(QStringLiteral("Cannot write %1").arg(out.fileName()));
    QTextStream(stdout) << "HTTP " << reply.httpStatus << ", " << reply.body.size() << " bytes -> " << out.fileName() << Qt::endl;
    return 0;
}
