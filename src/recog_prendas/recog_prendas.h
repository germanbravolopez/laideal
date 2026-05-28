#ifndef RECOGPRENDAS_H
#define RECOGPRENDAS_H

#include <QMainWindow>
#include <QDate>
#include <QMessageBox>
#include <QSqlQueryModel>
#include <QHash>

#include "mysortfilterproxymodel.h"
#include "sql_lite.h"

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
    explicit RecogPrendas(QWidget *parent = nullptr);
    ~RecogPrendas();

    QSqlDatabase db;
    VerifactuIntegration *m_verifactuIntegration = nullptr;
    QSqlQueryModel *sqlQueryModel = new QSqlQueryModel;
    MySortFilterProxyModel *proxyModel;
    bool isCellClicked = false;
    int rowClickedCell, columnClickedCell;

    enum UpdateDBop {
        PAY_YES,
        PAY_NO,
        PKU_YES,
        PKU_NO,
        OBSV,
        SIZE_AND_PRICE,
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
    void on_pb_pay_all_clicked();
    void on_pb_pku_all_clicked();
    void on_pb_print_clicked();
    void on_pb_separ_garm_clicked();
    void on_pb_verifactu_clicked();
    void retryVerifactuSubmit(const QString &ticketNum, const QDate &invoiceDate);
    void onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result);

private:
    Ui::RecogPrendas *ui;
    // Async submit tracking: reqId -> ticket number. Also used to dedup the pay-all
    // loop so multiple garments of the same ticket only fire one AEAT submission.
    QHash<QString, QString> m_pendingSubmits;

    void ensureVerifactuConnected();
    bool hasPendingSubmit(const QString &ticketNum) const;
    void printFactura(const QString &ticketNum, bool askSecondCopy);
};

#endif // RECOGPRENDAS_H
