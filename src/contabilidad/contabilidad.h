#ifndef CONTABILIDAD_H
#define CONTABILIDAD_H

#include <QDialog>
#include <QSqlDatabase>
#include <QDesktopServices>
#include <QDir>
#include <QDate>
#include <QSet>
#include <QVector>

#include "sql_lite.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
namespace UiKit { class ResultPanel; }

// Generar / Revertir contabilidad (built in code, no .ui): period and options,
// the actions, and a result panel that reports every outcome in the window.

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

    // Pure period-range math (no UI state), exposed for unit testing. For
    // Mensual, 'unit' is the month (1-12); for Trimestral/Anual it is the
    // quarter (1-4). endExclusive is the first day AFTER the period, i.e. the
    // range is the half-open [start, endExclusive).
    static void periodRangeFor(ConfigMode mode, int unit, int year, QDate &start, QDate &endExclusive);

    // Closing (locking) the books is a quarterly action, and never offered while
    // reverting: the "Bloquear datos" checkbox is enabled only for Trimestral.
    static bool lockOptionAvailable(ConfigMode mode, bool reverting);

    // Merges the lock state of ingresos and gastos for one period (each 0 = open,
    // 1 = locked, 2 = no rows): 2 only when both are empty, 1 if either is locked,
    // else 0. A quarter with only gastos can then be closed and reverted too.
    static int combinedLockState(int ingresosLock, int gastosLock);

    // Detail tables appended to the report so each summary figure can be audited.
    // Pure HTML rendering (no DB / UI state), exposed for unit testing; the rows come
    // from incomeTicketsBetweenDates / expensesBetweenDates and the total row equals
    // the matching summary figure.
    static QString createHtmlDetailIngresos(const QVector<IncomeTicketDetail> &tickets, double ivaRate);
    static QString createHtmlDetailGastos(const QVector<ExpenseDetail> &expenses);
    // True for the gastos IVA rates the summary sums (sin IVA, 10 %, 21 %). Rows
    // with any other rate are flagged in the detail and kept out of its total.
    static bool expenseIvaIsSummarised(int iva);

    // Report file under contabilidadPath(): "/contabilidad_trimestral_<year>_<q>.pdf",
    // "/Mensual/reporte_mensual_<year>_<m>.pdf" or "/Anual/reporte_anual_<year>.pdf";
    // the version with the detail tables gets "_detalle" before ".pdf", so both are kept.
    static QString reportRelativePath(ConfigMode mode, int unit, int year, bool withDetail);

    // "Comprobar bloqueo": whether the period is closed (locked by a quarterly
    // contabilidad). A month reads its quarter; a year lists its four quarters.
    static QString lockStatusMessage(QSqlDatabase &db, ConfigMode mode, int unit, int year);


    // All money figures of one accounting period (a quarter, a month, or - when
    // accumulated across the four quarters - a full year). Computed once per
    // period so the ingresos/gastos tables and the summary share the same numbers.
    // Public so the pure derivation below can be unit-tested.
    struct PeriodFigures {
        double ingImporte = 0.0, ingBase = 0.0, ingIva = 0.0;   // net of ingRegularizacion
        double ingRegularizacion = 0.0;            // earlier closed periods' cancellations / rectifications (IVA incl.)
        double gas10Importe = 0.0, gas10Base = 0.0, gas10Iva = 0.0;
        double gas21Importe = 0.0, gas21Base = 0.0, gas21Iva = 0.0;
        double gasNiImporte = 0.0;                 // gastos without IVA (base == importe)
        int ingTickets = 0, gasFacturas = 0;       // operation counts

        // Gastos with IVA (10 % + 21 %), the subtotal the gastos table shows before sin IVA.
        double gastosConIvaImporte() const { return gas10Importe + gas21Importe; }
        double gastosConIvaBase()    const { return gas10Base + gas21Base; }
        double gastosImporteTotal() const { return gas10Importe + gas21Importe + gasNiImporte; }
        double gastosBaseTotal()    const { return gas10Base + gas21Base + gasNiImporte; }
        double gastosIvaTotal()     const { return gas10Iva + gas21Iva; }
        double resultadoIva()       const { return ingIva - gastosIvaTotal(); }   // VAT to settle (modelo 303)
        double resultadoPeriodo()   const { return ingBase - gastosBaseTotal(); } // taxable result

        void accumulate(const PeriodFigures &o) {
            ingImporte += o.ingImporte; ingBase += o.ingBase; ingIva += o.ingIva;
            ingRegularizacion += o.ingRegularizacion;
            gas10Importe += o.gas10Importe; gas10Base += o.gas10Base; gas10Iva += o.gas10Iva;
            gas21Importe += o.gas21Importe; gas21Base += o.gas21Base; gas21Iva += o.gas21Iva;
            gasNiImporte += o.gasNiImporte;
            ingTickets += o.ingTickets; gasFacturas += o.gasFacturas;
        }
    };

    // Every report figure comes from the same detail rows the report lists, so a
    // summary can never disagree with its detail tables. Sums the valid amounts
    // (comma-decimal rows are flagged, not summed), subtracts the regularisations
    // of earlier closed periods from income, buckets gastos by rate (10 / 21 /
    // sin IVA; other or NULL rates are only counted), then applies
    // figuresFromTotals' IVA base/cuota math.
    // [periodStart, periodEnd) is the report period, used by netTicketCount.
    static PeriodFigures figuresFromDetails(const QVector<IncomeTicketDetail> &income,
                                            const QVector<RegularizationDetail> &regularizations,
                                            const QVector<ExpenseDetail> &expenses,
                                            double ivaRate,
                                            const QDate &periodStart, const QDate &periodEnd);
    // Gastos summary table: IVA 21 %, IVA 10 %, their subtotal, sin IVA, total.
    static QString createHtmlTableGastos(const PeriodFigures &f);
    // Number of tickets the period [periodStart, periodEnd) counts. A regularisation
    // offsets a ticket's income only when the cancelled payment was itself made in
    // this period (paid and cancelled in the same period -> not counted). A payment
    // from an earlier period being cancelled here does not cancel out a new sale of
    // the same ticket in this period (paid Q1, cancelled Q2, rest charged Q2 -> the
    // ticket counts in Q1 and in Q2).
    static int netTicketCount(const QVector<IncomeTicketDetail> &income,
                              const QVector<RegularizationDetail> &regularizations,
                              const QDate &periodStart, const QDate &periodEnd);
    // The same over the whole calendar year: a ticket paid across two quarters is
    // one ticket, and one paid and cancelled within the year is none.
    static int yearTicketCount(const QuarterlyDetails &details, int year);
    // Comma-decimal amounts in a period's rows (listed but not summed).
    static int invalidAmountCount(const QVector<IncomeTicketDetail> &income,
                                  const QVector<RegularizationDetail> &regularizations,
                                  const QVector<ExpenseDetail> &expenses);
    // Detail table of the period's regularisations (empty string when there are none).
    static QString createHtmlDetailRegularizaciones(const QVector<RegularizationDetail> &regularizations,
                                                    double ivaRate);

private slots:
    void onGenerateClicked();
    void onCheckLockClicked();
    void onConfigChanged();

private:
    void buildUi();
    void initialSettings();
    // Writes the selected report; returns its file (empty if it could not be written). invalidAmounts receives the
    // comma-decimal amounts left out of the sums.
    QString generateContabilidad(int &invalidAmounts);
    void updateLock();

    QSqlDatabase db;
    QLabel      *m_lblIntro = nullptr;
    QComboBox   *m_cbConfig = nullptr;
    QSpinBox    *m_sbYear = nullptr;
    QLabel      *m_lblPeriod = nullptr;
    QSpinBox    *m_sbPeriod = nullptr;
    QCheckBox   *m_chkLock = nullptr;
    QCheckBox   *m_chkDetail = nullptr;
    QPushButton *m_btnCheckLock = nullptr;
    QPushButton *m_btnGenerate = nullptr;
    UiKit::ResultPanel *m_lblResult = nullptr;

    // Current accounting mode, read from the combobox index (not its text).
    ConfigMode currentMode() const;


    void periodRange(int trimForYearConfig, QDate &start, QDate &endExclusive);
    QString periodSubtitle(int trimForYearConfig);
    // Pure IVA base/cuota derivation from a period's raw totals (used by
    // figuresFromDetails).
    static PeriodFigures figuresFromTotals(double ingImporte, int ingTickets,
                                           double gas10Importe, double gas21Importe,
                                           double gasNiImporte, int gasFacturas,
                                           double ivaRate);
    QString renderSection(const PeriodFigures &f, const QString &summaryHeading);
    static QString renderDetailTables(const QString &heading,
                                      const QVector<IncomeTicketDetail> &income,
                                      const QVector<RegularizationDetail> &regularizations,
                                      const QVector<ExpenseDetail> &expenses,
                                      double ivaRate);
    QString createHtmlTableIngresos(const PeriodFigures &f);
    QString createHtmlSummary(const PeriodFigures &f, const QString &heading);
};

#endif // CONTABILIDAD_H
