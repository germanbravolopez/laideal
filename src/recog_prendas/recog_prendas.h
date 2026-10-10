#ifndef RECOGPRENDAS_H
#define RECOGPRENDAS_H

#include <QMainWindow>
#include <QDate>
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

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableView;
namespace UiKit { class ResultPanel; }

// Recogida de prendas: search tickets, inspect / edit one garment, and act on its
// ticket (Cobrar, Recoger todo, Separar, Imprimir, Verifactu). Built in code with
// the shared UiKit style; messages go to the result panel, AEAT replies (which
// arrive later) to the status bar.
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
    int rowClickedCell = -1;

    enum UpdateDBop {
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
    double calculatePrice();

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
    void showPickupBadge(bool pickedUp);
    void on_pb_void_clicked();
    void on_pb_add_clicked();
    void on_pb_verifactu_clicked();
    // Re-submits ONE payment event: its own InvoiceID, total and fecha_pago are
    // read from the DB via sql_lite::verifactuEventFor(ticketNum, seq).
    void retryVerifactuSubmit(const QString &ticketNum, int seq);
    // Asks AEAT what it holds for this payment event and, only if the returned
    // record is provably the same invoice AND carries a CSV, offers to adopt it.
    // Read-only against AEAT; the DB write needs an explicit operator confirmation.
    // localAlreadySettled: the row is ENVIADA/ANULADA/RECTIFICADA, so the query is
    // informative only and no adoption is offered.
    void queryAeatAndOfferReconcile(const QString &ticketNum, int seq,
                                    bool localAlreadySettled = false);
    // Shows AEAT's record beside the local one. Only offers the DB write when the
    // two provably match and AEAT returned a CSV; always exposes the raw payload,
    // since the query response schema is unpublished.
    void showAeatReconcileDialog(const QString &ticketNum, int seq, const QString &invoiceId,
                                 const PendingVerifactuEvent &ev,
                                 const VerifactuRemoteRecord &rec,
                                 bool localAlreadySettled);
    void onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result);

private:
    // The window's widgets, named as in the former .ui form (the on_<name>_<signal>
    // slots connect by name and the e2e bench finds them by it).
    struct Widgets {
        QLabel *lbl_title = nullptr;
        QLineEdit *le_search = nullptr;
        QComboBox *cb_search_date = nullptr;
        QPushButton *pb_search = nullptr, *pb_reset = nullptr;
        QTableView *tableView = nullptr;
        QLineEdit *le_nr_ticket = nullptr, *le_client = nullptr, *le_phone = nullptr, *le_mobile = nullptr;
        QLineEdit *le_garm = nullptr, *le_qty = nullptr, *le_size = nullptr, *le_price = nullptr, *le_obsv = nullptr;
        QComboBox *cb_servic = nullptr;
        QDateEdit *de_date_recep = nullptr, *de_date_paym = nullptr, *de_date_pickup = nullptr, *de_date_anul = nullptr;
        QCheckBox *pb_payment = nullptr;   // read-only: payment happens through Cobrar
        QCheckBox *pb_state = nullptr;     // Recogida
        QLabel *lbl_payment_badge = nullptr, *lbl_state_badge = nullptr, *lbl_anul_badge = nullptr;
        QLabel *lbl_total = nullptr;
        QPushButton *pb_pay_all = nullptr, *pb_pku_all = nullptr, *pb_separ_garm = nullptr;
        QPushButton *pb_print = nullptr, *pb_verifactu = nullptr, *pb_void = nullptr, *pb_add = nullptr;
        QSpinBox *sb_separ = nullptr;
    };
    void buildUi();
    // Shows a dd-MM-yyyy date, or "-" when it is empty.
    static void showOptionalDate(QDateEdit *edit, const QString &ddMMyyyy);

    Widgets *ui = nullptr;
    UiKit::ResultPanel *m_result = nullptr;
    bool m_lastWriteOk = true;   // the last updateDb() write was stored
    QSqlDatabase db;
    // Async submit tracking: reqId -> the payment event it belongs to.
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
    // invoiceSeq forwarded to Imprimir so the reprint loads only the rows of
    // the given payment event. -1 = legacy / all rows for the ticket. Returns
    // whether it reached the printer.
    bool printFactura(const QString &ticketNum, int invoiceSeq = -1);
    // sourceRow/sourceCol are sqlQueryModel coords, not proxy coords.
    void selectSourceRow(int sourceRow);
};

#endif // RECOGPRENDAS_H
