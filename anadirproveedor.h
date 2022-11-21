#ifndef ANADIRPROVEEDOR_H
#define ANADIRPROVEEDOR_H

#include <QDialog>
#include <QPushButton>
#include <QMessageBox>
#include <QSqlDatabase>

namespace Ui {
class AnadirProveedor;
}

class AnadirProveedor : public QDialog
{
    Q_OBJECT

public:
    QSqlDatabase db;
    explicit AnadirProveedor(QWidget *parent = nullptr);
    ~AnadirProveedor();

private slots:
    void anadirproveedor_initial_settings();
    void on_bb_ok_cancel_clicked(QAbstractButton *button);
    bool check_supplier_is_not_in_db();
    void add_supplier_to_db();
    void update_supplier_to_db();
    void on_cb_empresa_editTextChanged(const QString &arg1);

private:
    Ui::AnadirProveedor *ui;
};

#endif // ANADIRPROVEEDOR_H
