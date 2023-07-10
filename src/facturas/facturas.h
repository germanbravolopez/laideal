#ifndef FACTURAS_H
#define FACTURAS_H

#include <QMainWindow>
#include <QMessageBox>
#include <QSqlQueryModel>
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
    void populate_servicios();

private slots:
    void initial_settings();
    void reset_all_contents();
    bool validate_form();
    void save_factura();

    void on_buttonBox_clicked(QAbstractButton *button);
    void on_le_importe_textEdited(const QString &arg1);
    void on_cb_iva_currentTextChanged(const QString &arg1);

private:
    Ui::Facturas *ui;
};

#endif // FACTURAS_H
