#ifndef RECOGPRENDAS_H
#define RECOGPRENDAS_H

#include <QMainWindow>
#include <QDate>
#include <QMessageBox>
#include <QSqlQueryModel>
#include <QHash>

#include "mysortfilterproxymodel.h"
#include "sql_lite.h"

// Defined in verifactutypes.h; only ever passed by const reference here, so the
// forward declaration keeps QPixmap out of this header (same as VerifactuResult,
// which sql_lite.h forward-declares).
struct VerifactuRemoteRecord;

class VerifactuIntegration;
struct VerifactuResult;

// `ingresos` column indices come from sql_lite.h (INGRESOS_COL_*).

namespace Ui {
class RecogPrendas;
}

class RecogPrendas : public QMainWindow
{
    Q_OBJECT

public:
    explicit RecogPrendas(const QSqlDatabase &database, QWidget *parent = nullptr);
    ~RecogPrendas();

    VerifactuIntegration *m_verifactuIntegration = nullptr;
    QSqlQueryModel *sqlQueryModel = new QSqlQueryModel;
    MySortFilterProxyModel *proxyModel = nullptr;
    bool isCellClicked = false;
    int rowClickedCell, columnClickedCell;

    enum UpdateDBop {
        PAY_YES,
        PAY_NO,
        PKU_YES,
        PKU_NO,
        OBSV,
        SIZE_AND_PRICE,
        QTY,
        SERVICE,
        PRICE,
        SEPARATE_GARM
    };

private slots:
    void initialSettings();
    void resetAllContents();
    void updateDb(UpdateDBop op, int nGarm = 0);
    void updateRowClickedToFields();
    float calculatePrice();

    void on_le_search_returnPressed();
    void on_cb_search_date_currentTextChanged(const QString &arg1);
    void on_pb_search_clicked();
    void on_pb_reset_clicked();
    void on_pb_payment_toggled(bool checked);
    void on_pb_state_toggled(bool checked);
    void on_tableView_clicked(const QModelIndex &index);
    void on_le_obsv_editingFinished();
    void on_le_size_editingFinished();
    void on_le_qty_editingFinished();
    void on_le_price_editingFinished();
    void on_cb_servic_activated(int index);
    void on_pb_pay_all_clicked();
    void on_pb_pku_all_clicked();
    void on_pb_print_clicked();
    void on_pb_separ_garm_clicked();
    void on_pb_verifactu_clicked();
    // Re-submits ONE payment event: its own InvoiceID, total and fecha_pago are
    // read from the DB via sql_lite::verifactuEventFor(ticketNum, seq).
    void retryVerifactuSubmit(const QString &ticketNum, int seq);
    // Asks AEAT what it holds for this payment event and, only if the returned
    // record is provably the same invoice AND carries a CSV, offers to adopt it.
    // Read-only against AEAT; the DB write needs an explicit operator confirmation.
    void queryAeatAndOfferReconcile(const QString &ticketNum, int seq);
    // Shows AEAT's record beside the local one. Only offers the DB write when the
    // two provably match and AEAT returned a CSV; always exposes the raw payload,
    // since the query response schema is unpublished.
    void showAeatReconcileDialog(const QString &ticketNum, int seq, const QString &invoiceId,
                                 const PendingVerifactuEvent &ev,
                                 const VerifactuRemoteRecord &rec);
    void onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result);

private:
    Ui::RecogPrendas *ui;
    QSqlDatabase db;
    // Async submit tracking: reqId -> the payment event it belongs to. Also used
    // to dedup the pay-all loop so multiple garments of the same ticket only fire
    // one AEAT submission.
    struct PendingSubmit {
        QString ticketNum;
        int     seq = 0;
        // Adopted from PayDialog after its bounded wait expired: the operator has
        // already been handed a recibo without a QR, so a late success has to tell
        // them the factura is now printable.
        bool    adopted = false;
    };
    QHash<QString, PendingSubmit> m_pendingSubmits;

    void ensureVerifactuConnected();
    bool hasPendingSubmit(const QString &ticketNum) const;
    // invoiceSeq forwarded to Imprimir so the reprint loads only the rows of
    // the given payment event. -1 = legacy / all rows for the ticket.
    void printFactura(const QString &ticketNum, bool askSecondCopy, int invoiceSeq = -1);
    // sourceRow/sourceCol are sqlQueryModel coords, not proxy coords.
    void selectSourceRow(int sourceRow, int sourceCol);
};

#endif // RECOGPRENDAS_H
