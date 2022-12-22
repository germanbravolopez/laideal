#ifndef LISTACLIENTES_H
#define LISTACLIENTES_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>
#include <QMessageBox>

#define NOMBRE_COLUMN_IDX 0

namespace Ui {
class ListaClientes;
}

class ListaClientes : public QMainWindow
{
    Q_OBJECT

public:
    QSqlTableModel *model;
    explicit ListaClientes(QWidget *parent = nullptr);
    ~ListaClientes();

private slots:
    void populate_table();
    void on_actionActualizar_triggered();
    void on_actionAnadir_fila_triggered();
    void on_actionEliminar_fila_triggered();

private:
    Ui::ListaClientes *ui;
};

#endif // LISTACLIENTES_H
