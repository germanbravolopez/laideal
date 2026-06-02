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
    explicit Facturas(const QSqlDatabase &database, QWidget *parent = nullptr);
    ~Facturas();
    void populateEmpresas();
    void populateServicios();

    // Pure IVA split of a gross amount (importe IVA incluido) at the given rate
    // (percentage, e.g. 21): base imponible = gross / (1 + rate/100); cuota de
    // IVA = gross - base. Exposed as statics for unit testing.
    static double taxBaseFromGross(double gross, double ivaRate);
    static double taxAmountFromGross(double gross, double ivaRate);

private slots:
    void initialSettings();
    void resetAllContents();
    bool validateForm();
    void saveFactura();

    void on_buttonBox_clicked(QAbstractButton *button);
    void on_le_importe_textEdited(const QString &arg1);
    void on_cb_iva_currentTextChanged(const QString &arg1);

private:
    Ui::Facturas *ui;
    QSqlDatabase db;
};

#endif // FACTURAS_H
