#ifndef INGRESOS_H
#define INGRESOS_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>

namespace Ui {
class Ingresos;
}

class Ingresos : public QMainWindow
{
    Q_OBJECT

public:
    explicit Ingresos(QWidget *parent = nullptr);
    ~Ingresos();

private slots:
    void populate_table();
    void on_actionActualizar_triggered();

private:
    Ui::Ingresos *ui;
};

#endif // INGRESOS_H
