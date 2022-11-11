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
    explicit ListaPrendas(QWidget *parent = nullptr);
    ~ListaPrendas();

private slots:
    void populate_table();
    void on_actionActualizar_triggered();

private:
    Ui::ListaPrendas *ui;
};

#endif // LISTAPRENDAS_H
