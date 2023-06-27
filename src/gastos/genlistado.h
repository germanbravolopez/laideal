#ifndef GENLISTADO_H
#define GENLISTADO_H

#include <QDialog>
#include <QDir>
#include <QDesktopServices>
#include <QAbstractItemModel>
#include <QSqlDatabase>
#include "mysortfilterproxymodel.h"

namespace Ui {
class GenListado;
}

class GenListado : public QDialog
{
    Q_OBJECT

public:
    explicit GenListado(QWidget *parent = nullptr);
    ~GenListado();
    QSqlDatabase db;
    QAbstractItemModel *model;

private slots:
    void initial_settings();
    void set_cb_fechas();
    void write_html(QString filename, QString html);
    QString generate_html_table();

    void on_bb_ok_cancel_accepted();
    void on_bb_ok_cancel_rejected();
    void on_checkb_allys_clicked(bool checked);

private:
    Ui::GenListado *ui;
};

#endif // GENLISTADO_H
