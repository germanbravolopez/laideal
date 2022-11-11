#ifndef LISTAPROVEEDORES_H
#define LISTAPROVEEDORES_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>

namespace Ui {
class ListaProveedores;
}

class ListaProveedores : public QMainWindow
{
    Q_OBJECT

public:
    explicit ListaProveedores(QWidget *parent = nullptr);
    ~ListaProveedores();

private slots:
    void populate_table();
    void on_actionActualizar_triggered();

private:
    Ui::ListaProveedores *ui;
};

#endif // LISTAPROVEEDORES_H
