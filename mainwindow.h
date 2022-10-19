#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "qabstractbutton.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDebug>

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
    void set_next_ticket_number();
    void populate_cb_client();
    void set_service_to_cb();
    void set_garment_to_cb_and_populate();
    void set_garment_price(int garment_row, std::string garment_text, QString service_text);

    void on_pb_payment_toggled(bool checked);
    void on_bb_save_reset_clicked(QAbstractButton *button);
    void on_cb_client_editTextChanged(const QString &arg1);
    void on_table_ticket_cellChanged(int row, int column);

    void textChanged_0(const QString &text);
    void textChanged_1(const QString &text);
    void textChanged_2(const QString &text);
    void textChanged_3(const QString &text);
    void textChanged_4(const QString &text);
    void textChanged_5(const QString &text);
    void textChanged_6(const QString &text);
    void textChanged_7(const QString &text);
    void textChanged_8(const QString &text);
    void textChanged_9(const QString &text);

private:
    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H
