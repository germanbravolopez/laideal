#ifndef INGRESOS_H
#define INGRESOS_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>

#define TICKET_COLUMN_IDX 0

namespace Ui {
class Ingresos;
}

class Ingresos : public QMainWindow
{
    Q_OBJECT

public:
    QSqlTableModel *model;
    explicit Ingresos(QWidget *parent = nullptr);
    ~Ingresos();

private slots:
    void populate_table();
    void on_actionActualizar_triggered();
    void on_actionActivar_modo_edicion_triggered();
    void on_actionDesactivar_modo_edicion_triggered();

private:
    Ui::Ingresos *ui;
};

#endif // INGRESOS_H
