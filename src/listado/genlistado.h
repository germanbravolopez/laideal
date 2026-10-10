#ifndef GENLISTADO_H
#define GENLISTADO_H

#include <QDialog>
#include <QDir>
#include <QDesktopServices>
#include <QAbstractItemModel>
#include <QSqlDatabase>

#define C_FECHAS      "Fechas"
#define C_PROVEEDORES "Proveedores"
#define C_INCL_TODOS  "Incluir todos los gastos"
#define C_CONTAB_CERR "Contabilidad cerrada"
#define C_NO_ROWS     "No rows to print"

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
    QAbstractItemModel *model;
    QString table_name;
    void print_table();

    // Pure helpers extracted from the ui-reading slots, exposed for unit testing.
    // Filename suffix for the gastos listado PDF: "agrupado_<agrupar>_<tipo>_<year>",
    // lower-cased with spaces as underscores; "todos_los_años" when allYears.
    static QString filenameSuffix(const QString &agrupar, const QString &tipoGastos,
                                  bool allYears, const QString &selectedYear);
    // Whether a gastos row passes the year + accounting-type filter.
    static bool shouldPrintGastoRow(bool allYears, const QString &selectedYear,
                                    const QString &rowYear, bool onlyClosed, bool rowClosed);

private slots:
    void onGenerateClicked();

private:
    void buildUi();
    void set_cb_fechas();
    QString generate_html_prendas_table();
    QString generate_html_gastos_table_with_specific_conditions();
    QString generate_html_gastos_table();
    bool check_years_invoice_type_for_row(int row);
    QString add_suffix_to_filename();

    QSqlDatabase db;
    QComboBox *m_cbYear = nullptr;
    QCheckBox *m_chkAllYears = nullptr;
    QComboBox *m_cbType = nullptr;
    QComboBox *m_cbGroup = nullptr;
    UiKit::ResultPanel *m_lblResult = nullptr;
};

#endif // GENLISTADO_H
