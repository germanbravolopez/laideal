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
    QSqlDatabase db;
    explicit Facturas(QWidget *parent = nullptr);
    ~Facturas();

private slots:
    void initial_settings();
    void reset_all_contents();
    void on_buttonBox_clicked(QAbstractButton *button);

    void save_factura();

private:
    Ui::Facturas *ui;
};

#endif // FACTURAS_H
