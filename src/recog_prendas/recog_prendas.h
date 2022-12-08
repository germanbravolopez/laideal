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

namespace Ui {
class RecogPrendas;
}

class RecogPrendas : public QMainWindow
{
    Q_OBJECT

public:
    QSqlDatabase db;
    bool is_cell_clicked = false, model_updated = false;
    int row_clicked_cell, column_clicked_cell;
    explicit RecogPrendas(QWidget *parent = nullptr);
    ~RecogPrendas();

private slots:
    void initial_settings();
    void reset_all_contents();
    void clear_table_remove_rows();
    void setup_model_table(QAbstractItemModel *model);
    void check_new_search_may_proceed();

    void on_le_search_returnPressed();
    void on_pb_search_clicked();
    void on_pb_reset_clicked();
    void on_pb_payment_toggled(bool checked);
    void on_pb_state_toggled(bool checked);
    void on_table_cellDoubleClicked(int row, int column);
    void on_pb_save_clicked();
    void on_le_obsv_editingFinished();

private:
    Ui::RecogPrendas *ui;
};

#endif // RECOGPRENDAS_H
