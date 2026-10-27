#ifndef ADDGARMENT_H
#define ADDGARMENT_H

#include <functional>
#include <QDate>
#include <QDialog>
#include <QSqlDatabase>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QGroupBox;
class QLineEdit;
class QSqlQueryModel;
namespace UiKit { class ResultPanel; }

// Tickets -> Añadir nuevas prendas: adds one garment to an existing receipt
// that has no paid garment yet (a paid receipt is an invoice AEAT holds). The
// receipt is searched first; the form only opens for it, and the save is refused
// if the number was retyped or the receipt was charged meanwhile. Built in code
// with the shared UiKit style; every outcome is shown in the result panel.
class AddGarment : public QDialog
{
    Q_OBJECT

public:
    explicit AddGarment(const QSqlDatabase &database, QWidget *parent = nullptr);
    // Types the receipt number and searches it, as the operator would.
    void loadTicket(const QString &ticketNum);
    bool ticketFound() const { return m_ticketFound; }
    // Why a paid garment cannot be invoiced now (Verifactu), or empty; checked at save.
    // Unset: paid garments are refused.
    std::function<QString()> invoicingBlocked;
    // The AEAT environment of the direct client, whose used numbers a new invoice skips; unset: all.
    std::function<QString()> aeatEnvironment;

signals:
    // A garment saved as paid: an invoice to submit to AEAT now (MainWindow does), as InvoiceID
    // "<ticketNum>-<seq>" (seq 0: "<ticketNum>"), the seq stored on its row.
    void paidGarmentSaved(const QString &ticketNum, const QDate &paymentDate, double amount, int seq);
    // Any garment saved (paid or not), e.g. for Recogida to refresh and report it.
    void garmentSaved(const QString &ticketNum);

private slots:
    void onSearchClicked();
    void onSaveClicked();
    void resetAllContents();
    void clearGarmentFields();
    void setGarmentPrice();

private:
    void buildUi();
    void populateGarments();
    // Empty when the garment can be saved, else the reason (rich text for the panel).
    QString validationError();
    bool saveGarment();

    QSqlDatabase db;
    QSqlQueryModel *m_ticketModel = nullptr;
    bool m_ticketFound = false;
    QString m_searchedTicket;   // the number the search checked; a save must use the same one

    QLineEdit *m_leNRecibo = nullptr;
    QGroupBox *m_grpTicket = nullptr;
    QGroupBox *m_grpGarment = nullptr;
    QGroupBox *m_grpState = nullptr;
    QLineEdit *m_leCliente = nullptr;
    QDateEdit *m_deFechaRecepcion = nullptr;
    QComboBox *m_cbPrenda = nullptr;
    QComboBox *m_cbServicio = nullptr;
    QLineEdit *m_leCantidad = nullptr;
    QLineEdit *m_leSize = nullptr;
    QLineEdit *m_leImporte = nullptr;
    QLineEdit *m_leObservaciones = nullptr;
    QCheckBox *m_chkPagado = nullptr;
    QDateEdit *m_deFechaPago = nullptr;
    QCheckBox *m_chkRecogido = nullptr;
    QDateEdit *m_deFechaRecogida = nullptr;
    UiKit::ResultPanel *m_lblResult = nullptr;
};

#endif // ADDGARMENT_H
