#ifndef GASTOS_H
#define GASTOS_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>

#define FECHA_COLUMN_IDX 4

namespace Ui {
class Gastos;
}

class Gastos : public QMainWindow
{
    Q_OBJECT

public:
    explicit Gastos(QWidget *parent = nullptr);
    ~Gastos();

private slots:
    void populate_table();
    void on_actionActualizar_triggered();

private:
    Ui::Gastos *ui;
};

#endif // GASTOS_H
