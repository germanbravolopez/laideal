#ifndef FACTURAS_H
#define FACTURAS_H

#include <QMainWindow>
#include <QMessageBox>
#include <QSqlQueryModel>
#include <QDate>
#include <QPushButton>

namespace Ui {
class Facturas;
}

class Facturas : public QMainWindow
{
    Q_OBJECT

public:
    explicit Facturas(QWidget *parent = nullptr);
    ~Facturas();
    QSqlDatabase db;
    void populate_empresas();

private slots:
    void initial_settings();
    void reset_all_contents();
    void save_factura();

    void on_buttonBox_clicked(QAbstractButton *button);

private:
    Ui::Facturas *ui;
};

#endif // FACTURAS_H
