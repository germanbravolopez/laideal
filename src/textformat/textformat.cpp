#include "textformat.h"

#include <QRegularExpression>

#include <cmath>

double roundToCents(double value)
{
    const double cents = value * 100.0;
    return std::round(cents + (cents >= 0.0 ? 1e-6 : -1e-6)) / 100.0;
}

QString moneyText(double value)
{
    return QString::number(roundToCents(value), 'f', 2);
}

QString moneyText(const QString &value)
{
    return moneyText(parseDecimal(value));
}

bool moneyParse(const QString &text, double *value)
{
    static const QRegularExpression rx(QStringLiteral(R"(^[+-]?\d+([.,]\d*)?$)"));
    const QString t = text.trimmed();
    if (!rx.match(t).hasMatch())
        return false;
    bool ok = false;
    const double v = QString(t).replace(',', '.').toDouble(&ok);
    if (ok && value)
        *value = v;
    return ok;
}

QString displayMoney(double value)
{
    return moneyText(value).replace(QLatin1Char('.'), QLatin1Char(','));
}

QString displayMoney(const QString &stored)
{
    return displayMoney(moneyText(stored).toDouble());
}

QString displayEuro(double value)
{
    return displayMoney(value) + QStringLiteral(" €");
}

QString displayEuro(const QString &stored)
{
    return displayMoney(stored) + QStringLiteral(" €");
}

double parseDecimal(const QString &text, bool *ok)
{
    return text.trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(ok);
}

QString displayDecimal(const QString &stored)
{
    return QString(stored).replace(QLatin1Char('.'), QLatin1Char(','));
}
