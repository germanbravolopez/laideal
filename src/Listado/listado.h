#ifndef LISTADO_H
#define LISTADO_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QSqlTableModel>
#include <QMessageBox>
#include <QCoreApplication>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QHeaderView>
#include <tableview.h>

#define NOMBRE_COLUMN_IDX 0

class Listado : public QMainWindow
{
    Q_OBJECT

public:
    QAction *actionActualizar;
    QAction *actionAnadir_fila;
    QAction *actionEliminar_fila;
    QWidget *centralwidget;
    QGridLayout *gridLayout;
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

private slots:
    void resize_window_to_table();
    void on_actionActualizar_triggered();
    void on_actionAnadir_fila_triggered();
    void on_actionEliminar_fila_triggered();

    void closeEvent(QCloseEvent* event);

private:

signals:
    void populate_clientes();
    void populate_prendas();
};

#endif // LISTADO_H
