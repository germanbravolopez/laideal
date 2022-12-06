#ifndef RECOGPRENDAS_H
#define RECOGPRENDAS_H

#include <QMainWindow>
#include <QMessageBox>
#include <QSqlQueryModel>
#include <QTableWidget>
#include <QStandardItemModel>

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
    QSqlDatabase db;
    QSqlQueryModel *sql_query_model = new QSqlQueryModel;
    //QTableWidget table_widget_model;
    //QModelIndex model_index_clicked;
    explicit RecogPrendas(QWidget *parent = nullptr);
    ~RecogPrendas();

private slots:
    void initial_settings();
    void reset_all_contents();

    void on_le_search_returnPressed();
    void on_pb_search_clicked();
    void on_pb_reset_clicked();
    void on_pb_payment_toggled(bool checked);
    void on_pb_state_toggled(bool checked);
    void on_tableView_doubleClicked(const QModelIndex &index);

    void on_pb_save_clicked();
/*
    void setup_model_table(QAbstractItemModel *model);
    void check_new_search_may_proceed();
    bool compare_models();
    void save_model_changes();
*/
private:
    Ui::RecogPrendas *ui;
};

#endif // RECOGPRENDAS_H
