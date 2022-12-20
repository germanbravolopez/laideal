#ifndef TABLEVIEW_H
#define TABLEVIEW_H

#include <QTableView>
#include <QWidget>
#include <QMouseEvent>
#include <QMenu>

class TableView : public QTableView
{
    Q_OBJECT

public:
    TableView(QWidget *parent);
    ~TableView();
    QAction *action1;
    QAction *action2;
    QMenu *context_menu;

public slots:
    void showContextMenu(QPoint pos);

};

#endif // TABLEVIEW_H
