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
    explicit ListaClientes(QWidget *parent = nullptr);
    ~ListaClientes();
    QSqlDatabase db;
    QSqlTableModel *model;

private slots:
    void populate_table();
    void on_actionActualizar_triggered();
    void on_actionAnadir_fila_triggered();
    void on_actionEliminar_fila_triggered();

    void closeEvent(QCloseEvent* event);

private:
    Ui::ListaClientes *ui;

signals:
    void populate_clientes();
};

#endif // LISTACLIENTES_H
