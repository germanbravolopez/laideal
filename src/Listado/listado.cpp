#include "listado.h"
#include "sql_lite.h"
#include "numberformatdelegate.h"

Listado::Listado(QWidget *parent) :
    QMainWindow(parent)
{
    setupUi(this);
    connect(table_listado->action1, SIGNAL(triggered()),
            this, SLOT(on_actionAnadir_fila_triggered()));
    connect(table_listado->action2, SIGNAL(triggered()),
            this, SLOT(on_actionEliminar_fila_triggered()));
    connect(filter_widget, &FilterWidget::filterChanged,
            this, &Listado::text_filter_changed);
    connect(filter_widget, &QLineEdit::textChanged,
            this, &Listado::text_filter_changed);
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

    actionActualizar = new QAction(Listado);
    actionActualizar->setObjectName("actionActualizar");
    actionAnadir_fila = new QAction(Listado);
    actionAnadir_fila->setObjectName("actionAnadir_fila");
    actionEliminar_fila = new QAction(Listado);
    actionEliminar_fila->setObjectName("actionEliminar_fila");

    centralwidget = new QWidget(Listado);
    centralwidget->setObjectName("centralwidget");
    gridLayout = new QGridLayout(centralwidget);
    gridLayout->setObjectName("gridLayout");
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

    gridLayout->addWidget(table_listado, 8, 0, 1, 1);

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

    retranslateUi(Listado);

    QMetaObject::connectSlotsByName(Listado);
}

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
    lbl_search->setText(QCoreApplication::translate("Listado", "B\303\272squeda:", nullptr));
    lbl_title->setText(QCoreApplication::translate("Listado", "Listado", nullptr));
    menuArchivo->setTitle(QCoreApplication::translate("Listado", "Archivo", nullptr));
    menuHerramientas->setTitle(QCoreApplication::translate("Listado", "Herramientas", nullptr));
}

void Listado::populate_table()
{
    if (QSqlDatabase::contains("qt_sql_default_connection"))
    {
        model = new QSqlTableModel(this, QSqlDatabase::database("qt_sql_default_connection"));
        model->setTable(table_name);
        model->setEditStrategy(QSqlTableModel::OnFieldChange);
        model->select();
        proxyModel = new MySortFilterProxyModel(this);
        proxyModel->setSourceModel(model);
        table_listado->setModel(proxyModel);
        table_listado->resizeColumnsToContents();
        table_listado->sortByColumn(NOMBRE_COLUMN_IDX, Qt::AscendingOrder);
        if (table_name == "prendas") {
            table_listado->setItemDelegateForColumn(1, new NumberFormatDelegate(this));
            table_listado->setItemDelegateForColumn(2, new NumberFormatDelegate(this));
        }
    }
    resize_window_to_table();
}

void Listado::resize_window_to_table()
{
    // Set window size to minimun of size of the table
    int size = 0;
    for (int column = 0; column < model->columnCount(); column++) {
        size += table_listado->columnWidth(column);
    }
    if (this->width() < size + 40) {
        this->resize(size + 40, this->height());
    }
}

void Listado::text_filter_changed()
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
}

void Listado::on_actionActualizar_triggered()
{
    populate_table();
}

void Listado::on_actionAnadir_fila_triggered()
{
    model->insertRow(table_listado->currentIndex().row() + 1);
    if (table_name == "clientes") {
        insert_new_item_to_table(db, {"", "", "", ""}, "clientes");
    } else if (table_name == "prendas") {
        insert_new_item_to_table(db, {"", "", ""}, "prendas");
    } else if (table_name == "proveedores") {
        insert_new_item_to_table(db, {"", "", "", ""}, "proveedores");
    }else if (table_name == "servicios") {
        insert_new_item_to_table(db, {""}, "servicios");
    }
    populate_table();
}

void Listado::on_actionEliminar_fila_triggered()
{
    int ret = QMessageBox::question(this, "Eliminar fila",
                                    "¿Está seguro que desea eliminar la fila " +
                                    QString::number(table_listado->currentIndex().row() + 1) + "?",
                                    QMessageBox::Yes | QMessageBox::No,
                                    QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        model->removeRow(table_listado->currentIndex().row());
        populate_table();
    }
}

void Listado::closeEvent(QCloseEvent* event)
{
    if (table_name == "clientes") {
        emit populate_clientes();
        event->accept();
    } else if (table_name == "prendas") {
        emit populate_prendas();
        event->accept();
    }
}
