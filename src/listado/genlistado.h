#ifndef GENLISTADO_H
#define GENLISTADO_H

#include <QDialog>
#include <QDir>
#include <QDesktopServices>
#include <QAbstractItemModel>
#include <QSet>
#include <QVector>
#include <QtGlobal>
#include <QSqlDatabase>

inline const QString C_FECHAS      = QStringLiteral("Fechas");
inline const QString C_PROVEEDORES = QStringLiteral("Proveedores");
inline const QString C_INCL_TODOS  = QStringLiteral("Incluir todos los gastos");
inline const QString C_CONTAB_CERR = QStringLiteral("Contabilidad cerrada");

class QCheckBox;
class QComboBox;
namespace UiKit { class ResultPanel; }

// Listado de gastos -> Generar PDF: the expense listing of one year (or all),
// grouped by date or supplier, optionally only closed quarters. Built in code with
// the shared UiKit style; stays open and reports each PDF in its result panel.
// Listado de prendas uses print_table() without showing the dialog.
class GenListado : public QDialog
{
    Q_OBJECT

public:
    explicit GenListado(const QSqlDatabase &database, QWidget *parent = nullptr);
    QAbstractItemModel *model = nullptr;
    QString table_name;
    // Writes the prendas PDF; false when it could not be written (see printedFile()).
    bool print_table();
    QString printedFile() const { return m_lastPrintedFile; }

    // Pure helpers extracted from the ui-reading slots, exposed for unit testing.
    // Filename suffix for the gastos listado PDF: "agrupado_<agrupar>_<tipo>_<year>",
    // lower-cased with spaces as underscores; "todos_los_años" when allYears.
    static QString filenameSuffix(const QString &agrupar, const QString &tipoGastos,
                                  bool allYears, const QString &selectedYear);
    // Whether a gastos row passes the year + accounting-type filter.
    static bool shouldPrintGastoRow(bool allYears, const QString &selectedYear,
                                    const QString &rowYear, bool onlyClosed, bool rowClosed);

    // The yyyy of a dd-MM-yyyy fecha (spaces around it ignored).
    static QString rowYear(const QString &fecha);
    // The model's rows ordered by the text of `column`, then by the fecha in `dateColumn`
    // (none when < 0), leaving the model's own sort alone.
    static QVector<int> rowsSortedBy(const QAbstractItemModel *model, int column, int dateColumn);
    // The cells of a supplier's IMPORTE TOTAL row: 6 for the label, then the amount under
    // Importe (plus the "Cerrado por contabilidad" column when the table has it).
    static QString supplierTotalCells(qint64 totalCents, bool withClosedColumn);

    // The importe of a gasto split into base and IVA, in whole cents so sums stay exact.
    // `iva` is the rate as typed ("21", "10", "4", "21.0", "21,0"); zero or unreadable
    // means no IVA (base == importe); an unreadable importe counts as 0.
    struct GastoAmounts { qint64 importeCents; qint64 baseCents; qint64 ivaCents; };
    static GastoAmounts splitGasto(const QString &importe, const QString &iva);

private slots:
    void onGenerateClicked();

private:
    void buildUi();
    void set_cb_fechas();
    QString generate_html_prendas_table(const QVector<int> &rows);
    QString generate_html_gastos_table_with_specific_conditions();
    // Empty when no row passes the filters.
    QString generate_html_gastos_table(const QVector<int> &rows);
    bool check_years_invoice_type_for_row(int row);
    // The row's fecha is in a quarter Contabilidad closed (m_closedQuarters).
    bool rowClosed(int row) const;
    QString add_suffix_to_filename();

    QSqlDatabase db;
    QString m_lastPrintedFile;
    QSet<int> m_closedQuarters;   // quarterKey()s, read when the gastos table is built
    QComboBox *m_cbYear = nullptr;
    QCheckBox *m_chkAllYears = nullptr;
    QComboBox *m_cbType = nullptr;
    QComboBox *m_cbGroup = nullptr;
    UiKit::ResultPanel *m_lblResult = nullptr;
};

#endif // GENLISTADO_H
