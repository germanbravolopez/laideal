#ifndef RECOGPRENDAS_H
#define RECOGPRENDAS_H

#include <QMainWindow>
#include <QMessageBox>
#include <QSqlQueryModel>

#define NOMBRE_COLUMN_IDX 0

namespace Ui {
class RecogPrendas;
}

class RecogPrendas : public QMainWindow
{
    Q_OBJECT

public:
    QSqlDatabase db;
    explicit RecogPrendas(QWidget *parent = nullptr);
    ~RecogPrendas();

private slots:
    void on_pb_search_clicked();

private:
    Ui::RecogPrendas *ui;
};

#endif // RECOGPRENDAS_H
