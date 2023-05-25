#ifndef GASTOS_H
#define GASTOS_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>
#include <QMessageBox>

#include "mysortfilterproxymodel.h"

namespace Ui {
class Gastos;
}

class Gastos : public QMainWindow
{
    Q_OBJECT

public:
    QSqlTableModel *model;
    MySortFilterProxyModel *proxyModel;
    explicit Gastos(QWidget *parent = nullptr);
    ~Gastos();

private slots:
    void populate_table();
    void on_actionActualizar_triggered();
    void on_actionActivar_modo_edicion_triggered();
    void on_actionDesactivar_modo_edicion_triggered();
    void on_actionAnadir_fila_triggered();
    void on_actionEliminar_fila_triggered();

private:
    Ui::Gastos *ui;
};

#endif // GASTOS_H
