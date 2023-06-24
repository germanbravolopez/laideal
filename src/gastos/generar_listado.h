#ifndef GENERAR_LISTADO_H
#define GENERAR_LISTADO_H

#include <QMainWindow>
#include <QDir>
#include <QDesktopServices>
#include <QAbstractItemModel>
#include <QSqlDatabase>

namespace Ui {
class GenerarListado;
}

class GenerarListado : public QMainWindow
{
    Q_OBJECT

public:
    explicit GenerarListado(QWidget *parent = nullptr);
    ~GenerarListado();
    QSqlDatabase db;
    QAbstractItemModel *model;

private slots:
    void initial_settings();
    void set_cb_fechas();
    void write_html(QString filename, QString html);
    QString generate_html_table();

    void on_buttonBox_accepted();
    void on_bb_ok_cancel_rejected();

private:
    Ui::GenerarListado *ui;
};

#endif // GENERAR_LISTADO_H
