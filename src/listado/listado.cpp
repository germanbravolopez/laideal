#include "listado.h"
#include "sql_lite.h"
#include "genlistado.h"
#include "insertnewitem.h"
#include "numberformatdelegate.h"
#include "textcolordelegate.h"

Listado::Listado(QWidget *parent) :
    QMainWindow(parent)
{
    setupUi(this);
    connect(table_listado->action1, &QAction::triggered,
            this, &Listado::on_actionAnadir_fila_triggered);
    connect(table_listado->action2, &QAction::triggered,
            this, &Listado::on_actionEliminar_fila_triggered);
    connect(filter_widget, &FilterWidget::filterChanged,
            this, &Listado::textFilterChanged);
    connect(filter_widget, &QLineEdit::textChanged,
            this, &Listado::textFilterChanged);
    connect(table_listado, &TableView::doubleClick,
            this, &Listado::handleDoubleClick, Qt::QueuedConnection);
}

void Listado::setupUi(QMainWindow *Listado)
{
    if (Listado->objectName().isEmpty())
        Listado->setObjectName("Listado");
    Listado->resize(520, 698);
    Listado->setMinimumSize(QSize(0, 0));
    QFont font;
    font.setPointSize(10);
    Listado->setFont(font);
    // task bar
    actionActualizar = new QAction(Listado);
    actionActualizar->setObjectName("actionActualizar");
    actionAnadir_fila = new QAction(Listado);
    actionAnadir_fila->setObjectName("actionAnadir_fila");
    actionEliminar_fila = new QAction(Listado);
    actionEliminar_fila->setObjectName("actionEliminar_fila");
    actionGenerar_pdf_con_el_listado = new QAction(Listado);
    actionGenerar_pdf_con_el_listado->setObjectName("actionGenerar_pdf_con_el_listado");
    // central widget
    centralwidget = new QWidget(Listado);
    centralwidget->setObjectName("centralwidget");
    gridLayout = new QGridLayout(centralwidget);
    gridLayout->setObjectName("gridLayout");
    // search bar
    horizontalLayout = new QHBoxLayout();
    horizontalLayout->setObjectName("horizontalLayout");
    lbl_search = new QLabel(centralwidget);
    lbl_search->setObjectName("lbl_search");
    lbl_search->setMaximumSize(QSize(65, 16777215));
    horizontalLayout->addWidget(lbl_search);

    filter_widget = new FilterWidget(centralwidget);
    filter_widget->setObjectName("filter_widget");
    horizontalLayout->addWidget(filter_widget);
    gridLayout->addLayout(horizontalLayout, 6, 0, 1, 1);
    // main title
    lbl_title = new QLabel(centralwidget);
    lbl_title->setObjectName("lbl_title");
    QFont font1;
    font1.setFamilies({QString::fromUtf8("Arial")});
    font1.setPointSize(26);
    font1.setBold(false);
    lbl_title->setFont(font1);
    lbl_title->setCursor(QCursor(Qt::ArrowCursor));
    lbl_title->setAutoFillBackground(true);
    lbl_title->setLocale(QLocale(QLocale::Spanish, QLocale::Spain));
    lbl_title->setAlignment(Qt::AlignCenter);
    gridLayout->addWidget(lbl_title, 0, 0, 1, 2);
    // table widget
    table_listado = new TableView(centralwidget);
    table_listado->setObjectName("table_listado");
    table_listado->setMinimumSize(QSize(500, 300));
    table_listado->setMaximumSize(QSize(16777215, 16777215));
    table_listado->setLayoutDirection(Qt::LeftToRight);
    table_listado->setLocale(QLocale(QLocale::Spanish, QLocale::Spain));
    table_listado->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    table_listado->setAlternatingRowColors(true);
    table_listado->setSelectionMode(QAbstractItemView::ContiguousSelection);
    table_listado->setSortingEnabled(true);
    table_listado->horizontalHeader()->setProperty("showSortIndicator", QVariant(true));
    table_listado->verticalHeader()->setVisible(false);
    gridLayout->addWidget(table_listado, 8, 0, 1, 1);
    // set menu bar
    Listado->setCentralWidget(centralwidget);
    menubar = new QMenuBar(Listado);
    menubar->setObjectName("menubar");
    menubar->setGeometry(QRect(0, 0, 520, 23));
    menuArchivo = new QMenu(menubar);
    menuArchivo->setObjectName("menuArchivo");
    menuHerramientas = new QMenu(menubar);
    menuHerramientas->setObjectName("menuHerramientas");
    Listado->setMenuBar(menubar);
    menubar->addAction(menuArchivo->menuAction());
    menubar->addAction(menuHerramientas->menuAction());
    menuArchivo->addAction(actionActualizar);
    menuHerramientas->addAction(actionAnadir_fila);
    menuHerramientas->addAction(actionEliminar_fila);
    menuHerramientas->addSeparator();
    menuHerramientas->addAction(actionGenerar_pdf_con_el_listado);

    retranslateUi(Listado);

    QMetaObject::connectSlotsByName(Listado);
} // setupUi

void Listado::retranslateUi(QMainWindow *Listado)
{
    Listado->setWindowTitle(QCoreApplication::translate("Listado", "Listado", nullptr));
    actionActualizar->setText(QCoreApplication::translate("Listado", "Actualizar", nullptr));
#if QT_CONFIG(shortcut)
    actionActualizar->setShortcut(QCoreApplication::translate("Listado", "Ctrl+A", nullptr));
#endif // QT_CONFIG(shortcut)
    actionAnadir_fila->setText(QCoreApplication::translate("Listado", "A\303\261adir fila", nullptr));
#if QT_CONFIG(shortcut)
    actionAnadir_fila->setShortcut(QCoreApplication::translate("Listado", "Ctrl+N", nullptr));
#endif // QT_CONFIG(shortcut)
    actionEliminar_fila->setText(QCoreApplication::translate("Listado", "Eliminar fila", nullptr));
#if QT_CONFIG(shortcut)
    actionEliminar_fila->setShortcut(QCoreApplication::translate("Listado", "Ctrl+D", nullptr));
#endif // QT_CONFIG(shortcut)
    actionGenerar_pdf_con_el_listado->setText(QCoreApplication::translate("Gastos", "Generar listado en pdf", nullptr));
#if QT_CONFIG(shortcut)
    actionGenerar_pdf_con_el_listado->setShortcut(QCoreApplication::translate("Gastos", "Ctrl+P", nullptr));
#endif // QT_CONFIG(shortcut)
    lbl_search->setText(QCoreApplication::translate("Listado", "B\303\272squeda:", nullptr));
    lbl_title->setText(QCoreApplication::translate("Listado", "Listado", nullptr));
    menuArchivo->setTitle(QCoreApplication::translate("Listado", "Archivo", nullptr));
    menuHerramientas->setTitle(QCoreApplication::translate("Listado", "Herramientas", nullptr));
} // retranslateUi

void Listado::populateTable()
{
    // Change the cursor to a loading icon
    QApplication::setOverrideCursor(Qt::WaitCursor);

    if (QSqlDatabase::contains("qt_sql_default_connection")) {
        model = new QSqlTableModel(this, QSqlDatabase::database("qt_sql_default_connection"));
        model->setTable(tableName);
        model->setEditStrategy(QSqlTableModel::OnFieldChange);
        // order model before showing data in table
        model->setSort(0, Qt::AscendingOrder);
        model->select();
        proxyModel = new MySortFilterProxyModel(this);
        proxyModel->table_name = tableName;
        proxyModel->setSourceModel(model);
        table_listado->setModel(proxyModel);
        // Scroll all the way down and all the way up to get all data populated
        int previousLength = 0;
        QScrollBar *verticalScrollBar = table_listado->verticalScrollBar();
        while (proxyModel->rowCount() > previousLength) {
            previousLength = proxyModel->rowCount();
            verticalScrollBar->setValue(verticalScrollBar->maximum());
        }
        verticalScrollBar->setValue(verticalScrollBar->minimum());
        // Perform sorting with proxy model
        if (tableName == "gastos")
            table_listado->sortByColumn(GASTOS_IDX_FECHA, Qt::DescendingOrder);
        else if (tableName == "ingresos")
            table_listado->sortByColumn(INGRESOS_IDX_ID, Qt::DescendingOrder);
        else
            table_listado->sortByColumn(LIST_PRENDAS_IDX_NAME, Qt::AscendingOrder);
        // Configure NumberDelegate
        if (tableName == "prendas") {
            table_listado->setItemDelegateForColumn(LIST_PRENDAS_IDX_LIMP, new NumberFormatDelegate(this));
            table_listado->setItemDelegateForColumn(LIST_PRENDAS_IDX_PLAN, new NumberFormatDelegate(this));
        }
        else if (tableName == "gastos") {
            table_listado->setItemDelegateForColumn(GASTOS_IDX_IMPORTE, new NumberFormatDelegate(this));
        }
        table_listado->setFont(QFont(table_listado->font().family(), 8));
        if (tableName == "ingresos") {
            table_listado->setItemDelegateForColumn(INGRESOS_IDX_IMPORTE, new NumberFormatDelegate(this));
            table_listado->setItemDelegateForColumn(INGRESOS_IDX_PAYED, new TextColorDelegate(table_listado, this));
            table_listado->setItemDelegateForColumn(INGRESOS_IDX_STATE, new TextColorDelegate(table_listado, this));
        }
        // Resize table
        table_listado->resizeColumnsToContents();
        table_listado->resizeRowsToContents();
    }
    resizeWindowToTable();

    // Restore the cursor to default
    QApplication::restoreOverrideCursor();
}

void Listado::resizeWindowToTable()
{
    // Set window size to minimun of size of the table
    int size = 0;
    for (int column = 0; column < table_listado->model()->columnCount(); column++) {
        size += table_listado->columnWidth(column);
    }
    if (this->width() < size + 40) {
        this->resize(size + 40, this->height());
    }
}

void Listado::textFilterChanged()
{
    FilterWidget::PatternSyntax s = filter_widget->patternSyntax();
    QString pattern = filter_widget->text();
    switch (s) {
    case FilterWidget::Wildcard:
        pattern = QRegularExpression::wildcardToRegularExpression(pattern);
        break;
    case FilterWidget::FixedString:
        pattern = QRegularExpression::escape(pattern);
        break;
    default:
        break;
    }

    QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
    if (filter_widget->caseSensitivity() == Qt::CaseInsensitive)
        options |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression regularExpression(pattern, options);
    proxyModel->setFilterRegularExpression(regularExpression);
    // Also set normalized filter so searches without tildes match names that have them
    proxyModel->setNormalizedFilter(MySortFilterProxyModel::removeDiacritics(filter_widget->text()).toLower());
    // Resize table
    table_listado->resizeColumnsToContents();
    table_listado->resizeRowsToContents();
}

void Listado::on_actionActualizar_triggered()
{
    QScrollBar *verticalScrollBar = table_listado->verticalScrollBar();
    verticalScrollBar->setValue(verticalScrollBar->minimum());
    if (tableName == "gastos")
        table_listado->sortByColumn(GASTOS_IDX_FECHA, Qt::DescendingOrder);
    else if (tableName == "ingresos")
        table_listado->sortByColumn(INGRESOS_IDX_ID, Qt::DescendingOrder);
    else
        table_listado->sortByColumn(LIST_PRENDAS_IDX_NAME, Qt::AscendingOrder);
    table_listado->resizeColumnsToContents();
    table_listado->resizeRowsToContents();
    resizeWindowToTable();
}

void Listado::on_actionAnadir_fila_triggered()
{
    if (tableName == "clientes") {
        InsertNewItem *ui_insert_new;
        ui_insert_new = new InsertNewItem(this);
        ui_insert_new->db = db;
        ui_insert_new->exec();
        populateTable();
    } else if (tableName == "prendas") {
        table_listado->model()->insertRow(table_listado->currentIndex().row() + 1);
        insertNewItemToTable(db, {"", "", ""}, "prendas");
        populateTable();
    } else if (tableName == "proveedores") {
        table_listado->model()->insertRow(table_listado->currentIndex().row() + 1);
        insertNewItemToTable(db, {"", "", "", ""}, "proveedores");
        populateTable();
    } else if (tableName == "servicios") {
        table_listado->model()->insertRow(table_listado->currentIndex().row() + 1);
        insertNewItemToTable(db, {""}, "servicios");
        populateTable();
    } else if (tableName == "gastos") {
        QMessageBox::information(this, "Añadir fila",
                                 "Para introducir nuevos gastos, hay que usar la herramienta de introducir "
                                 "facturas en la ventana principal.",
                                 QMessageBox::Ok, QMessageBox::Ok);
        populateTable();
    } else
        QMessageBox::critical(this, "Añadir fila",
                              "Esta tabla no soporta añadir nuevas filas directamente.",
                              QMessageBox::Ok, QMessageBox::Ok);
}

void Listado::on_actionEliminar_fila_triggered()
{
    if (tableName == "ingresos")
        QMessageBox::critical(this, "Eliminar fila",
                              "Esta tabla no soporta eliminar filas directamente.",
                              QMessageBox::Ok, QMessageBox::Ok);
    else {
        int ret = QMessageBox::question(this, "Eliminar fila",
                                        "¿Está seguro que desea eliminar la fila " +
                                        QString::number(table_listado->currentIndex().row() + 1) + "?",
                                        QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            table_listado->model()->removeRow(table_listado->currentIndex().row());
            populateTable();
        }
    }
}

void Listado::on_actionGenerar_pdf_con_el_listado_triggered()
{
    if (tableName == "gastos") {
        GenListado *ui_generar_listado;
        ui_generar_listado = new GenListado(this);
        ui_generar_listado->db = db;
        ui_generar_listado->model = table_listado->model();
        ui_generar_listado->exec();
        populateTable();
    } else if (tableName == "prendas") {
            GenListado *ui_generar_listado;
            ui_generar_listado = new GenListado(this);
            ui_generar_listado->db = db;
            ui_generar_listado->model = table_listado->model();
            ui_generar_listado->table_name = tableName;
            ui_generar_listado->print_table();
    } else {
        QMessageBox::information(this, "Generar listado",
                                 "Herramienta para generar listado no está implementada para este tipo de listado.",
                                 QMessageBox::Ok, QMessageBox::Ok);
    }
}

void Listado::closeEvent(QCloseEvent* event)
{
    if (tableName == "clientes") {
        emit populateClientes();
        event->accept();
    } else if (tableName == "prendas") {
        emit populatePrendas();
        event->accept();
    }
}

void Listado::handleDoubleClick(const QModelIndex &index)
{
    if (index.isValid()) {
        int lastColumn = table_listado->model()->columnCount() - 1;
        QString headerName = table_listado->model()->headerData(lastColumn, Qt::Horizontal, Qt::DisplayRole).toString();
        if (headerName == "edit_lock") {
            QVariant value = table_listado->model()->data(table_listado->model()->index(index.row(), lastColumn));
            if (value.toInt() == 1 && index.column() != lastColumn) {
                QMessageBox::warning(this, "Edición bloqueada",
                                     "No es posible editar el contenido porque se encuentra cerrado por contabilidad.\n"
                                     "Desbloquear la fila para poder editarlo.",
                                     QMessageBox::Ok, QMessageBox::Ok);
                // Deselect the current cell
                QItemSelectionModel *selectionModel = table_listado->selectionModel();
                selectionModel->clearSelection();
            }
        }
    }
}
