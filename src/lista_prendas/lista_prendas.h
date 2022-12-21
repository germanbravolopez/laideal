#ifndef LISTAPRENDAS_H
#define LISTAPRENDAS_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>
#include <QMessageBox>

#define NOMBRE_COLUMN_IDX 0

namespace Ui {
class ListaPrendas;
}

class ListaPrendas : public QMainWindow
{
    Q_OBJECT

public:
    QSqlDatabase db;
    QSqlTableModel *model;
    explicit ListaPrendas(QWidget *parent = nullptr);
    ~ListaPrendas();

private slots:
    void populate_table();
    void on_actionActualizar_triggered();
    void on_actionAnadir_fila_triggered();
    void on_actionEliminar_fila_triggered();

private:
    Ui::ListaPrendas *ui;
};

#endif // LISTAPRENDAS_H
