#ifndef ADDGARMENT_H
#define ADDGARMENT_H

// Handle logic
#include <QMainWindow>
#include <QMessageBox>
#include <QSqlQueryModel>
#include <QPushButton>

namespace Ui {
class AddGarment;
}

class AddGarment : public QMainWindow
{
    Q_OBJECT

public:
    explicit AddGarment(QWidget *parent = nullptr);
    ~AddGarment();
    QSqlDatabase db;
    QSqlQueryModel *sql_query_model = new QSqlQueryModel;
    bool ticket_found = false;

private slots:
    void initial_settings();
    void reset_all_contents();
    void on_pb_search_pressed();
    void fill_content_from_db();
    void populate_garments();
    void on_pb_pagado_toggled(bool checked);
    void on_pb_estado_toggled(bool checked);
    void set_garment_price();
    void on_le_cantidad_textChanged(const QString &arg1);
    void on_le_size_textChanged(const QString &arg1);
    void on_cb_servicio_currentTextChanged(const QString &arg1);
    void on_cb_prenda_currentTextChanged(const QString &arg1);
    void on_buttonBox_clicked(QAbstractButton *button);
    bool validate_form();
    void save_factura();

private:
    Ui::AddGarment *ui;

};

#endif // ADDGARMENT_H
