#include "listado.h"
#include "sql_lite.h"
#include "genlistado.h"
#include "insertnewitem.h"
#include "numberformatdelegate.h"
#include "textcolordelegate.h"
#include "ingresoscolumns.h"
#include "uikit.h"

#include <QHash>
#include <QPushButton>
#include <QVBoxLayout>

Listado::Listado(const QSqlDatabase &database, QWidget *parent) :
    QMainWindow(parent),
    db(database)
{
    setupUi();
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
    connect(table_listado, &QTableView::clicked, this, [this](const QModelIndex &index) {
        if (tableName == "ingresos" && index.column() == INGRESOS_COL_VERIFACTU_URL_QR) {
            QString url = index.data().toString();
            if (!url.isEmpty())
                QDesktopServices::openUrl(QUrl(url));
        }
    });
}

void Listado::setupUi()
{
    setObjectName("Listado");
    setWindowTitle("Listado");
    resize(720, 700);
    // The actions keep their menu entries and shortcuts; the buttons below trigger them too.
    actionActualizar = new QAction("Actualizar", this);
    actionActualizar->setObjectName("actionActualizar");
    actionActualizar->setShortcut(QKeySequence("Ctrl+A"));
    actionAnadir_fila = new QAction("Añadir fila", this);
    actionAnadir_fila->setObjectName("actionAnadir_fila");
    actionAnadir_fila->setShortcut(QKeySequence("Ctrl+N"));
    actionEliminar_fila = new QAction("Eliminar fila", this);
    actionEliminar_fila->setObjectName("actionEliminar_fila");
    actionEliminar_fila->setShortcut(QKeySequence("Ctrl+D"));
    actionGenerar_pdf_con_el_listado = new QAction("Generar listado en PDF", this);
    actionGenerar_pdf_con_el_listado->setObjectName("actionGenerar_pdf_con_el_listado");
    actionGenerar_pdf_con_el_listado->setShortcut(QKeySequence("Ctrl+P"));

    QMenu *menuArchivo = menuBar()->addMenu("Archivo");
    menuArchivo->addAction(actionActualizar);
    QMenu *menuHerramientas = menuBar()->addMenu("Herramientas");
    menuHerramientas->addAction(actionAnadir_fila);
    menuHerramientas->addAction(actionEliminar_fila);
    menuHerramientas->addSeparator();
    menuHerramientas->addAction(actionGenerar_pdf_con_el_listado);

    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    lbl_title = UiKit::heading("Listado");
    lbl_title->setObjectName("lbl_title");
    layout->addWidget(lbl_title);
    m_lblIntro = UiKit::introPanel(QString());
    layout->addWidget(m_lblIntro);

    QHBoxLayout *actions = new QHBoxLayout();
    m_btnAdd = UiKit::secondaryButton("Añadir fila", "btnAdd");
    m_btnDelete = UiKit::secondaryButton("Eliminar fila", "btnDelete");
    m_btnPdf = UiKit::secondaryButton("Generar PDF", "btnPdf");
    QPushButton *btnRefresh = UiKit::secondaryButton("Actualizar", "btnRefresh");
    actions->addWidget(m_btnAdd);
    actions->addWidget(m_btnDelete);
    actions->addWidget(m_btnPdf);
    actions->addStretch();
    actions->addWidget(new QLabel("Buscar:"));
    filter_widget = new FilterWidget(central);
    filter_widget->setObjectName("filter_widget");
    filter_widget->setMinimumWidth(220);
    actions->addWidget(filter_widget);
    actions->addWidget(btnRefresh);
    layout->addLayout(actions);

    table_listado = new TableView(central);
    table_listado->setObjectName("table_listado");
    table_listado->setMinimumSize(QSize(500, 300));
    table_listado->setLocale(QLocale(QLocale::Spanish, QLocale::Spain));
    table_listado->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    table_listado->setAlternatingRowColors(true);
    table_listado->setSelectionMode(QAbstractItemView::ContiguousSelection);
    table_listado->setSortingEnabled(true);
    table_listado->horizontalHeader()->setProperty("showSortIndicator", QVariant(true));
    table_listado->verticalHeader()->setVisible(false);
    // One point under the app font: the tables are wide (ingresos has ~20 columns).
    QFont tableFont = table_listado->font();
    tableFont.setPointSizeF(tableFont.pointSizeF() - 1);
    table_listado->setFont(tableFont);
    layout->addWidget(table_listado, 1);

    m_lblResult = new UiKit::ResultPanel();
    m_lblResult->setMinimumHeight(40);
    layout->addWidget(m_lblResult);
    setCentralWidget(central);

    connect(m_btnAdd,    &QPushButton::clicked, actionAnadir_fila, &QAction::trigger);
    connect(m_btnDelete, &QPushButton::clicked, actionEliminar_fila, &QAction::trigger);
    connect(m_btnPdf,    &QPushButton::clicked, actionGenerar_pdf_con_el_listado, &QAction::trigger);
    connect(btnRefresh,  &QPushButton::clicked, actionActualizar, &QAction::trigger);
    QMetaObject::connectSlotsByName(this);
}

void Listado::configureForTable()
{
    struct Config { const char *intro; bool add, del, pdf; };
    static const QHash<QString, Config> configs = {
        { "ingresos",    { "Todas las prendas cobradas y pendientes, ticket a ticket. Es de solo lectura: los cambios "
                           "se hacen en Recogida de prendas, Anular factura y Rectificar factura. Clic en el enlace "
                           "de AEAT para ver la factura registrada.", false, false, false } },
        { "gastos",      { "Facturas de gastos. Doble clic en una celda para corregirla; las de un trimestre cerrado "
                           "por la contabilidad no se pueden editar. Las facturas nuevas se añaden con Formulario "
                           "facturas.", false, true, true } },
        { "prendas",     { "Prendas y sus precios de limpieza y plancha. Doble clic en una celda para cambiarla.",
                           true, true, true } },
        { "clientes",    { "Clientes de la tintorería. Doble clic en una celda para cambiarla.", true, true, false } },
        { "proveedores", { "Proveedores de las facturas de gastos. Doble clic en una celda para cambiarla.",
                           true, true, false } },
        { "servicios",   { "Servicios de las facturas de gastos. Doble clic en una celda para cambiarla.",
                           true, true, false } },
    };
    const Config c = configs.value(tableName, { "", false, false, false });
    m_lblIntro->setText(QString::fromUtf8(c.intro));
    m_lblIntro->setVisible(*c.intro != '\0');
    m_btnAdd->setVisible(c.add);
    m_btnDelete->setVisible(c.del);
    m_btnPdf->setVisible(c.pdf);
}

void Listado::populateTable()
{
    configureForTable();
    // Change the cursor to a loading icon
    QApplication::setOverrideCursor(Qt::WaitCursor);

    if (QSqlDatabase::contains("qt_sql_default_connection")) {
        model = new QSqlTableModel(this, QSqlDatabase::database("qt_sql_default_connection"));
        model->setTable(tableName);
        model->setEditStrategy(QSqlTableModel::OnFieldChange);
        // For ingresos, fetch most-recent rows first so the first batch is already the latest.
        // For other tables, ascending order + force-fetch all rows.
        if (tableName == "ingresos")
            model->setSort(0, Qt::DescendingOrder);
        else
            model->setSort(0, Qt::AscendingOrder);
        model->select();
        proxyModel = new MySortFilterProxyModel(this);
        proxyModel->table_name = tableName;
        proxyModel->setSourceModel(model);
        table_listado->setModel(proxyModel);
        if (tableName != "ingresos") {
            // Force-load all rows for smaller tables via fetchMore.
            while (model->canFetchMore(QModelIndex()))
                model->fetchMore(QModelIndex());
        }
        QScrollBar *verticalScrollBar = table_listado->verticalScrollBar();
        verticalScrollBar->setValue(verticalScrollBar->minimum());
        // Perform sorting with proxy model
        if (tableName == "gastos")
            table_listado->sortByColumn(GASTOS_IDX_FECHA, Qt::DescendingOrder);
        else if (tableName == "ingresos")
            table_listado->sortByColumn(INGRESOS_COL_N_RECIBO, Qt::DescendingOrder);
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
        if (tableName == "ingresos") {
            table_listado->setItemDelegateForColumn(INGRESOS_COL_IMPORTE, new NumberFormatDelegate(this));
            table_listado->setItemDelegateForColumn(INGRESOS_COL_PAGADO, new TextColorDelegate(table_listado, this));
            table_listado->setItemDelegateForColumn(INGRESOS_COL_ESTADO, new TextColorDelegate(table_listado, this));
            table_listado->setItemDelegateForColumn(INGRESOS_COL_VERIFACTU_URL_QR, new LinkDelegate(this));
            table_listado->horizontalHeader()->moveSection(INGRESOS_COL_VERIFACTU_URL_QR, INGRESOS_COL_VERIFACTU_ERROR);
            placeIngresosDateColumns(table_listado->horizontalHeader());
            // hash is not useful at user interface.
            table_listado->setColumnHidden(INGRESOS_COL_HASH, true);
            // edit_lock is an internal flag (set by Contabilidad on quarter close); not user-facing.
            table_listado->setColumnHidden(INGRESOS_COL_EDIT_LOCK, true);
            // verifactu_csv is redundant with the clickable verifactu_url_qr link in this view.
            table_listado->setColumnHidden(INGRESOS_COL_VERIFACTU_CSV, true);
            // Hide raw XML column - exported via Herramientas > Exportar registros AEAT (XML), not viewed inline
            table_listado->setColumnHidden(INGRESOS_COL_VERIFACTU_XML, true);
            table_listado->setColumnHidden(INGRESOS_COL_VERIFACTU_CANCEL_XML, true);
            // Hide chained hash (AEAT "Huella") - 64-char hex, not useful inline
            table_listado->setColumnHidden(INGRESOS_COL_VERIFACTU_HASH, true);
            table_listado->setColumnHidden(INGRESOS_COL_VERIFACTU_INVOICE_SEQ, true);
            table_listado->setColumnHidden(INGRESOS_COL_VERIFACTU_INVOICE_ID, true);
            // Verifactu integrity (Art. 8.1 RD 1007/2023): no inline edits on submitted
            // records. All ingresos changes must flow through RecogPrendas / Cancel /
            // Rectify which keep AEAT, the chained hash and the accounting lock in sync.
            table_listado->setEditTriggers(QAbstractItemView::NoEditTriggers);
        }
        // Resize table
        table_listado->resizeColumnsToContents();
        table_listado->resizeRowsToContents();
        // Lock row height so lazily-loaded rows use the same compact height.
        if (model->rowCount() > 0)
            table_listado->verticalHeader()->setDefaultSectionSize(table_listado->rowHeight(0));
    }
    resizeWindowToTable();

    // Restore the cursor to default
    QApplication::restoreOverrideCursor();
}

void Listado::resizeWindowToTable()
{
    int size = 0;
    for (int column = 0; column < table_listado->model()->columnCount(); column++) {
        size += table_listado->columnWidth(column);
    }
    int desired = size + 40;
    int maxWidth = screen()->availableGeometry().width();
    if (this->width() < desired)
        this->resize(qMin(desired, maxWidth), this->height());
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
        table_listado->sortByColumn(INGRESOS_COL_N_RECIBO, Qt::DescendingOrder);
    else
        table_listado->sortByColumn(LIST_PRENDAS_IDX_NAME, Qt::AscendingOrder);
    table_listado->resizeColumnsToContents();
    table_listado->resizeRowsToContents();
    resizeWindowToTable();
}

void Listado::on_actionAnadir_fila_triggered()
{
    if (tableName == "clientes") {
        InsertNewItem form(db, this);
        if (form.exec() != QDialog::Accepted)
            return;
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
        m_lblResult->setText(UiKit::warnHtml("Los gastos nuevos se añaden con Herramientas → Formulario facturas "
                                             "en la ventana principal."));
        return;
    } else {
        qWarning() << "Listado::on_actionAnadir_fila_triggered: '" << tableName << "' does not support adding rows";
        m_lblResult->setText(UiKit::warnHtml("Este listado no admite añadir filas directamente."));
        return;
    }
    m_lblResult->setText(tableName == "clientes" ? UiKit::okHtml("Cliente añadido.")
                         : UiKit::okHtml("Fila añadida.") + " Rellénela con doble clic en cada celda.");
}

void Listado::on_actionEliminar_fila_triggered()
{
    if (tableName == "ingresos") {
        qWarning() << "Listado::on_actionEliminar_fila_triggered: ingresos rows cannot be deleted here";
        m_lblResult->setText(UiKit::warnHtml("Los ingresos no se eliminan desde el listado: use Anular prendas "
                                             "o Anular factura."));
    } else if (!table_listado->currentIndex().isValid()) {
        m_lblResult->setText(UiKit::warnHtml("Seleccione primero la fila que quiere eliminar."));
    } else if (proxyModel->rowLocked(table_listado->currentIndex().row())) {
        qWarning() << "Listado::on_actionEliminar_fila_triggered: refused deleting a locked" << tableName << "row";
        m_lblResult->setText(UiKit::errorHtml("Fila bloqueada.")
                             + "<br>Pertenece a un trimestre cerrado por la contabilidad; revierta la "
                               "contabilidad para poder eliminarla.");
    } else {
        int ret = QMessageBox::question(this, "Eliminar fila",
                                        "¿Está seguro que desea eliminar la fila " +
                                        QString::number(table_listado->currentIndex().row() + 1) + "?",
                                        QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            table_listado->model()->removeRow(table_listado->currentIndex().row());
            populateTable();
            m_lblResult->setText(UiKit::okHtml("Fila eliminada."));
        }
    }
}

void Listado::on_actionGenerar_pdf_con_el_listado_triggered()
{
    if (tableName == "gastos") {
        GenListado *ui_generar_listado;
        ui_generar_listado = new GenListado(db, this);
        ui_generar_listado->model = table_listado->model();
        ui_generar_listado->exec();
        populateTable();
    } else if (tableName == "prendas") {
            GenListado *ui_generar_listado;
            ui_generar_listado = new GenListado(db, this);
            ui_generar_listado->model = table_listado->model();
            ui_generar_listado->table_name = tableName;
            ui_generar_listado->print_table();
            m_lblResult->setText(UiKit::okHtml("Listado de prendas generado en PDF."));
    } else {
        qDebug() << "Listado::on_actionGenerar_pdf_con_el_listado_triggered: no PDF listing for" << tableName;
        m_lblResult->setText(UiKit::warnHtml("Este listado no tiene versión en PDF."));
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
                m_lblResult->setText(UiKit::errorHtml("Edición bloqueada.")
                                     + "<br>La fila pertenece a un trimestre cerrado por la contabilidad; "
                                       "revierta la contabilidad para poder editarla.");
                // Deselect the current cell
                QItemSelectionModel *selectionModel = table_listado->selectionModel();
                selectionModel->clearSelection();
            }
        }
    }
}
