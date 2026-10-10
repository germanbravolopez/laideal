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
#include <QDesktopServices>
#include <QUrl>

#include "tableview.h"
#include "filterwidget.h"
#include "mysortfilterproxymodel.h"
#include "linkdelegate.h"

class QPushButton;
namespace UiKit { class ResultPanel; }

// One table of the database to browse and edit (ingresos, gastos, prendas,
// clientes, proveedores, servicios): heading, explanation, the actions as buttons
// (also in the menus, with their shortcuts), search, the table and a result panel
// where every message appears. Built in code with the shared UiKit style.
class Listado : public QMainWindow
{
    Q_OBJECT

public:
    QAction *actionActualizar;
    QAction *actionAnadir_fila;
    QAction *actionEliminar_fila;
    QAction *actionGenerar_pdf_con_el_listado;
    FilterWidget *filter_widget;
    QLabel *lbl_title;
    TableView *table_listado;

    void populateTable();

    explicit Listado(const QSqlDatabase &database, QWidget *parent = nullptr);
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
    void setupUi();
    // Explanation and available buttons for tableName.
    void configureForTable();

    QSqlDatabase db;
    QLabel *m_lblIntro = nullptr;
    QPushButton *m_btnAdd = nullptr;
    QPushButton *m_btnDelete = nullptr;
    QPushButton *m_btnPdf = nullptr;
    UiKit::ResultPanel *m_lblResult = nullptr;

signals:
    void populateClientes();
    void populatePrendas();
};

#endif // LISTADO_H
