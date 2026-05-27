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
    QSqlQueryModel *sqlQueryModel = new QSqlQueryModel;
    bool ticketFound = false;

private slots:
    void initialSettings();
    void resetAllContents();
    void on_pb_search_pressed();
    void fillContentFromDb();
    void populateGarments();
    void on_pb_pagado_toggled(bool checked);
    void on_pb_estado_toggled(bool checked);
    void setGarmentPrice();
    void on_le_cantidad_textChanged(const QString &arg1);
    void on_le_size_textChanged(const QString &arg1);
    void on_cb_servicio_currentTextChanged(const QString &arg1);
    void on_cb_prenda_currentTextChanged(const QString &arg1);
    void on_buttonBox_clicked(QAbstractButton *button);
    bool validateForm();
    void saveFactura();

private:
    Ui::AddGarment *ui;

};

#endif // ADDGARMENT_H
