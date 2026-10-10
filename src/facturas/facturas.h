#ifndef FACTURAS_H
#define FACTURAS_H

#include <QDialog>
#include <QSqlDatabase>

class QComboBox;
class QDateEdit;
class QLineEdit;
class QPushButton;
namespace UiKit { class ResultPanel; }

// Herramientas -> Formulario facturas: records one expense invoice (gastos) per
// save, IVA included; base and cuota are derived as the amount is typed. Built in
// code with the shared UiKit style; every outcome is shown in the result panel.
class Facturas : public QDialog
{
    Q_OBJECT

public:
    explicit Facturas(const QSqlDatabase &database, QWidget *parent = nullptr);
    void populateEmpresas();
    void populateServicios();

    // Pure IVA split of a gross amount (importe IVA incluido) at the given rate
    // (percentage, e.g. 21): base imponible = gross / (1 + rate/100); cuota de
    // IVA = gross - base. Exposed as statics for unit testing.
    static double taxBaseFromGross(double gross, double ivaRate);
    static double taxAmountFromGross(double gross, double ivaRate);

private slots:
    void onSaveClicked();
    void resetAllContents();
    void updateTaxSplit();

private:
    void buildUi();
    // Empty when the form can be saved, else the reason (rich text for the panel).
    QString validationError();
    bool saveFactura();

    QSqlDatabase db;
    QLineEdit   *m_leFra = nullptr;
    QDateEdit   *m_deFecha = nullptr;
    QComboBox   *m_cbEmpresa = nullptr;
    QComboBox   *m_cbServicio = nullptr;
    QLineEdit   *m_leDescripcion = nullptr;
    QComboBox   *m_cbIva = nullptr;
    QLineEdit   *m_leImporte = nullptr;
    QLineEdit   *m_leBase = nullptr;
    QLineEdit   *m_leIva = nullptr;
    UiKit::ResultPanel *m_lblResult = nullptr;
};

#endif // FACTURAS_H
