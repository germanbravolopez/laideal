#ifndef LISTACLIENTES_H
#define LISTACLIENTES_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>

#define NOMBRE_COLUMN_IDX 0

namespace Ui {
class ListaClientes;
}

class ListaClientes : public QMainWindow
{
    Q_OBJECT

public:
    QSqlDatabase db;
    explicit ListaClientes(QWidget *parent = nullptr);
    ~ListaClientes();

private slots:
    void populate_table();
    void on_actionActualizar_triggered();
    void on_actionAnadir_cliente_triggered();

private:
    Ui::ListaClientes *ui;
};

#endif // LISTACLIENTES_H
