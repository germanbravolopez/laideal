#ifndef FACTURAS_H
#define FACTURAS_H

#include <QDialog>
#include <QSqlDatabase>

class QComboBox;
class QDateEdit;
class QLineEdit;
class QPushButton;
namespace UiKit { class ResultPanel; }

// Contabilidad -> Añadir factura de gastos: records one expense invoice (gastos) per
// save, IVA included; base and cuota are derived as the amount is typed. Built in
// code with the shared UiKit style; every outcome is shown in the result panel.
class Facturas : public QDialog
{
    Q_OBJECT

public:
    explicit Facturas(const QSqlDatabase &database, QWidget *parent = nullptr);
    void populateEmpresas();
    void populateServicios();

    // Pure IVA amount of a gross amount (importe IVA incluido) at the given rate
    // (percentage, e.g. 21); the base is taxBaseFromGross (verifactutypes.h).
    // Exposed as a static for unit testing.
    static double taxAmountFromGross(double gross, double ivaRate);

    // Parses the typed importe (comma or point decimals). False for empty text,
    // signs, exponents, inf / nan, zero and anything over 999999999.
    static bool parseGross(const QString &text, double *value = nullptr);

private slots:
    void onSaveClicked();
    void resetAllContents();
    void updateTaxSplit();

private:
    void buildUi();
    // Empty when the form can be saved, else the reason (rich text for the panel).
    QString validationError();
    // "del dd-MM-yyyy por X €." of the stored invoice with the typed number and supplier, else empty.
    QString existingInvoiceSummary();
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
