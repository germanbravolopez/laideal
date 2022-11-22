#ifndef ANADIRCLIENTE_H
#define ANADIRCLIENTE_H

#include <QDialog>
#include <QPushButton>
#include <QMessageBox>
#include <QSqlDatabase>

namespace Ui {
class AnadirCliente;
}

class AnadirCliente : public QDialog
{
    Q_OBJECT

public:
    QSqlDatabase db;
    explicit AnadirCliente(QWidget *parent = nullptr);
    ~AnadirCliente();

private slots:
    void anadircliente_initial_settings();
    void on_bb_ok_cancel_clicked(QAbstractButton *button);
    bool check_client_is_not_in_db();
    void add_client_to_db();
    void update_client_to_db();
    void on_cb_nombre_editTextChanged(const QString &arg1);

private:
    Ui::AnadirCliente *ui;
};

#endif // ANADIRCLIENTE_H
