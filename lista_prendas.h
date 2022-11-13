#ifndef LISTAPRENDAS_H
#define LISTAPRENDAS_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>

namespace Ui {
class ListaPrendas;
}

class ListaPrendas : public QMainWindow
{
    Q_OBJECT

public:
    QSqlDatabase db;
    explicit ListaPrendas(QWidget *parent = nullptr);
    ~ListaPrendas();

private slots:
    void populate_table();
    void on_actionActualizar_triggered();
    void on_actionAnadir_prenda_triggered();

private:
    Ui::ListaPrendas *ui;
};

#endif // LISTAPRENDAS_H
