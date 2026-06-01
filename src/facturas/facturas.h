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
