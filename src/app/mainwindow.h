#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QAbstractButton>
#include <QSqlDatabase>
#include <QDebug>
#include <QMessageBox>
#include <QStyleFactory>

#define TABLE_TICKET_QNTY   0
#define TABLE_TICKET_GARM   1
#define TABLE_TICKET_SIZE   2
#define TABLE_TICKET_SERV   3
#define TABLE_TICKET_OBSE   4
#define TABLE_TICKET_PRIC   5

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    QSqlDatabase db;
    int pb_added_rows = 0;
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void mainwindow_initial_settings();
    void reset_all_contents();

    // Mainwindow objects
    void set_next_ticket_number();
    void populate_cb_client();
    void resize_table();
    void set_service_to_cb(int initial_row);
    void set_garment_to_cb_and_populate(int initial_row);
    void set_garment_price(int garment_row, QString garment_text, QString service_text);

    void on_pb_payment_toggled(bool checked);
    void on_bb_save_reset_clicked(QAbstractButton *button);
    void on_cb_client_editTextChanged(const QString &arg1);
    void on_table_ticket_cellChanged(int row, int column);
    void on_pb_add_row_clicked();

    void cbGarmChanged(const QString &text);
    void cbServChanged(const QString &text);

    bool validate_ticket();
    QString remove_special_char(QString str);
    void check_client_data();
    void save_ticket();

    // Taskbar
    void on_actionCerrar_triggered();
    void on_actionIngresos_triggered();
    void on_actionGastos_triggered();
    void on_actionListado_de_prendas_triggered();
    void on_actionListado_de_clientes_triggered();
    void on_actionListado_de_proveedores_triggered();
    void on_actionRecogida_de_prendas_triggered();

private:
    Ui::MainWindow *ui;
};

#endif // MAINWINDOW_H
