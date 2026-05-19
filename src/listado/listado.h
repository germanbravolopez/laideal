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
#include <QApplication>
#include <QScreen>

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
    void populateTable();

    explicit Listado(QWidget *parent = nullptr);
    QSqlDatabase db;
    QString tableName;
    QSqlTableModel *model;
    MySortFilterProxyModel *proxyModel;

private slots:
    void resizeWindowToTable();
    void textFilterChanged();
    void on_actionActualizar_triggered();
    void on_actionAnadir_fila_triggered();
    void on_actionEliminar_fila_triggered();
    void on_actionGenerar_pdf_con_el_listado_triggered();
    void closeEvent(QCloseEvent* event);
    void handleDoubleClick(const QModelIndex &index);

private:

signals:
    void populateClientes();
    void populatePrendas();
};

#endif // LISTADO_H
