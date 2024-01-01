#ifndef RECOGPRENDAS_H
#define RECOGPRENDAS_H

#include <QMainWindow>
#include <QMessageBox>
#include <QSqlQueryModel>

#define TABLE_TICKET     0
#define TABLE_CLIENT     1
#define TABLE_DATE_RCP   2
#define TABLE_DATE_PAY   3
#define TABLE_DATE_PKU   4
#define TABLE_PRICE      5
#define TABLE_IS_PAYED   6
#define TABLE_STATE      7
#define TABLE_QUANTITY   8
#define TABLE_GARMENT    9
#define TABLE_SIZE      10
#define TABLE_SERVICE   11
#define TABLE_OBSERV    12
#define TABLE_EDIT_LOCK 13

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
    QSqlQueryModel *sql_query_model = new QSqlQueryModel;
    bool is_cell_clicked = false;
    int row_clicked_cell, column_clicked_cell;

    enum UpdateDBop {
        PAY_YES,
        PAY_NO,
        PKU_YES,
        PKU_NO,
        OBSV,
        SIZE_AND_PRICE
    };

private slots:
    void initial_settings();
    void reset_all_contents();
    void update_db(UpdateDBop op);
    void update_row_clicked_to_fields();
    void calculate_price();

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

private:
    Ui::RecogPrendas *ui;
};

#endif // RECOGPRENDAS_H
