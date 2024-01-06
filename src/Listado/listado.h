#ifndef LISTADO_H
#define LISTADO_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>
#include <QMessageBox>
#include <QCoreApplication>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QLineEdit>
#include <QStatusBar>
#include <QScrollBar>

#include "tableview.h"
#include "filterwidget.h"
#include "mysortfilterproxymodel.h"

class Listado : public QMainWindow
{
    Q_OBJECT

public:
    QAction *actionActualizar;
    QAction *actionAnadir_fila;
    QAction *actionEliminar_fila;
    QAction *actionGenerar_pdf_con_el_listado;
    QWidget *centralwidget;
    QGridLayout *gridLayout;
    QHBoxLayout *horizontalLayout;
    QLabel *lbl_search;
    FilterWidget *filter_widget;
    QLabel *lbl_title;
    TableView *table_listado;
    QMenuBar *menubar;
    QMenu *menuArchivo;
    QMenu *menuHerramientas;

    void setupUi(QMainWindow *Listado);
    void retranslateUi(QMainWindow *Listado);
    void populate_table();

    explicit Listado(QWidget *parent = nullptr);
    QSqlDatabase db;
    QString table_name;
    QSqlTableModel *model;
    MySortFilterProxyModel *proxyModel;

private slots:
    void resize_window_to_table();
    void text_filter_changed();
    void on_actionActualizar_triggered();
    void on_actionAnadir_fila_triggered();
    void on_actionEliminar_fila_triggered();
    void on_actionGenerar_pdf_con_el_listado_triggered();
    void closeEvent(QCloseEvent* event);
    void handleDoubleClick(const QModelIndex &index);

private:

signals:
    void populate_clientes();
    void populate_prendas();
};

#endif // LISTADO_H
