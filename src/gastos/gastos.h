#ifndef GASTOS_H
#define GASTOS_H

#include <QMainWindow>
#include <QSqlTableModel>
#include <QMessageBox>
#include <QSqlDatabase>

#include "mysortfilterproxymodel.h"

namespace Ui {
class Gastos;
}

class Gastos : public QMainWindow
{
    Q_OBJECT

public:
    explicit Gastos(QWidget *parent = nullptr);
    ~Gastos();
    QSqlDatabase db;
    QSqlTableModel *model;
    MySortFilterProxyModel *proxyModel;

private slots:
    void populate_table();
    void resize_window_to_table();
    void on_actionActualizar_triggered();
    void on_actionActivar_modo_edicion_triggered();
    void on_actionDesactivar_modo_edicion_triggered();
    void on_actionAnadir_fila_triggered();
    void on_actionEliminar_fila_triggered();
    void on_actionGenerar_pdf_con_el_listado_triggered();

private:
    Ui::Gastos *ui;
};

#endif // GASTOS_H
