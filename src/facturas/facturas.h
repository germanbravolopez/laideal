#ifndef FACTURAS_H
#define FACTURAS_H

#include <QMainWindow>
#include <QMessageBox>
#include <QSqlQueryModel>
#include <QDate>

namespace Ui {
class Facturas;
}

class Facturas : public QMainWindow
{
    Q_OBJECT

public:
    QSqlDatabase db;
    QSqlQueryModel *sql_query_model = new QSqlQueryModel;
    explicit Facturas(QWidget *parent = nullptr);
    ~Facturas();

private slots:
    void initial_settings();
    void reset_all_contents();

private:
    Ui::Facturas *ui;
};

#endif // FACTURAS_H
