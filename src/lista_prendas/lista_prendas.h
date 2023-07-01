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
    explicit ListaPrendas(QWidget *parent = nullptr);
    ~ListaPrendas();
    QSqlDatabase db;
    QSqlTableModel *model;

private slots:
    void populate_table();
    void on_actionActualizar_triggered();
    void on_actionAnadir_fila_triggered();
    void on_actionEliminar_fila_triggered();

    void closeEvent(QCloseEvent* event);

private:
    Ui::ListaPrendas *ui;

signals:
    void populate_prendas();
};

#endif // LISTAPRENDAS_H
