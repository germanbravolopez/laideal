#include "tableview.h"

TableView::TableView(QWidget *parent)
    : QTableView(parent)
{
    // create context menu
    context_menu = new QMenu(tr("Context menu"), this);
    action1 = new QAction("Añadir fila", this);
    action2 = new QAction("Eliminar fila", this);
    context_menu->addAction(action1);
    context_menu->addAction(action2);
    // connect context menu
    this->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &TableView::customContextMenuRequested,
            this, &TableView::showContextMenu);
    connect(this, &QTableView::doubleClicked,
            this, &TableView::doubleClick);
}

TableView::~TableView()
{
}

void TableView::showContextMenu(QPoint pos)
{
   context_menu->exec(this->mapToGlobal(pos));
}
