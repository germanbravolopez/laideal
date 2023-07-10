#ifndef LISTASERVICIOS_H
#define LISTASERVICIOS_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>
#include <QMessageBox>

#define NOMBRE_COLUMN_IDX 0

namespace Ui {
class ListaServicios;
}

class ListaServicios : public QMainWindow
{
    Q_OBJECT

public:
    explicit ListaServicios(QWidget *parent = nullptr);
    ~ListaServicios();
    QSqlDatabase db;
    QSqlTableModel *model;

private slots:
    void populate_table();
    void resize_window_to_table();
    void on_actionActualizar_triggered();
    void on_actionAnadir_fila_triggered();
    void on_actionEliminar_fila_triggered();

private:
    Ui::ListaServicios *ui;

};

#endif // LISTASERVICIOS_H
