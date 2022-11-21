#ifndef LISTAPROVEEDORES_H
#define LISTAPROVEEDORES_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>

#define NOMBRE_COLUMN_IDX 0

namespace Ui {
class ListaProveedores;
}

class ListaProveedores : public QMainWindow
{
    Q_OBJECT

public:
    QSqlDatabase db;
    explicit ListaProveedores(QWidget *parent = nullptr);
    ~ListaProveedores();

private slots:
    void populate_table();
    void on_actionActualizar_triggered();

    void on_actionAnadir_proveedor_triggered();

private:
    Ui::ListaProveedores *ui;
};

#endif // LISTAPROVEEDORES_H
