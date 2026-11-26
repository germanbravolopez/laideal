#ifndef TEXTFORMAT_H
#define TEXTFORMAT_H

// Pure text helpers: money and decimal parsing/display and the stored date text.
// Qt Core only; no database, no widgets.

#include <QDate>
#include <QString>

// Dates are stored as text in this format ("05-03-2026"); parseDbDate gives an invalid
// QDate for anything else.
inline const QString kDbDateFormat = QStringLiteral("dd-MM-yyyy");
inline QDate parseDbDate(const QString &text) { return QDate::fromString(text, kDbDateFormat); }
inline QString dbDateText(const QDate &date) { return date.toString(kDbDateFormat); }

// Money is stored and sent to AEAT in cents, rounded half away from zero (28.405 ->
// 28.41): the tolerance absorbs binary error, so 28.405 (28.40499...) still rounds up.
double      roundToCents(double value);
// An importe as stored in `ingresos`: "28.41". Text input may use a decimal comma.
QString     moneyText(double value);
QString     moneyText(const QString &value);
// Strict reading of a typed or stored amount: an optional sign, digits and at most one
// decimal separator (dot or comma). "1.234,50", "abc" and "" are refused (false), never
// read as 0 - every amount writer refuses them.
bool        moneyParse(const QString &text, double *value);
// What the windows and printed tickets show: a decimal comma whatever the language
// ("28,41"), no thousands separator. Storage, the maths and AEAT keep the dot.
QString     displayMoney(double value);
QString     displayMoney(const QString &stored);
// displayMoney followed by a space and the euro sign.
QString     displayEuro(double value);
QString     displayEuro(const QString &stored);
// Reads a typed decimal that may use a comma ("2,5"): trimmed, comma read as the dot.
// Lenient like QString::toDouble (0 and *ok false when unreadable); moneyParse is the strict one.
double      parseDecimal(const QString &text, bool *ok = nullptr);
// A stored decimal such as a size ("2.5") shown with the comma ("2,5"); other text unchanged.
QString     displayDecimal(const QString &stored);

#endif // TEXTFORMAT_H
