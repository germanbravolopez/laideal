#ifndef LISTACLIENTES_H
#define LISTACLIENTES_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>

namespace Ui {
class ListaClientes;
}

class ListaClientes : public QMainWindow
{
    Q_OBJECT

public:
    explicit ListaClientes(QWidget *parent = nullptr);
    ~ListaClientes();

private slots:
    void populate_table();
    void on_actionActualizar_triggered();

private:
    Ui::ListaClientes *ui;
};

#endif // LISTACLIENTES_H
