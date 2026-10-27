#ifndef REPORTHTML_H
#define REPORTHTML_H

#include <QString>

// Shared "clean professional" scaffolding for the A4 PDF reports (Contabilidad,
// Listados and the declaración responsable, which adds its own bare markup).
// Everything renders through QTextDocument, which only supports a subset of
// HTML/CSS, so it stays table- and inline-style based. The reports are Spanish
// whatever the app language, by design; the zebra striping of long tables is
// added by the callers, not by tableOpen().
namespace ReportHtml {

// Opens the document: <!DOCTYPE>..<style>..<body> + business header band
// (name / address / city / NIF / phone on the left, issue date on the right),
// an accent rule, the report <h1> title and an optional subtitle line. The business
// text is escaped here; title and subtitle are HTML by contract.
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

// Prints `html` to an A4 PDF at `file` (15 mm margins) and opens it in the PDF
// viewer unless opening is switched off. Returns false when the file could not be
// written. Every report goes through here.
bool writePdf(const QString &file, const QString &html);
// Test seam: when false, reports are written but not opened (the e2e bench runs headless).
void setOpenGeneratedReports(bool open);
// Test seam: the HTML of the last report written (the PDFs are compressed).
QString lastReportHtml();

} // namespace ReportHtml

#endif // REPORTHTML_H
