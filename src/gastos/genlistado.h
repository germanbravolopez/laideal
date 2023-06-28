#ifndef GENLISTADO_H
#define GENLISTADO_H

#include <QDialog>
#include <QDir>
#include <QDesktopServices>
#include <QAbstractItemModel>
#include <QSqlDatabase>
#include "mysortfilterproxymodel.h"

#define C_FECHAS      "Fechas"
#define C_PROVEEDORES "Proveedores"
#define C_INCL_TODOS  "Incluir todos"
#define C_CONTAB_CERR "Contabilidad cerrada"

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
    QString generate_table_with_specific_conditions();
    QString generate_html_table();
    bool check_years_invoice_type_for_row(int row);

    void on_bb_ok_cancel_accepted();
    void on_bb_ok_cancel_rejected();
    void on_checkb_allys_clicked(bool checked);

private:
    Ui::GenListado *ui;
};

#endif // GENLISTADO_H
