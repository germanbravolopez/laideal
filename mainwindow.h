#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "qabstractbutton.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void initial_settings();
    void set_service_to_cb();
    void reset_all_contents();
    void on_pb_payment_toggled(bool checked);
    void on_bb_save_reset_clicked(QAbstractButton *button);

private:
    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H
