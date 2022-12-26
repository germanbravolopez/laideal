#ifndef IMPRIMIR_H
#define IMPRIMIR_H

#include <QDialog>
#include <QSqlDatabase>
#include <QMessageBox>
#include <QTextEdit>
#include <QColor>
#include <QTextDocument>
#include <QFile>
#include <QPainter>

namespace Ui {
class Imprimir;
}

class Imprimir : public QDialog
{
    Q_OBJECT

public:
    QSqlDatabase db;
    explicit Imprimir(QWidget *parent = nullptr);
    ~Imprimir();

private slots:
    void create_ticket_and_print();
    void on_bb_ok_cancel_accepted();
    void on_bb_ok_cancel_rejected();

private:
    Ui::Imprimir *ui;

};

#endif // IMPRIMIR_H
