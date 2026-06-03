#include "reporthtml.h"
#include "appsettings.h"

#include <QDate>
#include <QDateTime>

namespace ReportHtml {

// QTextDocument renders only a subset of CSS. border-collapse is unreliable (it
// leaves doubled internal borders against a thin outer edge), so the data-table
// grid is drawn via the table `border` attribute in tableOpen() instead, and the
// stylesheet only carries fonts, headings and the header-row fill. Per-cell
// padding comes from the table's cellpadding attribute.
static QString styleBlock()
{
    return
    "<style>"
        "body { font-family: Arial, Helvetica, sans-serif; color: #2c3e50; font-size: 11px; }"
        "h1 { font-size: 18px; color: #2c3e50; margin: 6px 0 2px 0; }"
        "h2 { font-size: 14px; color: #34495e; margin: 12px 0 4px 0; }"
        "h3 { font-size: 12px; color: #34495e; margin: 10px 0 3px 0; }"
        "th { background-color: #e8ecf0; text-align: left; }"
    "</style>";
}

QString documentOpen(const QString &title, const QString &subtitle)
{
    AppSettings *s = AppSettings::instance();

    QString identity;
    if (!s->verifactuNif().isEmpty())
        identity += "NIF: " + s->verifactuNif();
    if (!s->businessPhone().isEmpty())
        identity += (identity.isEmpty() ? QString() : " &middot; ") + "Tel: " + s->businessPhone();

    QString html =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    + styleBlock() +
    "</head><body>"
        "<table width='100%' style='border:0;'>"
            "<tr>"
                "<td style='border:0; vertical-align:top;'>"
                    "<span style='font-size:16px; font-weight:bold; color:#2c3e50;'>" + s->businessName() + "</span><br>"
                    "<span>" + s->businessAddress() + "</span><br>"
                    "<span>" + s->businessCity() + "</span>"
                    + (identity.isEmpty() ? QString() : "<br><span>" + identity + "</span>") +
                "</td>"
                "<td style='border:0; text-align:right; vertical-align:top; color:#566573;'>"
                    "Fecha de emisi&oacute;n:<br>" + QDate::currentDate().toString("dd-MM-yyyy") +
                "</td>"
            "</tr>"
        "</table>"
        "<hr>"
        "<h1>" + title + "</h1>";

    if (!subtitle.isEmpty())
        html += "<p style='color:#566573; margin:0;'>" + subtitle + "</p>";

    return html;
}

QString tableOpen(bool grid)
{
    if (grid)
        return "<table border='1' cellspacing='0' cellpadding='5' style='border-color:#b6bec7;'>";
    return "<table cellspacing='0' cellpadding='6'>";
}

QString documentClose()
{
    // No <hr> here: a rule placed straight after a content-width table is laid
    // out beside it by QTextDocument instead of below it. The right-aligned
    // timestamp with top spacing reads as the footer on its own.
    return
        "<p style='font-size:9px; color:#7f8c8d; text-align:right; margin-top:24px;'>"
            "Documento generado autom&aacute;ticamente por La Ideal el "
            + QDateTime::currentDateTime().toString("dd-MM-yyyy HH:mm") +
        "</p>"
    "</body></html>";
}

QString formatEuro(double value)
{
    // Spanish format: '.' thousands grouping, ',' decimal, two decimals, e.g.
    // "1.234,56 €". Grouped manually because QLocale::toString(value,'f',2)
    // grouped inconsistently (no separator for 1234, a single misplaced one for
    // 1000000).
    const bool negative = value < 0.0;
    const qlonglong cents = qRound64(qAbs(value) * 100.0);
    const qlonglong intPart = cents / 100;
    const int frac = static_cast<int>(cents % 100);

    const QString digits = QString::number(intPart);
    QString grouped;
    int n = 0;
    for (int i = digits.size() - 1; i >= 0; --i) {
        grouped.prepend(digits.at(i));
        if (++n % 3 == 0 && i > 0)
            grouped.prepend('.');
    }

    QString out = grouped + "," + QStringLiteral("%1").arg(frac, 2, 10, QChar('0'));
    if (negative)
        out.prepend('-');
    return out + " €";
}

} // namespace ReportHtml
