#ifndef REPORTHTML_H
#define REPORTHTML_H

#include <QString>

// Shared "clean professional" scaffolding for the A4 PDF reports
// (Contabilidad, Listados). Both modules render through QTextDocument, which
// only supports a subset of HTML/CSS (tables + inline attributes, no flexbox),
// so everything here stays table- and inline-style based. Keeping the style,
// business header and currency formatting in one place stops the two report
// generators from drifting apart.
namespace ReportHtml {

// Opens the document: <!DOCTYPE>..<style>..<body> + business header band
// (name / address / city / NIF / phone on the left, issue date on the right),
// an accent rule, the report <h1> title and an optional subtitle line.
QString documentOpen(const QString &title, const QString &subtitle = QString());

// Opening tag for a data table.
//   grid == true  -> uniform 1px grid via Qt's native table `border` attribute
//                    (good for the small single-page Contabilidad tables).
//   grid == false -> borderless; row separation comes from zebra striping + the
//                    shaded header. Use for long, multi-page tables (Listados),
//                    where QTextDocument's per-cell grid renders unevenly across
//                    page breaks.
QString tableOpen(bool grid = false);

// Closes the document: a footer with the generation timestamp + </body></html>.
QString documentClose();

// Currency in Spanish format with '.' thousands grouping, ',' decimal and a
// trailing euro sign, e.g. "1.234,56 €".
QString formatEuro(double value);

} // namespace ReportHtml

#endif // REPORTHTML_H
