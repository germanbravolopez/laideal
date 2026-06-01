#ifndef CONTABILIDAD_H
#define CONTABILIDAD_H

#include <QDialog>
#include <QMessageBox>
#include <QSqlQueryModel>
#include <QSqlDatabase>
#include <QMessageBox>
#include <QDesktopServices>
#include <QDir>
#include <QDate>

namespace Ui {
class Contabilidad;
}

class Contabilidad : public QDialog
{
    Q_OBJECT

public:
    // Accounting period mode. Values match the cb_config combobox item order
    // (see contabilidad.ui), so the logic no longer depends on the Spanish
    // display strings: renaming an item cannot silently break the comparisons.
    enum ConfigMode { Mensual = 0, Trimestral = 1, Anual = 2 };

    explicit Contabilidad(const QSqlDatabase &database, QWidget *parent = nullptr);
    ~Contabilidad();
    bool revertirOn = false; // indicates whether the dialog is being used to revert an already done contabilidad (true) or to do a new contabilidad (false)
    void resetAllContents();

private slots:
    void initialSettings();

    void on_bb_ok_cancel_accepted();
    void on_bb_ok_cancel_rejected();
    void on_cb_config_currentTextChanged(const QString &arg1);

    void generateContabilidad();
    float getTotalIncome(QString table, int iva, int trimForYearConfig);
    void updateLock();
    void writeHtml(QString filename, QString html);

private:
    Ui::Contabilidad *ui;
    QSqlDatabase db;

    // Current accounting mode, read from the combobox index (not its text).
    ConfigMode currentMode() const;

    // All money figures of one accounting period (a quarter, a month, or - when
    // accumulated across the four quarters - a full year). Computed once per
    // period so the ingresos/gastos tables and the summary share the same numbers.
    struct PeriodFigures {
        double ingImporte = 0.0, ingBase = 0.0, ingIva = 0.0;
        double gas10Importe = 0.0, gas10Base = 0.0, gas10Iva = 0.0;
        double gas21Importe = 0.0, gas21Base = 0.0, gas21Iva = 0.0;
        double gasNiImporte = 0.0;                 // gastos without IVA (base == importe)
        int ingTickets = 0, gasFacturas = 0;       // operation counts

        double gastosImporteTotal() const { return gas10Importe + gas21Importe + gasNiImporte; }
        double gastosBaseTotal()    const { return gas10Base + gas21Base + gasNiImporte; }
        double gastosIvaTotal()     const { return gas10Iva + gas21Iva; }
        double resultadoIva()       const { return ingIva - gastosIvaTotal(); }   // VAT to settle (modelo 303)
        double resultadoPeriodo()   const { return ingBase - gastosBaseTotal(); } // taxable result

        void accumulate(const PeriodFigures &o) {
            ingImporte += o.ingImporte; ingBase += o.ingBase; ingIva += o.ingIva;
            gas10Importe += o.gas10Importe; gas10Base += o.gas10Base; gas10Iva += o.gas10Iva;
            gas21Importe += o.gas21Importe; gas21Base += o.gas21Base; gas21Iva += o.gas21Iva;
            gasNiImporte += o.gasNiImporte;
            ingTickets += o.ingTickets; gasFacturas += o.gasFacturas;
        }
    };

    void periodRange(int trimForYearConfig, QDate &start, QDate &endExclusive);
    QString periodSubtitle(int trimForYearConfig);
    PeriodFigures computeFigures(int trimForYearConfig);
    QString renderSection(const PeriodFigures &f, const QString &summaryHeading);
    QString createHtmlTableIngresos(const PeriodFigures &f);
    QString createHtmlTableGastos(const PeriodFigures &f);
    QString createHtmlSummary(const PeriodFigures &f, const QString &heading);
};

#endif // CONTABILIDAD_H
