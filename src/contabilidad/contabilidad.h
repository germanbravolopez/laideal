#ifndef CONTABILIDAD_H
#define CONTABILIDAD_H

#include <QMainWindow>
#include <QMessageBox>
#include <QSqlQueryModel>
#include <QDate>
#include <QMessageBox>

#include <QApplication>
#include <QPdfWriter>
#include <QPainter>
#include <QDesktopServices>

namespace Ui {
class Contabilidad;
}

class Contabilidad : public QMainWindow
{
    Q_OBJECT

public:
    QSqlDatabase db;
    QSqlQueryModel *sql_query_model = new QSqlQueryModel;
    explicit Contabilidad(QWidget *parent = nullptr);
    ~Contabilidad();

private slots:
    void initial_settings();
    void reset_all_contents();

    void on_bb_ok_cancel_accepted();
    void on_bb_ok_cancel_rejected();

    void generate_contabilidad();
    double get_total_income(QString table, int iva);
    void lock_data();
    void ask_for_repeat();
    void write_to_pdf(QString filename, QString html);

private:
    Ui::Contabilidad *ui;
};

#endif // CONTABILIDAD_H
