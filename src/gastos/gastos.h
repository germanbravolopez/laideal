#ifndef GASTOS_H
#define GASTOS_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QDesktopServices>
#include <QPainter>

#include "mysortfilterproxymodel.h"

namespace Ui {
class Gastos;
}

class Gastos : public QMainWindow
{
    Q_OBJECT

public:
    QSqlTableModel *model;
    MySortFilterProxyModel *proxyModel;
    explicit Gastos(QWidget *parent = nullptr);
    ~Gastos();

private slots:
    void populate_table();
    void on_actionActualizar_triggered();
    void on_actionActivar_modo_edicion_triggered();
    void on_actionDesactivar_modo_edicion_triggered();
    void on_actionAnadir_fila_triggered();
    void on_actionEliminar_fila_triggered();
    void on_actionGenerar_pdf_con_el_listado_triggered();

    void write_html(QString filename, QString html);
    QString generate_html_table();

private:
    Ui::Gastos *ui;
};

#endif // GASTOS_H
