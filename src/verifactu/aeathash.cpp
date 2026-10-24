#include "aeathash.h"

#include <QCryptographicHash>
#include <QStringList>
#include <QUrl>

namespace AeatHash {

namespace {

const QString kDateFormat = QStringLiteral("dd-MM-yyyy");
const QString kQrProductionUrl = QStringLiteral("https://www2.agenciatributaria.gob.es/wlpl/TIKE-CONT/ValidarQR");
const QString kQrTestUrl = QStringLiteral("https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR");

QString pair(const QString &name, const QString &value)
{
    return name + QLatin1Char('=') + value.trimmed();
}

QString sha256Hex(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex()).toUpper();
}

QString dateText(const QDate &date)
{
    return date.isValid() ? date.toString(kDateFormat) : QString();
}

} // namespace

QString registrationHashInput(const RegistrationFields &f)
{
    return QStringList{
        pair(QStringLiteral("IDEmisorFactura"), f.issuerNif),
        pair(QStringLiteral("NumSerieFactura"), f.invoiceNumber),
        pair(QStringLiteral("FechaExpedicionFactura"), dateText(f.issueDate)),
        pair(QStringLiteral("TipoFactura"), f.invoiceType),
        pair(QStringLiteral("CuotaTotal"), f.totalTax),
        pair(QStringLiteral("ImporteTotal"), f.totalAmount),
        pair(QStringLiteral("Huella"), f.previousHash),
        pair(QStringLiteral("FechaHoraHusoGenRegistro"), f.generatedAt),
    }.join(QLatin1Char('&'));
}

QString cancellationHashInput(const CancellationFields &f)
{
    return QStringList{
        pair(QStringLiteral("IDEmisorFacturaAnulada"), f.issuerNif),
        pair(QStringLiteral("NumSerieFacturaAnulada"), f.invoiceNumber),
        pair(QStringLiteral("FechaExpedicionFacturaAnulada"), dateText(f.issueDate)),
        pair(QStringLiteral("Huella"), f.previousHash),
        pair(QStringLiteral("FechaHoraHusoGenRegistro"), f.generatedAt),
    }.join(QLatin1Char('&'));
}

QString registrationHash(const RegistrationFields &fields)
{
    return sha256Hex(registrationHashInput(fields));
}

QString cancellationHash(const CancellationFields &fields)
{
    return sha256Hex(cancellationHashInput(fields));
}

QString amountText(double amount)
{
    // Rounded half away from zero like every stored amount (sql_lite::roundToCents).
    const double cents = amount < 0 ? -qRound64(-amount * 100.0 + 1e-6) : qRound64(amount * 100.0 + 1e-6);
    return QString::number(cents / 100.0, 'f', 2);
}

QString timestampText(const QDateTime &moment)
{
    const int offset = moment.offsetFromUtc();
    const int minutes = qAbs(offset) / 60;
    return moment.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"))
           + (offset < 0 ? QLatin1Char('-') : QLatin1Char('+'))
           + QStringLiteral("%1:%2").arg(minutes / 60, 2, 10, QLatin1Char('0')).arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

QString qrValidationUrl(const QString &issuerNif, const QString &invoiceNumber, const QDate &issueDate,
                        const QString &totalAmount, bool testEnvironment)
{
    const auto encoded = [](const QString &value) { return QString::fromLatin1(QUrl::toPercentEncoding(value.trimmed())); };
    return (testEnvironment ? kQrTestUrl : kQrProductionUrl)
           + QStringLiteral("?nif=") + encoded(issuerNif)
           + QStringLiteral("&numserie=") + encoded(invoiceNumber)
           + QStringLiteral("&fecha=") + encoded(dateText(issueDate))
           + QStringLiteral("&importe=") + encoded(totalAmount);
}

} // namespace AeatHash
