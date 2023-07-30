#ifndef INGRESOS_H
#define INGRESOS_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>

#include "mysortfilterproxymodel.h"

#define COLUMN_IDX_TICKET  0
#define COLUMN_IDX_IMPORTE 5
#define COLUMN_IDX_PAYED   6
#define COLUMN_IDX_STATE   7

namespace Ui {
class Ingresos;
}

class Ingresos : public QMainWindow
{
    Q_OBJECT

public:
    explicit Ingresos(QWidget *parent = nullptr);
    ~Ingresos();
    QSqlTableModel *model;
    MySortFilterProxyModel *proxyModel;

private slots:
    void populate_table();
    void resize_window_to_table();
    void on_actionActualizar_triggered();
    void on_actionActivar_modo_edicion_triggered();
    void on_actionDesactivar_modo_edicion_triggered();

private:
    Ui::Ingresos *ui;
};

#endif // INGRESOS_H
