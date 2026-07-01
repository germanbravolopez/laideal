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

// Void unpaid, not-yet-delivered garments of a ticket (an erroneous receipt or a
// customer change of mind). This is the local counterpart to CancelInvoiceDialog:
// the rows here were never sent to AEAT (pagado=NO, verifactu_estado PENDIENTE),
// so voiding is a pure DB update - estado -> "Anulado", verifactu_estado ->
// "ANULADA" - with no AEAT anulacion. Paid/ENVIADA rows are shown but not
// selectable; those must be cancelled via "Anular Factura Verifactu".
class VoidGarmentsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VoidGarmentsDialog(const QSqlDatabase &database, QWidget *parent = nullptr);

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
    QLabel       *m_lblResult;
    QPushButton  *m_btnVoid;

    QString           m_loadedTicket;
    QVector<Garment>  m_garments;
    QSqlDatabase      db;

    void buildUi();
    void rebuildTable();
};

#endif // VOIDGARMENTSDIALOG_H
