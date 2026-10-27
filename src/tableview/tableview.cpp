#include "tableview.h"

#include <QAction>
#include <QMenu>

TableView::TableView(QWidget *parent)
    : QTableView(parent)
{
    m_contextMenu = new QMenu(tr("Menú contextual"), this);
    m_addRowAction = new QAction(tr("Añadir fila"), this);
    m_deleteRowAction = new QAction(tr("Eliminar fila"), this);
    m_contextMenu->addAction(m_addRowAction);
    m_contextMenu->addAction(m_deleteRowAction);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &TableView::customContextMenuRequested,
            this, &TableView::showContextMenu);
    connect(this, &QTableView::doubleClicked,
            this, &TableView::doubleClick);
}

void TableView::showContextMenu(QPoint pos)
{
    m_contextMenu->exec(mapToGlobal(pos));
}
