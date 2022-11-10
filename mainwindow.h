#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "qabstractbutton.h"
#include <QSqlDatabase>
#include <QDebug>
#include <QMessageBox>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    QSqlDatabase db;
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
    void save_ticket();

    // Taskbar
    void on_actionCerrar_triggered();

private:
    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H
