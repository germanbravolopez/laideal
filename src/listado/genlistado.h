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

namespace Ui {
class GenListado;
}

class GenListado : public QDialog
{
    Q_OBJECT

public:
    explicit GenListado(const QSqlDatabase &database, QWidget *parent = nullptr);
    ~GenListado();
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
    void initial_settings();
    void set_cb_fechas();
    QString generate_html_prendas_table();
    void write_html(QString filename, QString html);
    QString generate_html_gastos_table_with_specific_conditions();
    QString generate_html_gastos_table();
    bool check_years_invoice_type_for_row(int row);
    QString add_suffix_to_filename();

    void on_bb_ok_cancel_accepted();
    void on_bb_ok_cancel_rejected();
    void on_checkb_allys_clicked(bool checked);

private:
    Ui::GenListado *ui;
    QSqlDatabase db;
};

#endif // GENLISTADO_H
