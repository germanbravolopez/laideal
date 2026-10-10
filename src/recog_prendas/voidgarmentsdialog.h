#ifndef VOIDGARMENTSDIALOG_H
#define VOIDGARMENTSDIALOG_H

#include <QDialog>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

class QLineEdit;
class QLabel;
class QTableWidget;
class QPushButton;
namespace UiKit { class ResultPanel; }

// Void unpaid, not-yet-delivered garments of a ticket (an erroneous receipt or a
// customer change of mind). This is the local counterpart to CancelInvoiceDialog:
// the rows here were never sent to AEAT (pagado=NO, verifactu_estado SIN COBRAR),
// so voiding is a pure DB update - estado -> "Anulado", verifactu_estado ->
// "ANULADA" - with no AEAT anulacion. Paid/ENVIADA rows are shown but not
// selectable; those must be cancelled via "Anular Factura Verifactu". Opened from
// Recogida de prendas (Anular prendas…) with the selected ticket loaded. Built in
// code with the shared UiKit style.
class VoidGarmentsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VoidGarmentsDialog(const QSqlDatabase &database, QWidget *parent = nullptr);
    // Types the ticket number and searches it, as the operator would.
    void loadTicket(const QString &ticketNum);
    // Garments voided while the dialog was open.
    int voidedCount() const { return m_voidedTotal; }

private slots:
    void onSearchClicked();
    void onVoidSelectedClicked();

private:
    struct Garment {
        QString hash;
        QString prenda;
        QString cantidad;
        QString importe;
        QString pagado;
        QString verifactuEstado;
        QString estado;
        bool    voidable = false;
    };

    QLineEdit    *m_leTicketNum;
    QLabel       *m_lblHeader;
    QTableWidget *m_table;
    UiKit::ResultPanel *m_lblResult;
    QPushButton  *m_btnVoid;

    QString           m_loadedTicket;
    QVector<Garment>  m_garments;
    QSqlDatabase      db;
    int               m_voidedTotal = 0;

    void buildUi();
    void rebuildTable();
};

#endif // VOIDGARMENTSDIALOG_H
