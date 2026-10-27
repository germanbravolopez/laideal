#ifndef TABLEVIEW_H
#define TABLEVIEW_H

#include <QTableView>

class QAction;
class QMenu;

// QTableView with a right-click menu (add / delete row) and a doubleClick signal.
class TableView : public QTableView
{
    Q_OBJECT

public:
    explicit TableView(QWidget *parent);
    QAction *addRowAction() const { return m_addRowAction; }
    QAction *deleteRowAction() const { return m_deleteRowAction; }

public slots:
    void showContextMenu(QPoint pos);

signals:
    void doubleClick(const QModelIndex &index);

private:
    QAction *m_addRowAction;
    QAction *m_deleteRowAction;
    QMenu *m_contextMenu;
};

#endif // TABLEVIEW_H
