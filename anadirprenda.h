#ifndef ANADIRPRENDA_H
#define ANADIRPRENDA_H

#include <QDialog>
#include <QPushButton>
#include <QMessageBox>
#include <QSqlDatabase>

namespace Ui {
class AnadirPrenda;
}

class AnadirPrenda : public QDialog
{
    Q_OBJECT

public:
    QSqlDatabase db;
    explicit AnadirPrenda(QWidget *parent = nullptr);
    ~AnadirPrenda();

private slots:
    void anadirprenda_initial_settings();
    void on_bb_ok_cancel_clicked(QAbstractButton *button);
    bool check_garment_is_not_in_db();
    void add_garment_to_db();
    void update_garment_to_db();
    void on_cb_nombre_editTextChanged(const QString &arg1);

private:
    Ui::AnadirPrenda *ui;
};

#endif // ANADIRPRENDA_H
