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
#include <QDir>

namespace Ui {
class Contabilidad;
}

class Contabilidad : public QMainWindow
{
    Q_OBJECT

public:
    const QString C_MENSUAL     = "Mensual";
    const QString C_TRIMESTRAL  = "Trimestral";
    const QString C_ANUAL       = "Anual";

    QSqlDatabase db;
    explicit Contabilidad(QWidget *parent = nullptr);
    ~Contabilidad();

private slots:
    void initial_settings();
    void reset_all_contents();

    void on_bb_ok_cancel_accepted();
    void on_bb_ok_cancel_rejected();
    void on_cb_config_currentTextChanged(const QString &arg1);

    void generate_contabilidad();
    float get_total_income(QString table, int iva, int trim_for_year_config);
    void lock_data();
    void write_html(QString filename, QString html);
    QString create_html_header();
    QString create_html_tables(int trim_for_year_config);
    QString create_html_table_ingresos(int trim_for_year_config);
    QString create_html_table_gastos(int trim_for_year_config);


private:
    Ui::Contabilidad *ui;
};

#endif // CONTABILIDAD_H
