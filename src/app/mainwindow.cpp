#include "mainwindow.h"
#include "sql_lite.h"
#include "verifactutypes.h"
#include "verifactucoordinator.h"
#include "cancelinvoicedialog.h"
#include "rectifyinvoicedialog.h"
#include <QMessageBox>
#include "applogger.h"
#include <QDateTime>
#include <QFileInfo>
#include <QUrl>
#include <QScopeGuard>
#include "listado.h"
#include "recog_prendas.h"
#include "imprimir.h"
#include "contabilidad.h"
#include "facturas.h"
#include "add_garment.h"
#include "appsettings.h"
#include "applanguage.h"
#include "settingsdialog.h"
#include "verifactumanager.h"
#include "verifactuconfig.h"
#include "verifacturesponse.h"
#include "updaterdialog.h"
#include "pendingsubmitsdialog.h"
#include "refusedinvoicesdialog.h"
#include "aeatexport.h"
#include "aeatexportdialog.h"
#include "aeatcertificate.h"
#include "aeatselftest.h"
#include "aeatconnectionprobe.h"
#include "producerdeclaration.h"
#include "selfcheck.h"
#include "reporthtml.h"
#include "uikit.h"
#include <QTimer>
#include <QThread>
#include <QEventLoop>
#include <QTextEdit>
#include <QTextCursor>
#include <QFile>
#include <QStatusBar>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDateEdit>
#include <QDir>
#include <QProcess>
#include <QFileDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeySequence>
#include <QPointer>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QTableWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Widgets)
{
    buildUi();
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(dbPath());
    db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=15000");   // a save waits out a backup's read
    buildCoordinator();
    mainwindowInitialSettings();
    initializeVerifactu();
    if (!m_updateNotice.isEmpty())
        appendToResult(m_updateNotice);

    buildStatusBar();
    updateAeatEnvironment();
    updateUnsentRecords();
    updateRefusedInvoices();
    // Refreshed regularly for the invoices settled in other windows.
    auto *refusedTimer = new QTimer(this);
    connect(refusedTimer, &QTimer::timeout, this, &MainWindow::updateRefusedInvoices);
    refusedTimer->start(30000);

    m_updater = new Updater(this);
    connect(m_updater, &Updater::updateAvailable,    this, &MainWindow::onUpdateAvailable);
    connect(m_updater, &Updater::noUpdateAvailable,  this, &MainWindow::onUpdaterNoUpdateAvailable);
    connect(m_updater, &Updater::checkFailed,        this, &MainWindow::onUpdaterCheckFailed);

    // Startup auto-check (silent on no-update / failure). Deferred so the
    // main window is fully shown before the dialog can pop up over it.
    if (AppSettings::instance()->checkUpdatesOnStartup()) {
        QTimer::singleShot(1500, this, [this]() {
            m_updater->checkForUpdates(/*silentOnNoUpdate=*/true);
        });
    }

    // Startup recovery for Verifactu submissions that were in flight when the
    // app last closed (verifactu_estado='PENDIENTE' with no matching reqId in
    // memory after a restart). The dialog scans for surviving PENDIENTE rows
    // and lets the operator decide per ticket; deferred 4s so it lands AFTER
    // the backup/update prompts have settled.
    QTimer::singleShot(4000, this, &MainWindow::showPendingSubmits);

    // Verifactu Req. 4: trigger a backup at startup once per 24h. Deferred so
    // it doesn't block the first paint of the window. Silent on success - the
    // operator only sees a message if the backup fails (since the regulatory
    // requirement is durable storage, a failed snapshot is worth surfacing).
    if (m_backupManager->needsBackup()) {
        QTimer::singleShot(3000, this, [this]() {
            // performBackup() does VACUUM INTO + integrity_check (seconds on a
            // large DB) and uses its own dedicated DB connections, so run it on
            // a worker thread to keep the UI responsive; marshal the Result back
            // to the GUI thread (Qt::QueuedConnection on `this`) for the message.
            m_backupManager->setSecondFolder(AppSettings::instance()->backupSecondFolder());
            QThread *worker = QThread::create([this]() {
                const BackupManager::Result res = m_backupManager->performBackup();
                QMetaObject::invokeMethod(this, [this, res]() {
                    BackupManager::recordSuccess(res);   // settings are written on the GUI thread
                    BackupManager::recordFailure(res);
                    if (res.skipped)
                        return;                          // another run is making the copy: not a failure
                    if (!res.success) {
                        m_result->setText(UiKit::errorHtml(tr("No se pudo crear la copia de seguridad automática."))
                                          + "<br>" + res.errorMessage.toHtmlEscaped());
                    } else if (!res.secondCopyError.isEmpty()) {
                        m_result->setText(UiKit::warnHtml(res.secondCopyError.toHtmlEscaped()));
                    } else {
                        statusBar()->showMessage(
                            tr("Copia de seguridad creada (%1).").arg(res.backupPath), 8000);
                    }
                }, Qt::QueuedConnection);
            });
            connect(worker, &QThread::finished, worker, &QObject::deleteLater);
            m_backupThread = worker;
            worker->start();
        });
    }
}

void MainWindow::buildCoordinator()
{
    m_coordinator = new VerifactuCoordinator(db, this);
    connect(m_coordinator, &VerifactuCoordinator::statusMessage, this,
            [this](const QString &text, int timeoutMs) { statusBar()->showMessage(text, timeoutMs); });
    connect(m_coordinator, &VerifactuCoordinator::submitRefused, this, &MainWindow::showVerifactuRejection);
    connect(m_coordinator, &VerifactuCoordinator::submitSettled, this, &MainWindow::updateRefusedInvoices);
    connect(m_coordinator, &VerifactuCoordinator::recordSettled, this, &MainWindow::onRecordSettled);
    connect(m_coordinator, &VerifactuCoordinator::unsentRecordsChanged, this, &MainWindow::updateUnsentRecords);
    connect(m_coordinator, &VerifactuCoordinator::chainChecked, this, &MainWindow::onChainChecked);
    // Limpiar, the menus and the edits would change the ticket being printed.
    connect(m_coordinator, &VerifactuCoordinator::aeatWaitStarted, this, [this] {
        m_menuWasEnabled = menuBar()->isEnabled();
        centralWidget()->setEnabled(false);
        menuBar()->setEnabled(false);
    });
    connect(m_coordinator, &VerifactuCoordinator::aeatWaitFinished, this, [this] {
        centralWidget()->setEnabled(true);
        menuBar()->setEnabled(m_menuWasEnabled);
    });
}

VerifactuIntegration *MainWindow::integration() const
{
    return m_coordinator ? m_coordinator->integration() : nullptr;
}

void MainWindow::buildStatusBar()
{
    // Refused invoices stay in view until resent (the customer may hold their factura):
    // refreshed on every AEAT outcome.
    m_refusedInvoices = new QLabel(this);
    m_refusedInvoices->setObjectName("lblRefusedInvoices");
    m_refusedInvoices->setToolTip(tr("Pulse para ver cuáles son y abrirlas en Recogida de Prendas"));
    connect(m_refusedInvoices, &QLabel::linkActivated, this, &MainWindow::showRefusedInvoices);
    m_aeatEnvironment = new QLabel(this);
    m_aeatEnvironment->setObjectName("lblAeatEnvironment");
    m_unsentRecords = new QLabel(this);
    m_unsentRecords->setObjectName("lblUnsentRecords");
    m_unsentRecords->setToolTip(tr("Un fallo de conexión o del certificado ha impedido enviarlos. Están guardados: "
                                   "si es la conexión, el envío se reintenta solo cada minuto; si es el "
                                   "certificado, se envían al corregirlo en Archivo \u2192 Configuración."));
    for (QLabel *label : { m_refusedInvoices, m_aeatEnvironment, m_unsentRecords })
        statusBar()->addPermanentWidget(label);
}

void MainWindow::showPendingSubmits()
{
    // Not over Configuración or the updater: try again shortly.
    if (QApplication::activeModalWidget()) {
        QTimer::singleShot(2000, this, &MainWindow::showPendingSubmits);
        return;
    }
    auto *dlg = new PendingSubmitsDialog(db, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->inFlight = [this](const QString &invoiceId) {
        return integration() && integration()->isInFlight(invoiceId);
    };
    if (!dlg->loadPending()) {
        dlg->deleteLater();
        return;
    }
    dlg->retry = [this](const QString &ticketNum, int seq, const QDate &invoiceDate, double totalAmount) {
        if (!integration())
            return tr("Verifactu no está disponible.");
        if (const QString blocked = integration()->numberingBlockedReason(); !blocked.isEmpty())
            return blocked;
        if (integration()->isInFlight(verifactuInvoiceId(ticketNum, seq)))
            return tr("Esta factura se está enviando ahora mismo: espere a la respuesta de la AEAT.");
        const QString reqId = m_coordinator->submitInvoice(ticketNum, invoiceDate, totalAmount, seq);
        return reqId.isEmpty() ? integration()->getLastError() : QString();
    };
    dlg->open();
}

MainWindow::~MainWindow()
{
    // Closing during the automatic backup waits for it: a cut-off copy is only a .part file,
    // but the thread must not outlive the window it reports to.
    if (m_backupThread)
        m_backupThread->wait();
    delete ui;
}

void MainWindow::buildUi()
{
    setObjectName("MainWindow");
    setWindowTitle("La Ideal");
    setLocale(QLocale(QLocale::Spanish, QLocale::Spain));
    resize(1240, 760);

    // Actions (names as in the former .ui: the on_action<Name>_triggered slots connect by name).
    QAction *actionRecibo = new QAction(tr("Recibo..."), this);
    actionRecibo->setObjectName("actionRecibo");
    actionRecibo->setToolTip(tr("Imprimir recibo"));
    actionRecibo->setShortcut(QKeySequence("Alt+P, R"));
    QAction *actionFactura = new QAction(tr("Factura..."), this);
    actionFactura->setObjectName("actionFactura");
    actionFactura->setToolTip(tr("Imprimir factura"));
    actionFactura->setShortcut(QKeySequence("Alt+P, F"));
    QAction *actionRecogida_de_prendas = new QAction(tr("Recogida de prendas..."), this);
    actionRecogida_de_prendas->setObjectName("actionRecogida_de_prendas");
    actionRecogida_de_prendas->setShortcut(QKeySequence("Alt+R"));
    QAction *actionFormulario_facturas = new QAction(tr("Añadir factura de gastos..."), this);
    actionFormulario_facturas->setObjectName("actionFormulario_facturas");
    actionFormulario_facturas->setShortcut(QKeySequence("Alt+F"));
    QAction *actionGenerar_contabilidad = new QAction(tr("Generar contabilidad..."), this);
    actionGenerar_contabilidad->setObjectName("actionGenerar_contabilidad");
    actionGenerar_contabilidad->setShortcut(QKeySequence("Alt+G"));
    QAction *actionIngresos = new QAction(tr("Ingresos"), this);
    actionIngresos->setObjectName("actionIngresos");
    actionIngresos->setToolTip(tr("Visualizar tabla ingresos"));
    actionIngresos->setShortcut(QKeySequence("Alt+Shift+I"));
    QAction *actionGastos = new QAction(tr("Gastos"), this);
    actionGastos->setObjectName("actionGastos");
    actionGastos->setToolTip(tr("Visualizar tabla gastos"));
    actionGastos->setShortcut(QKeySequence("Alt+Shift+G"));
    QAction *actionListado_de_prendas = new QAction(tr("Listado de prendas"), this);
    actionListado_de_prendas->setObjectName("actionListado_de_prendas");
    actionListado_de_prendas->setToolTip(tr("Visualizar listado de prendas"));
    actionListado_de_prendas->setShortcut(QKeySequence("Alt+Shift+P"));
    QAction *actionListado_de_clientes = new QAction(tr("Listado de clientes"), this);
    actionListado_de_clientes->setObjectName("actionListado_de_clientes");
    actionListado_de_clientes->setToolTip(tr("Visualizar listado de clientes"));
    actionListado_de_clientes->setShortcut(QKeySequence("Alt+Shift+C"));
    QAction *actionListado_de_proveedores = new QAction(tr("Listado de proveedores"), this);
    actionListado_de_proveedores->setObjectName("actionListado_de_proveedores");
    actionListado_de_proveedores->setToolTip(tr("Visualizar listado de proveedores"));
    actionListado_de_proveedores->setShortcut(QKeySequence("Alt+Shift+O"));
    QAction *actionListado_de_servicios = new QAction(tr("Listado de servicios"), this);
    actionListado_de_servicios->setObjectName("actionListado_de_servicios");
    actionListado_de_servicios->setToolTip(tr("Visualizar listado de servicios"));
    actionListado_de_servicios->setShortcut(QKeySequence("Alt+Shift+S"));
    QAction *actionFactura_completa = new QAction(tr("Factura completa..."), this);
    actionFactura_completa->setObjectName("actionFactura_completa");
    actionFactura_completa->setToolTip(tr("Imprimir factura completa"));
    actionFactura_completa->setShortcut(QKeySequence("Alt+P, Alt+F"));
    QAction *actionCerrar = new QAction(tr("Cerrar"), this);
    actionCerrar->setObjectName("actionCerrar");
    actionCerrar->setToolTip(tr("Cerrar aplicación"));
    actionCerrar->setShortcut(QKeySequence("Alt+C"));
    // No shortcut: reopening a closed quarter is never one key press away.
    QAction *actionRevertir_contabilidad = new QAction(tr("Revertir contabilidad..."), this);
    actionRevertir_contabilidad->setObjectName("actionRevertir_contabilidad");
    QAction *actionAnadir_nuevas_prendas = new QAction(tr("Añadir nuevas prendas..."), this);
    actionAnadir_nuevas_prendas->setObjectName("actionAnadir_nuevas_prendas");
    actionAnadir_nuevas_prendas->setToolTip(tr("Añadir nuevas prendas a un ticket guardado"));
    actionAnadir_nuevas_prendas->setShortcut(QKeySequence("Alt+Shift+N"));
    QAction *actionAnular_factura_verifactu = new QAction(tr("Anular factura Verifactu..."), this);
    actionAnular_factura_verifactu->setObjectName("actionAnular_factura_verifactu");
    actionAnular_factura_verifactu->setToolTip(tr("Anular una factura previamente enviada a la AEAT a través de Verifactu"));
    actionAnular_factura_verifactu->setShortcut(QKeySequence("Alt+A"));
    QAction *actionRectificar_factura_verifactu = new QAction(tr("Rectificar factura Verifactu..."), this);
    actionRectificar_factura_verifactu->setObjectName("actionRectificar_factura_verifactu");
    actionRectificar_factura_verifactu->setToolTip(tr("Emitir una factura rectificativa (R1-R5) de una factura previamente enviada a la AEAT (Art. 8.2.a RD 1007/2023)"));
    actionRectificar_factura_verifactu->setShortcut(QKeySequence("Alt+T"));
    QAction *actionExportar_registros_aeat = new QAction(tr("Exportar registros AEAT (XML)..."), this);
    actionExportar_registros_aeat->setObjectName("actionExportar_registros_aeat");
    actionExportar_registros_aeat->setToolTip(tr("Exporta los XML AEAT de los tickets de un rango de fechas a un único archivo (Art. 14.1 RD 1007/2023)"));
    QAction *actionMostrar_log = new QAction(tr("Log de depuración"), this);
    actionMostrar_log->setObjectName("actionMostrar_log");
    actionMostrar_log->setToolTip(tr("Muestra la ubicación del archivo de log para enviar al soporte técnico"));
    QAction *actionAcerca_de_Verifactu = new QAction(tr("Acerca de Verifactu"), this);
    actionAcerca_de_Verifactu->setObjectName("actionAcerca_de_Verifactu");
    actionAcerca_de_Verifactu->setToolTip(tr("Declaración responsable del productor del sistema informático "
                                              "(art. 13 RD 1007/2023, art. 15 Orden HAC/1177/2024)"));
    QAction *actionHacer_copia_de_seguridad = new QAction(tr("Hacer copia de seguridad ahora"), this);
    actionHacer_copia_de_seguridad->setObjectName("actionHacer_copia_de_seguridad");
    actionHacer_copia_de_seguridad->setToolTip(tr("Crea una copia íntegra de la base de datos en la carpeta de copias (Art. 8.2.c RD 1007/2023)"));
    QAction *actionBuscar_actualizaciones = new QAction(tr("Buscar actualizaciones"), this);
    actionBuscar_actualizaciones->setObjectName("actionBuscar_actualizaciones");
    actionBuscar_actualizaciones->setToolTip(tr("Comprueba en GitHub si hay una versión más reciente y la instala"));
    QAction *actionComprobar_tras_actualizar = new QAction(tr("Comprobar tras actualizar..."), this);
    actionComprobar_tras_actualizar->setObjectName("actionComprobar_tras_actualizar");
    actionComprobar_tras_actualizar->setToolTip(tr("Comprueba esta versión con una copia de sus datos y su impresora, y guarda un informe para enviarlo"));
    QAction *actionNotas_de_la_version = new QAction(tr("Notas de la versión"), this);
    actionNotas_de_la_version->setObjectName("actionNotas_de_la_version");
    actionNotas_de_la_version->setToolTip(tr("Muestra el historial de cambios de todas las versiones publicadas"));

    QAction *actionConfiguracion = new QAction(tr("Configuración..."), this);
    actionConfiguracion->setObjectName("actionConfiguracion");
    actionConfiguracion->setToolTip(tr("Datos del negocio, base de datos, impresora y conexión con la AEAT"));

    // Menus, one per kind of work. The UI smoke test opens them by their visible text.
    // "..." marks an entry that opens a window to fill in or choose from before anything
    // happens; one that acts at once or only shows something has none.
    const auto addMenu = [this](const QString &title, const char *name) {
        QMenu *menu = menuBar()->addMenu(title);
        menu->setObjectName(name);
        menu->setToolTipsVisible(true);
        return menu;
    };
    QMenu *menuArchivo = addMenu(tr("Archivo"), "menuArchivo");
    menuArchivo->addAction(actionConfiguracion);
    menuArchivo->addAction(actionHacer_copia_de_seguridad);
    menuArchivo->addSeparator();
    menuArchivo->addAction(actionCerrar);
    QMenu *menuTickets = addMenu(tr("Tickets"), "menuTickets");
    menuTickets->addAction(actionRecogida_de_prendas);
    menuTickets->addAction(actionAnadir_nuevas_prendas);
    QMenu *menuImprimir_ticket = menuTickets->addMenu(tr("Imprimir"));
    menuImprimir_ticket->setObjectName("menuImprimir_ticket");
    menuImprimir_ticket->setToolTipsVisible(true);
    menuImprimir_ticket->addAction(actionRecibo);
    menuImprimir_ticket->addAction(actionFactura);
    menuImprimir_ticket->addAction(actionFactura_completa);
    QMenu *menuFacturas = addMenu(tr("Facturas"), "menuFacturas");
    menuFacturas->addAction(actionAnular_factura_verifactu);
    menuFacturas->addAction(actionRectificar_factura_verifactu);
    menuFacturas->addSeparator();
    menuFacturas->addAction(actionExportar_registros_aeat);
    QMenu *menuContabilidad = addMenu(tr("Contabilidad"), "menuContabilidad");
    menuContabilidad->addAction(actionFormulario_facturas);
    menuContabilidad->addSeparator();
    menuContabilidad->addAction(actionGenerar_contabilidad);
    menuContabilidad->addAction(actionRevertir_contabilidad);
    QMenu *menuVisualizar = addMenu(tr("Ver"), "menuVisualizar");
    menuVisualizar->addAction(actionIngresos);
    menuVisualizar->addAction(actionGastos);
    menuVisualizar->addSeparator();
    menuVisualizar->addAction(actionListado_de_prendas);
    menuVisualizar->addAction(actionListado_de_clientes);
    menuVisualizar->addAction(actionListado_de_proveedores);
    menuVisualizar->addAction(actionListado_de_servicios);
    QMenu *menuAyuda = addMenu(tr("Ayuda"), "menuAyuda");
    menuAyuda->addAction(actionBuscar_actualizaciones);
    menuAyuda->addAction(actionNotas_de_la_version);
    menuAyuda->addAction(actionComprobar_tras_actualizar);
    menuAyuda->addAction(actionMostrar_log);
    menuAyuda->addSeparator();
    menuAyuda->addAction(actionAcerca_de_Verifactu);

    ui->actionConfiguracion = actionConfiguracion;

    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);

    // Heading, explanation and the windows used most, one click away -------------
    QHBoxLayout *top = new QHBoxLayout();
    ui->lbl_title = UiKit::heading(QString());
    refreshTitle();
    top->addWidget(ui->lbl_title);
    top->addStretch();
    const auto quick = [this, top](QAction *action, const QString &text, const QString &name) {
        QPushButton *button = UiKit::secondaryButton(text, name);
        button->setToolTip(action->toolTip());
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        top->addWidget(button);
    };
    quick(actionRecogida_de_prendas, tr("Recogida de prendas"), "pb_quick_recogida");
    quick(actionAnadir_nuevas_prendas, tr("Añadir prendas"), "pb_quick_add");
    quick(actionFormulario_facturas, tr("Factura de gastos"), "pb_quick_gastos");
    layout->addLayout(top);
    layout->addWidget(UiKit::introPanel(tr(
        "Nuevo ticket: elija o escriba el cliente, añada las prendas y pulse \"Guardar ticket\". "
        "Se imprime el recibo; si el ticket queda pagado, se envía a AEAT y se imprime la factura.")));

    // Client and ticket ----------------------------------------------------------
    QHBoxLayout *head = new QHBoxLayout();
    QGroupBox *grpClient = new QGroupBox(tr("Cliente"));
    QGridLayout *c = new QGridLayout(grpClient);
    ui->cb_client = new QComboBox();
    ui->cb_client->setObjectName("cb_client");
    ui->cb_client->setEditable(true);
    ui->cb_client->setInsertPolicy(QComboBox::NoInsert);
    ui->cb_client->setToolTip(tr("Elija un cliente del listado o escriba uno nuevo: se añade al guardar."));
    ui->le_phone = new QLineEdit();
    ui->le_phone->setObjectName("le_phone");
    ui->le_mobile = new QLineEdit();
    ui->le_mobile->setObjectName("le_mobile");
    ui->le_addr = new QLineEdit();
    ui->le_addr->setObjectName("le_addr");
    c->addWidget(new QLabel(tr("Cliente:")), 0, 0);
    c->addWidget(ui->cb_client, 0, 1, 1, 3);
    c->addWidget(new QLabel(tr("Teléfono:")), 1, 0);
    c->addWidget(ui->le_phone, 1, 1);
    c->addWidget(new QLabel(tr("Móvil:")), 1, 2);
    c->addWidget(ui->le_mobile, 1, 3);
    c->addWidget(new QLabel(tr("Dirección:")), 2, 0);
    c->addWidget(ui->le_addr, 2, 1, 1, 3);
    c->setColumnStretch(1, 1);
    c->setColumnStretch(3, 1);
    head->addWidget(grpClient, 3);

    QGroupBox *grpTicket = new QGroupBox(tr("Ticket"));
    QGridLayout *t = new QGridLayout(grpTicket);
    ui->le_nr_ticket = new QLineEdit();
    ui->le_nr_ticket->setObjectName("le_nr_ticket");
    ui->le_nr_ticket->setReadOnly(true);
    ui->le_nr_ticket->setToolTip(tr("El siguiente número libre; se asigna solo."));
    UiKit::emphasize(ui->le_nr_ticket, 3);
    ui->le_nr_ticket->setMaximumWidth(160);
    ui->de_date_recep = UiKit::dateEdit(QDate::currentDate(), "de_date_recep");
    ui->de_date_recep->setMaximumDate(QDate::currentDate());   // a paid ticket's invoice date
    ui->de_date_recep->setToolTip(tr("Fecha de recepción de las prendas."));
    ui->pb_payment = new QCheckBox(tr("Pagado al dejarlo"));
    ui->pb_payment->setObjectName("pb_payment");
    ui->pb_payment->setToolTip(tr("Márquelo si el cliente paga ahora: el ticket se envía a AEAT al guardar."));
    ui->lbl_payment_badge = new QLabel();
    ui->lbl_payment_badge->setObjectName("lbl_payment_badge");
    t->addWidget(new QLabel(tr("Nº recibo:")), 0, 0);
    t->addWidget(ui->le_nr_ticket, 0, 1);
    t->addWidget(new QLabel(tr("Recepción:")), 1, 0);
    t->addWidget(ui->de_date_recep, 1, 1);
    QHBoxLayout *paid = new QHBoxLayout();
    paid->addWidget(ui->pb_payment);
    paid->addWidget(ui->lbl_payment_badge);
    paid->addStretch();
    t->addLayout(paid, 2, 0, 1, 3);
    t->setColumnStretch(3, 1);
    head->addWidget(grpTicket, 2);
    layout->addLayout(head);

    // Garments -------------------------------------------------------------------
    QGroupBox *grpGarments = new QGroupBox(tr("Prendas"));
    QVBoxLayout *g = new QVBoxLayout(grpGarments);
    ui->table_ticket = new QTableWidget(kInitialTicketRows, 6);
    ui->table_ticket->setObjectName("table_ticket");
    ui->table_ticket->setHorizontalHeaderLabels(
        { tr("Cant."), tr("Prenda"), tr("Tamaño (m2)"), tr("Serv."), tr("Observaciones"), tr("Importe") });
    ui->table_ticket->setAlternatingRowColors(true);
    ui->table_ticket->setToolTip(tr("Cantidad y prenda calculan el importe; el importe se puede cambiar a mano."));
    g->addWidget(ui->table_ticket, 1);
    QHBoxLayout *garmentFooter = new QHBoxLayout();
    ui->pb_add_row = UiKit::secondaryButton(tr("Añadir fila"), "pb_add_row");
    garmentFooter->addWidget(ui->pb_add_row);
    garmentFooter->addStretch();
    QLabel *lblTotal = new QLabel(tr("Importe total:"));
    UiKit::emphasize(lblTotal, 3);
    garmentFooter->addWidget(lblTotal);
    ui->le_cost_total = new QLineEdit();
    ui->le_cost_total->setObjectName("le_cost_total");
    ui->le_cost_total->setReadOnly(true);   // always the sum of the rows
    UiKit::emphasize(ui->le_cost_total, 3);
    ui->le_cost_total->setAlignment(Qt::AlignRight);
    ui->le_cost_total->setMaximumWidth(150);
    garmentFooter->addWidget(ui->le_cost_total);
    garmentFooter->addWidget(new QLabel(QStringLiteral("€")));
    g->addLayout(garmentFooter);
    layout->addWidget(grpGarments, 1);

    // Actions + result -------------------------------------------------------------
    QHBoxLayout *actions = new QHBoxLayout();
    ui->pb_reset = UiKit::secondaryButton(tr("Limpiar"), "pb_reset");
    ui->pb_reset->setToolTip(tr("Borra el ticket en curso sin guardarlo."));
    actions->addWidget(ui->pb_reset);
    actions->addStretch();
    ui->pb_save = UiKit::primaryButton(tr("Guardar ticket"), "pb_save");
    ui->pb_save->setToolTip(tr("Guarda el ticket e imprime el recibo (y la factura si está pagado)."));
    actions->addWidget(ui->pb_save);
    layout->addLayout(actions);
    m_result = new UiKit::ResultPanel();
    m_result->setMinimumHeight(44);
    layout->addWidget(m_result);
    setCentralWidget(central);
    statusBar();   // AEAT replies, backups

    QMetaObject::connectSlotsByName(this);
}

void MainWindow::mainwindowInitialSettings()
{
    QStringList startProblems;   // HTML lines for the result panel
    m_backupManager = new BackupManager(this);
    // A new version migrates the database: keep a copy of it as it was first, also with
    // automatic backups switched off (the data repairs never run without one).
    const QString version = Updater::currentVersion();
    // The version that ran before this start; without a recorded one, the version of the
    // pre-migration copy (read before this start's copy moves it on).
    const QString previousVersion = AppSettings::instance()->lastStartedVersion().isEmpty()
        ? AppSettings::instance()->backupLastVersion() : AppSettings::instance()->lastStartedVersion();
    bool rewriteData = true;   // false: no copy of the database as the previous version left it
    if (AppSettings::instance()->backupLastVersion() != version && QFile::exists(dbPath())) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_backupManager->setSecondFolder(AppSettings::instance()->backupSecondFolder());
        const BackupManager::Result res = m_backupManager->performBackup();
        QApplication::restoreOverrideCursor();
        if (!res.secondCopyError.isEmpty())
            startProblems << UiKit::warnHtml(res.secondCopyError.toHtmlEscaped());
        if (res.success) {
            AppSettings::instance()->setBackupLastVersion(version);
            BackupManager::recordSuccess(res);   // saves the settings
        } else if (res.skipReason != BackupManager::SkipReason::SameSecond) {   // same second: a copy of this start exists
            BackupManager::recordFailure(res);   // records nothing for a run that is still going
            rewriteData = false;
            startProblems << UiKit::errorHtml(tr("No se pudo hacer la copia de seguridad previa a la actualización: %1")
                                                  .arg(res.errorMessage).toHtmlEscaped())
                             + "<br>" + tr("Las reparaciones de datos de esta versión esperan a que la copia se haga "
                                           "(se reintenta al iniciar de nuevo).");
        }
    }
    if (!migrateDatabase(db, rewriteData))
        startProblems << UiKit::errorHtml(tr("La base de datos no se ha actualizado del todo para esta versión. "
                                             "Avise al servicio técnico antes de seguir (los detalles están en el registro)."));
    // Once per update: ask for the post-update check and its report (added after the
    // Verifactu start-up messages, which replace the panel).
    if (SelfCheck::isFirstStartAfterUpdate(previousVersion, version))
        m_updateNotice = UiKit::warnHtml(tr("La Ideal se ha actualizado a la versión %1.").arg(version))
                         + "<br>" + tr("Ejecute <b>Ayuda \u2192 Comprobar tras actualizar\u2026</b> (o Inicio \u2192 La Ideal - Comprobar "
                                       "tras actualizar) y envíe el informe que guarda en el escritorio.");
    if (AppSettings::instance()->lastStartedVersion() != version) {
        AppSettings::instance()->setLastStartedVersion(version);
        AppSettings::instance()->save();
    }
    // Req. 4: backups switched off, or none correct for days, stay visible at every start.
    if (const QString backup = BackupManager::currentBackupWarning(); !backup.isEmpty())
        startProblems << UiKit::warnHtml(backup.toHtmlEscaped());

    connect(ui->actionConfiguracion, &QAction::triggered, this, &MainWindow::openSettings);

    // Table settings
    ui->table_ticket->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->table_ticket->verticalHeader()->setVisible(false);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_QNTY, 60);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_GARM, 400);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_SERV, 70);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_SIZE, 110);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_OBSE, 260);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_PRIC, 120);
    // The garment name takes the spare width; the amounts keep a fixed column.
    ui->table_ticket->horizontalHeader()->setSectionResizeMode(TABLE_TICKET_GARM, QHeaderView::Stretch);
    resetAllContents();
    if (startProblems.isEmpty())
        m_result->showInfo(tr("Listo para un nuevo ticket."));
    else
        m_result->setText(startProblems.join("<br>"));
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(this);
    QList<QPair<QString, QString>> certificates;
    for (const AeatCertificate::Info &c : AeatCertificate::personalCertificates())
        certificates << qMakePair(tr("%1 (caduca el %2)").arg(c.subject, dbDateText(c.expiry.date())), c.thumbprint);
    dlg.setCertificateChoices(certificates);
    dlg.setDirectPendingCount(aeatPendingRecordCount(db, AppSettings::instance()->aeatDirectProduction()
                                                              ? QStringLiteral("produccion") : QStringLiteral("pruebas")));
    dlg.setProductionRefusedCount(refusedInvoiceCount(db, recoveryFloorIso()));
    connect(&dlg, &SettingsDialog::aeatSelfTestRequested, &dlg,
            [this, &dlg](const QString &nif, const QString &name, const QString &thumbprint,
                         const QString &file, const QString &password) {
        m_coordinator->runAeatSelfTest(&dlg, nif, name, thumbprint, file, password);
    });
    // Before production is accepted: the certificate, and an answer from the real AEAT.
    dlg.setProductionCheck([this, &dlg](const QString &nif, const QString &name, const QString &thumbprint,
                                        const QString &file, const QString &password) {
        return m_coordinator->checkAeatProduction(&dlg, nif, name, thumbprint, file, password);
    });
    connect(&dlg, &SettingsDialog::aeatConnectionTestRequested, &dlg,
            [this, &dlg](const QString &nif, const QString &name, const QString &thumbprint,
                         const QString &file, const QString &password, bool production) {
        m_coordinator->runAeatConnectionTest(&dlg, nif, name, thumbprint, file, password, production);
    });
    connect(&dlg, &SettingsDialog::testConnectionRequested, &dlg,
            [this, &dlg](const QString &nif, const QString &name, const QString &serviceKey, bool production) {
        m_coordinator->testGatewayConnection(&dlg, nif, name, serviceKey, production);
    });
    const AppSettings *settings = AppSettings::instance();
    const bool wasInProduction = settings->verifactuDirectAeat() && settings->aeatDirectProduction();
    if (dlg.exec() == QDialog::Accepted) {
        refreshTitle();
        // Reload Verifactu with potentially new credentials
        initializeVerifactu();
        // Production entered now, confirmed by the owner: its period begins today, also
        // on a database that was in production before and left it.
        if (!wasInProduction && settings->verifactuDirectAeat() && settings->aeatDirectProduction()
            && integration()) {
            integration()->markProductionStarted();
            updateAeatEnvironment();
            updateRefusedInvoices();
        }
    }
}

void MainWindow::refreshTitle()
{
    const QString shop = AppSettings::instance()->businessName();
    ui->lbl_title->setText(shop.isEmpty() ? tr("Tintorería La Ideal") : shop);
}

void MainWindow::appendToResult(const QString &html)
{
    m_result->setText(m_result->text().isEmpty() ? html : m_result->text() + "<br><br>" + html);
}

void MainWindow::initializeVerifactu()
{
    bool first = false;
    if (!m_coordinator->initialize(&first)) {
        qWarning() << "Verifactu initialization failed:" << integration()->getLastError();
        appendToResult(UiKit::warnHtml(tr("Verifactu no está disponible: %1")
                                           .arg(integration()->getLastError().toHtmlEscaped()))
                       + "<br>" + tr("Los tickets se guardan en local; configure Verifactu en Archivo \u2192 Configuración."));
    }

    if (!first) {
        updateAeatEnvironment();
        updateUnsentRecords();
    }
    settlePendingRectifications();
    if (integration()->isDirect() && integration()->isConfigured()) {
        QStringList warnings;   // HTML lines
        if (const QString sendingError = integration()->sendingError(); !sendingError.isEmpty())
            warnings << UiKit::warnHtml(tr("Conexión directa con la AEAT: los registros se guardan pero no se "
                                           "pueden enviar: %1").arg(sendingError.toHtmlEscaped()));
        if (const QString saveError = AppSettings::instance()->lastSaveError(); !saveError.isEmpty())
            warnings << UiKit::errorHtml(tr("No se puede guardar la configuración (%1): el número de instalación "
                                            "de la conexión directa podría cambiar al reiniciar. Avise al servicio técnico.")
                                             .arg(saveError.toHtmlEscaped()));
        if (const QString expiry = integration()->certificateExpiryWarning(); !expiry.isEmpty())
            warnings << UiKit::warnHtml(expiry.toHtmlEscaped());
        QString chainProblem;
        int chainContinuations = 0;
        if (!integration()->verifyStoredChain(&chainProblem, &chainContinuations)) {
            qCritical() << "MainWindow: the stored AEAT record chain does not verify -" << chainProblem;
            warnings << UiKit::errorHtml(tr("La cadena de registros de la AEAT guardada no es correcta: %1")
                                             .arg(chainProblem.toHtmlEscaped()))
                        + "<br>" + tr("Los registros se han modificado fuera de la aplicación. Avise al servicio técnico.");
        } else if (chainContinuations > 0) {
            qWarning() << "MainWindow: the stored chain continues" << chainContinuations
                       << "record(s) not in this database (a restored copy)";
        }
        if (!warnings.isEmpty())
            appendToResult(warnings.join("<br>"));
        continueDirectChain();
    }
}

void MainWindow::onChainChecked(bool ok)
{
    // Any finished chain sync: AEAT may hold later numbers than the one on screen.
    if (!ok || m_saving)
        return;                                                 // a save resets the form itself
    setNextTicketNumber();
    if (m_numberingRefused)
        m_result->setText(UiKit::okHtml(tr("Conexión directa con la AEAT: cadena comprobada, ya se pueden guardar tickets.")));
    m_numberingRefused = false;
    // A chain restart must be seen, whichever sync decided it.
    const QString restart = integration()->takeChainRestartNotice();
    if (!restart.isEmpty()) {
        qWarning() << "MainWindow: direct AEAT chain restarted -" << restart;
        m_result->setText(UiKit::warnHtml(tr("Conexión directa con la AEAT: %1").arg(restart.toHtmlEscaped())));
    }
}

void MainWindow::onRecordSettled(const QString &invoiceId, bool cancellation, const VerifactuResult &result)
{
    if (cancellation) {
        // AEAT holds the invoice cancelled: the books must say so (dated today, in an open quarter).
        const int applied = applySettledVerifactuCancellation(db, invoiceId, QDate::currentDate(), result);
        if (applied < 0 || applied == 2) {
            // No ticket behind it (the self-test's record), or its dialog already marked it.
            qDebug() << "MainWindow: AEAT settled the cancellation of" << invoiceId
                     << (applied < 0 ? "(no ticket)" : "(already marked)") << "-"
                     << (result.isSuccess() ? "accepted" : result.errorDescription);
            return;
        }
        const bool marked = applied > 0;
        qWarning() << "MainWindow: AEAT settled the cancellation of" << invoiceId << "outside its dialog -"
                   << (result.isSuccess() ? "accepted" : result.errorDescription) << "- marked" << marked;
        m_result->setText(result.isSuccess()
            ? UiKit::okHtml(tr("La AEAT ha registrado la anulación de la factura %1.").arg(invoiceId.toHtmlEscaped()))
                  + (marked ? QString() : "<br>" + UiKit::errorHtml(tr("No se pudo marcar como anulada: revísela en Anular factura.")))
            : UiKit::errorHtml(tr("La AEAT no ha aceptado la anulación de la factura %1: %2")
                                   .arg(invoiceId.toHtmlEscaped(), result.errorDescription.toHtmlEscaped())));
        return;
    }
    const int rows = applySettledVerifactuResult(db, invoiceId, result);
    qDebug() << "MainWindow: AEAT settled" << invoiceId << "-" << rows << "row(s) updated";
    updateRefusedInvoices();
    if (rows > 0)
        showVerifactuRejection(invoiceId, result);
}

void MainWindow::settlePendingRectifications()
{
    for (const QString &number : pendingRectificationNumbers(db)) {
        if (integration()->holdsPendingRegistration(number))
            continue;                                                   // it sends itself and settles
        VerifactuResult stored;
        if (integration()->storedRegistrationOutcome(number, &stored)) {
            qDebug() << "MainWindow: rectificativa" << number << "settled from its stored AEAT answer -"
                     << applySettledVerifactuResult(db, number, stored) << "row(s)";
            continue;
        }
        if (markRectificationUnsent(db, number, tr("Sin respuesta de la AEAT: compruébela con \"Consultar en AEAT\" en Recogida de Prendas "
                                                  "antes de rehacerla en Rectificar factura.")))
            qWarning() << "MainWindow: rectificativa" << number << "left PENDIENTE with nothing to send it - marked ERROR";
    }
}

void MainWindow::continueDirectChain()
{
    // Our system's own chain, synced with AEAT before the first record (never the gateway's).
    integration()->syncDirectChain(
        [this](bool ok, const QString &message) {
            qDebug() << "MainWindow: direct AEAT chain -" << ok << message;
            const int atAeat = integration()->highestNumberAtAeat();
            bool localRead = false;
            const int local = readMaxValueInColumnFromTable(db, "n_recibo", "ingresos", &localRead);
            if (localRead && atAeat > local) {
                // Restored from an older backup: AEAT already holds later numbers.
                qWarning() << "MainWindow: AEAT holds ticket numbers up to" << atAeat << "- the database only" << local;
                m_result->setText(UiKit::warnHtml(tr("La AEAT ya tiene facturas de este sistema hasta el nº %1 y esta base "
                                                     "de datos llega al %2 (¿restaurada de una copia anterior?). El "
                                                     "siguiente ticket será el %3.").arg(atAeat).arg(local).arg(atAeat + 1)));
                if (!m_saving)                                      // a save resets the form itself
                    setNextTicketNumber();
            }
            if (!ok)
                m_result->setText(UiKit::warnHtml(tr("Conexión directa con la AEAT: %1").arg(message.toHtmlEscaped())));
            statusBar()->showMessage(tr("Conexión directa con la AEAT: %1").arg(message), 10000);
        });
}

void MainWindow::resetAllContents()
{
    ui->cb_client->clear();
    ui->table_ticket->clearContents();
    setNextTicketNumber();
    populateCbClient();
    resizeTable();
    setServiceToCb(0); // Set rows starting from 0
    setGarmentToCbAndPopulate(0); // Set rows starting from 0
    ui->le_addr->clear();
    ui->le_cost_total->clear();
    ui->le_mobile->clear();
    ui->le_phone->clear();
    ui->de_date_recep->setMaximumDate(QDate::currentDate());
    ui->de_date_recep->setDate(QDate::currentDate());
    ui->pb_payment->setChecked(false);
    on_pb_payment_toggled(false);
}

/********************************************************************************************
 * CUSTOM FUNCTIONS
 *******************************************************************************************/

void MainWindow::setNextTicketNumber()
{
    bool read = false;
    const int next = nextTicketNumber(db, integration() ? integration()->highestNumberAtAeat() : 0,
                                      &read);
    // Never a guessed number: without the highest stored one no ticket is saved.
    ui->le_nr_ticket->setText(read ? QString::number(next) : QString());
    if (!read && m_result)
        m_result->setText(UiKit::errorHtml(tr("No se pudo leer el último número de ticket de la base de datos."))
                          + "<br>" + tr("No se puede guardar ningún ticket hasta que se pueda leer; revise el log de depuración."));
}

void MainWindow::populateCbClient()
{
    // The client typed in the ticket being filled stays; the phone fields are not reloaded.
    const QString typed = ui->cb_client->currentText();
    const QSignalBlocker block(ui->cb_client);
    ui->cb_client->clear();
    ui->cb_client->addItems(readColumnFromTable(db, "nombre", "clientes", ""));
    ui->cb_client->setCurrentText(typed);
}

void MainWindow::resizeTable()
{
    for (int row = 0; row < pbAddedRows; row++) {
        ui->table_ticket->removeRow(ui->table_ticket->rowCount() - 1);
    }
    pbAddedRows = 0;
}

void MainWindow::setServiceToCb(int initialRow)
{
    for (int row = initialRow; row < ui->table_ticket->rowCount(); row++) {
        QComboBox *comBox = new QComboBox();
        comBox->addItem("Limp.");
        comBox->addItem("Plan.");
        ui->table_ticket->setCellWidget(row, TABLE_TICKET_SERV, comBox);
        connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_SERV)),
                &QComboBox::currentTextChanged,
                this, &MainWindow::cbServChanged);
    }
}

void MainWindow::setGarmentToCbAndPopulate(int initialRow)
{
    QStringList garmentList = readColumnFromTable(db, "nombre", "prendas", "");
    for (int row = initialRow; row < ui->table_ticket->rowCount(); row++) {
        QComboBox *comBoxPrenda = new QComboBox();
        comBoxPrenda->setAutoFillBackground(true);
        comBoxPrenda->setEditable(true);
        comBoxPrenda->addItems(garmentList);
        comBoxPrenda->setCurrentText("");
        comBoxPrenda->setObjectName("cb_prenda_" + QString::number(row));
        ui->table_ticket->setCellWidget(row, TABLE_TICKET_GARM, comBoxPrenda);
        connect(qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM)),
                &QComboBox::currentTextChanged,
                this, &MainWindow::cbGarmChanged);
    }
}

void MainWindow::setGarmentPrice(int garmentRow, QString garmentText, QString serviceText)
{
    QTableWidgetItem *qntyItem = ui->table_ticket->item(garmentRow, TABLE_TICKET_QNTY);
    if (!qntyItem || qntyItem->text().trimmed().isEmpty()) {
        // A garment picked before its quantity counts one; the new cell re-prices the row.
        ui->table_ticket->setItem(garmentRow, TABLE_TICKET_QNTY, new QTableWidgetItem("1"));
        return;
    }
    // Comma-decimal normalisation + size factor live in sql_lite::garmentImporte
    // (unit-tested); see its comment for why the comma matters (m2 garments).
    QTableWidgetItem *sizeItem = ui->table_ticket->item(garmentRow, TABLE_TICKET_SIZE);
    const QString size = sizeItem ? sizeItem->text() : QString();
    // An m2 garment is priced by its size; until it is measured it is 0, not one m2.
    const double importe = garmentUnmeasured(garmentText, size) ? 0.0
        : garmentImporte(qntyItem->text(), size, readGarmentPrice(db, garmentText, serviceText));
    auto *amount = new QTableWidgetItem(displayMoney(importe));
    amount->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ui->table_ticket->setItem(garmentRow, TABLE_TICKET_PRIC, amount);
}

int MainWindow::rowOfCellWidget(QObject *widget, int column) const
{
    for (int row = 0; row < ui->table_ticket->rowCount(); ++row)
        if (ui->table_ticket->cellWidget(row, column) == widget)
            return row;
    return -1;
}

void MainWindow::updateRowPrice(int row)
{
    if (row < 0)
        return;
    auto *cbGarment = qobject_cast<QComboBox *>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
    auto *cbService = qobject_cast<QComboBox *>(ui->table_ticket->cellWidget(row, TABLE_TICKET_SERV));
    if (!cbGarment || !cbService)
        return;
    if (cbGarment->findText(cbGarment->currentText(), Qt::MatchExactly) != -1)
        setGarmentPrice(row, cbGarment->currentText(), cbService->currentText());
    else if (cbGarment->currentText().isEmpty())
        ui->table_ticket->setItem(row, TABLE_TICKET_PRIC, new QTableWidgetItem);
}

void MainWindow::cbGarmChanged(const QString &)
{
    // The row of the combo that changed - not the table's current row, which a
    // combo inside a cell does not move.
    updateRowPrice(rowOfCellWidget(sender(), TABLE_TICKET_GARM));
}

void MainWindow::cbServChanged(const QString &)
{
    updateRowPrice(rowOfCellWidget(sender(), TABLE_TICKET_SERV));
}

bool MainWindow::validateTicket()
{
    const auto refuse = [this](const QString &reason) {
        m_result->setText(UiKit::errorHtml(reason) + "<br>" + tr("No se ha guardado nada."));
        return false;
    };
    if (ui->le_nr_ticket->text().toInt() <= 0)
        return refuse(tr("No hay número de ticket: no se pudo leer el último de la base de datos."));
    if (ui->cb_client->currentText().trimmed().isEmpty())
        return refuse(tr("No se ha introducido ningún cliente."));
    if (integration()) {
        const QString blocked = integration()->numberingBlockedReason();
        m_numberingRefused = !blocked.isEmpty();
        if (m_numberingRefused)
            return refuse(blocked);
        const QString notInvoiced = ui->pb_payment->isChecked() ? integration()->invoicingBlockedReason()
                                                                : QString();
        if (!notInvoiced.isEmpty())
            return refuse(notInvoiced + " " + tr("Puede guardar el ticket sin cobrar."));
        // A paid ticket is invoiced under its reception date.
        const QString early = ui->pb_payment->isChecked()
            ? integration()->dateBeforeProductionReason(ui->de_date_recep->date()) : QString();
        if (!early.isEmpty())
            return refuse(early + " " + tr("Puede guardar el ticket sin cobrar y cobrarlo en Recogida de prendas."));
    }
    double totalCost = 0.0;
    bool sizePending = false;   // an m2 garment can be left at the shop before it is measured
    for (int row = 0; row < ui->table_ticket->rowCount(); row++) {
        auto *cbGarment = qobject_cast<QComboBox *>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
        QTableWidgetItem *qty = ui->table_ticket->item(row, TABLE_TICKET_QNTY);
        const QString garment = cbGarment ? cbGarment->currentText() : QString();
        if (!ui->table_ticket->item(row, TABLE_TICKET_PRIC)) {
            // A typed garment that is not in the price list has no price: it would be dropped.
            if (!garment.isEmpty())
                return refuse(tr("La prenda de la fila %1 no tiene precio.").arg(row + 1));
            continue;
        }
        if (garment.isEmpty() && ui->table_ticket->item(row, TABLE_TICKET_PRIC)->text().isEmpty())
            continue;   // an empty slot
        if (!qty || qty->text().toInt() <= 0)
            return refuse(tr("La prenda de la fila %1 no tiene cantidad.").arg(row + 1));
        if (garment.isEmpty())
            return refuse(tr("La fila %1 tiene importe pero no prenda.").arg(row + 1));
        QTableWidgetItem *size = ui->table_ticket->item(row, TABLE_TICKET_SIZE);
        if (garmentUnmeasured(garment, size ? size->text() : QString()))
            sizePending = true;
        else if (ui->table_ticket->item(row, TABLE_TICKET_PRIC)->text().isEmpty())
            return refuse(tr("La prenda de la fila %1 no tiene precio.").arg(row + 1));
        const double price = parseDecimal(ui->table_ticket->item(row, TABLE_TICKET_PRIC)->text());
        if (price < 0.0)
            return refuse(tr("La prenda de la fila %1 tiene un importe negativo; las correcciones se hacen "
                             "con Rectificar factura.").arg(row + 1));
        totalCost += price;
    }
    if (totalCost == 0.0 && !sizePending)
        return refuse(tr("El ticket no tiene ninguna prenda con importe."));
    // Once paid the invoice goes to AEAT and its amounts are frozen: an unmeasured m2
    // garment would be invoiced at 0 and could never be charged.
    if (sizePending && ui->pb_payment->isChecked())
        return refuse(tr("Hay una prenda por m2 sin medir: introduzca su tamaño o guarde el ticket sin "
                         "cobrar y cóbrelo en Recogida cuando esté medida."));
    // The whole quarter: a month without rows inside a closed quarter is closed too.
    if (quarterIsClosed(db, ui->de_date_recep->date()))
        return refuse(tr("La fecha de recepción pertenece a un trimestre con la contabilidad cerrada."));
    return true;
}

void MainWindow::checkClientData()
{
    QString currentClient = ui->cb_client->currentText();
    if (ui->cb_client->findText(currentClient) >= 0) {
        updateItemToClient(db, "tel_fijo",  ui->le_phone->text(),  currentClient);
        updateItemToClient(db, "movil",     ui->le_mobile->text(), currentClient);
        updateItemToClient(db, "direccion", ui->le_addr->text(),   currentClient);
    }
    else {
        currentClient = removeSpecialChars(currentClient.simplified().toLower());
        bool clientFound = false;
        for (int idx = 0; idx < ui->cb_client->count(); idx++) {
            QString clientInCb = removeSpecialChars(ui->cb_client->itemText(idx).simplified().toLower());
            if (currentClient == clientInCb)
                clientFound = true;
        }
        if (!clientFound) {
            if (!addNewClient(db, ui->cb_client->currentText(), ui->le_phone->text(), ui->le_addr->text(), ui->le_mobile->text()))
                m_clientNote = tr("El ticket se ha guardado, pero no se pudo añadir el cliente al listado de clientes; "
                                  "revise el log de depuración.");
        } else {
            qDebug() << "checkClientData: client found in database after removing special characters like accents or 'ñ'. "
                     << "The data entered for the client in this receipt has not been added to the client in the client list. "
                     << "If you want to update the data, add manually in the client list.";
            m_clientNote = tr("El cliente ya existía escrito de otra forma (tildes o 'ñ'): sus teléfonos y "
                              "dirección no se han cambiado; actualícelos en Listado de clientes si hace falta.");
        }
    }

}

QString MainWindow::recoveryFloorIso()
{
    AppSettings *s = AppSettings::instance();
    return verifactuRecoveryFloorIso(db, s->verifactuPendingRecoveryFloorDate(), s->verifactuDirectAeat() && s->aeatDirectProduction());
}

void MainWindow::updateAeatEnvironment()
{
    if (!m_aeatEnvironment)
        return;                                     // before the status bar is built
    const VerifactuCoordinator::Notice notice = m_coordinator->aeatEnvironmentNotice();
    m_aeatEnvironment->setText(notice.text);
    m_aeatEnvironment->setVisible(notice.visible);
}

void MainWindow::updateUnsentRecords()
{
    if (!m_unsentRecords)
        return;                                     // before the status bar is built
    const VerifactuCoordinator::Notice notice = m_coordinator->unsentRecordsNotice();
    m_unsentRecords->setText(notice.text);
    m_unsentRecords->setVisible(notice.visible);
}

void MainWindow::updateRefusedInvoices()
{
    if (!m_refusedInvoices)
        return;                                     // an outcome settled before the window finished building
    const int refused = refusedInvoiceCount(db, recoveryFloorIso());
    // A link: the count opens the list of those invoices (showRefusedInvoices).
    m_refusedInvoices->setText("<a href=\"refused\" style=\"text-decoration:none\">"
                               + UiKit::errorHtml(tr("%n factura(s) rechazada(s) por la AEAT: pulse aquí para ver "
                                                     "cuáles y reenviarlas", "", refused)) + "</a>");
    m_refusedInvoices->setVisible(refused > 0);
}

void MainWindow::showRefusedInvoices()
{
    auto *dlg = new RefusedInvoicesDialog(db, recoveryFloorIso(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &RefusedInvoicesDialog::openTicket, this, [this, dlg](const QString &nRecibo, int seq) {
        auto *recogida = new RecogPrendas(db, this);
        recogida->setAttribute(Qt::WA_DeleteOnClose);
        recogida->m_verifactuIntegration = integration();
        recogida->show();
        recogida->showTicket(nRecibo, seq);
        // Back in the list, a resent invoice is gone.
        QPointer<RefusedInvoicesDialog> list(dlg);
        connect(recogida, &QObject::destroyed, this, [this, list] {
            if (list)
                list->reload();
            updateRefusedInvoices();
        });
    });
    dlg->show();
}

void MainWindow::showVerifactuRejection(const QString &invoiceId, const VerifactuResult &result)
{
    // A definitive refusal can come after the customer got the factura with its QR:
    // it stays in the panel, not only in the status bar, until someone resends it.
    const QString html = m_coordinator->rejectionHtml(invoiceId, result);
    if (!html.isEmpty())
        m_result->setText(html);
}

bool MainWindow::saveTicket(double &storedTotal, int &garments)
{
    // A paid garment starts PENDIENTE and the async submit handler patches
    // CSV/timestamp/estado once AEAT replies (see onVerifactuRequestFinished());
    // an unpaid one starts SIN COBRAR - there is no invoice to send yet.
    // table_ticket has a fixed set of empty row slots - only rows with a price are saved,
    // so log the count of garments actually inserted, not the slot count.
    QList<IngresoGarmentRow> rows;
    storedTotal = 0.0;
    garments = 0;
    for (int row = 0; row < ui->table_ticket->rowCount(); row++) {
        // If there is any content in price of that row then save
        QComboBox *cbGarment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
        // A slot with no garment (e.g. a quantity typed and left) is not a garment.
        if (ui->table_ticket->item(row, TABLE_TICKET_PRIC) && cbGarment && !cbGarment->currentText().isEmpty()) {
            QComboBox *cbService = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_SERV));

            IngresoGarmentRow r;
            r.nRecibo        = ui->le_nr_ticket->text();
            r.cliente        = ui->cb_client->currentText();
            r.fechaRecepcion = dbDateText(ui->de_date_recep->date());
            // A paid-at-save ticket books the payment on the reception date.
            r.fechaPago      = ui->pb_payment->isChecked() ? r.fechaRecepcion : QString("");
            r.fechaRecogida  = "";
            r.importe        = ui->table_ticket->item(row, TABLE_TICKET_PRIC)->text().replace(",",".");
            r.pagado         = ui->pb_payment->isChecked() ? INGRESOS_PAGADO_SI : INGRESOS_PAGADO_NO;
            r.estado         = INGRESOS_ESTADO_EN_TIENDA;
            r.cantidad       = ui->table_ticket->item(row, TABLE_TICKET_QNTY)->text();
            r.prenda         = cbGarment->currentText();
            r.size           = ui->table_ticket->item(row, TABLE_TICKET_SIZE)
                                   ? ui->table_ticket->item(row, TABLE_TICKET_SIZE)->text().replace(",",".") : QString("");
            r.servicio       = cbService->currentText();
            r.observaciones  = ui->table_ticket->item(row, TABLE_TICKET_OBSE)
                                   ? ui->table_ticket->item(row, TABLE_TICKET_OBSE)->text() : QString("");
            r.hash           = genHash16();
            // An unpaid row has no invoice to send, so it is SIN COBRAR, not PENDIENTE;
            // a paid one starts PENDIENTE and the async AEAT submit patches it on reply.
            r.verifactuEstado = verifactuEstadoToString(
                r.pagado == QLatin1String(INGRESOS_PAGADO_SI) ? VerifactuEstado::NotSubmitted
                                                : VerifactuEstado::Unpaid);

            storedTotal += roundToCents(moneyText(r.importe).toDouble());
            qDebug() << "saveTicket: garment" << rows.size() + 1 << "ticket=" << r.nRecibo
                     << "importe=" << r.importe << "hash=" << r.hash;
            rows << r;
        }
    }
    if (!insertGarmentRows(db, rows)) {
        qWarning() << "saveTicket: ticket" << ui->le_nr_ticket->text()
                   << "not stored (nothing kept) - stopping before any AEAT submission";
        storedTotal = 0.0;
        return false;
    }
    garments = rows.size();
    qDebug() << "saveTicket: ticket" << ui->le_nr_ticket->text()
             << "-" << rows.size() << "garment(s) saved, total" << storedTotal;
    return true;
}

bool MainWindow::printRecibo()
{
    // Save-time print: AEAT submission is in flight (async). DB still has empty CSV,
    // so the QR cannot be fetched yet. Print without QR/CSV; the customer can be
    // given a reprint via RecogPrendas after AEAT replies.
    auto *ui_impr = new Imprimir(db, this);
    const auto release = qScopeGuard([ui_impr] { ui_impr->deleteLater(); });
    ui_impr->isRecibo = true;
    ui_impr->le_n_ticket->setText(ui->le_nr_ticket->text());
    ui_impr->getTicketInfo();
    ui_impr->buildTicket(true, ui->pb_payment->isChecked());
    if (!AppSettings::instance()->enablePrinting())
        return false;
    const bool printed = ui_impr->printTicket();
    ui_impr->buildTicket(false, ui->pb_payment->isChecked());
    return ui_impr->printTicket() && printed;
}

bool MainWindow::printFra(const QPixmap &qrCode, bool verifactuIssued)
{
    auto *ui_impr = new Imprimir(db, this);
    const auto release = qScopeGuard([ui_impr] { ui_impr->deleteLater(); });
    ui_impr->isRecibo = false;
    ui_impr->qrCode = qrCode;
    ui_impr->verifactuIssued = verifactuIssued;
    ui_impr->le_n_ticket->setText(ui->le_nr_ticket->text());
    ui_impr->getTicketInfo();
    ui_impr->buildTicket(true, false);
    if (!AppSettings::instance()->enablePrinting())
        return false;
    const bool printed = ui_impr->printTicket();
    ui_impr->isRecibo = true;
    ui_impr->buildTicket(false, true);
    return ui_impr->printTicket() && printed;
}

/********************************************************************************************
 * FUNCTIONS FOR WIDGETS
 *******************************************************************************************/

void MainWindow::on_pb_payment_toggled(bool checked)
{
    ui->lbl_payment_badge->setText(checked ? UiKit::okHtml(tr("SÍ")) : UiKit::errorHtml(tr("NO")));
}

struct MainWindow::SavedTicket {
    QString number, client;
    int garments = 0;
    double total = 0.0;
    bool paid = false;
    QString printed;        // empty: nothing was printed
    QString refusal;        // an AEAT refusal that came in during the wait
    QString numberError;    // the next number could not be read
};

void MainWindow::on_pb_save_clicked()
{
    // The number on screen is the ticket being printed until the form resets; a click
    // during the wait for AEAT would save a second ticket under it.
    if (m_saving)
        return;
    m_saving = true;
    ui->pb_save->setEnabled(false);
    const auto saved = qScopeGuard([this] {
        m_saving = false;
        ui->pb_save->setEnabled(true);
    });
    if (!validateTicket())
        return;
    m_clientNote.clear();
    SavedTicket ticket;
    ticket.number = ui->le_nr_ticket->text();
    ticket.paid = ui->pb_payment->isChecked();
    const QDate invoiceDate = ui->de_date_recep->date();

    // The invoice amount is what was stored, row by row, not the on-screen total.
    if (!saveTicket(ticket.total, ticket.garments)) {
        // Nothing is sent nor printed for a ticket that was not stored whole.
        m_result->setText(UiKit::errorHtml(tr("No se pudo guardar el ticket %1 completo.").arg(ticket.number))
                          + "<br>" + tr("No se ha guardado ninguna prenda ni se ha enviado a AEAT o impreso. "
                                        "Revise el log (Ayuda \u2192 Log de depuración) antes de repetirlo."));
        return;
    }
    // Only for a stored ticket: a retry after a failed save must not add the client twice.
    checkClientData();
    ticket.printed = submitAndPrint(ticket.number, invoiceDate, ticket.paid, ticket.total, ticket.refusal);
    ticket.client = ui->cb_client->currentText();
    resetAllContents();
    // Without the next number setNextTicketNumber left its error in the panel: keep it.
    ticket.numberError = ui->le_nr_ticket->text().isEmpty() ? m_result->text() : QString();
    m_result->setText(savedMessage(ticket));
}

QString MainWindow::submitAndPrint(const QString &ticketNum, const QDate &invoiceDate, bool isPaid,
                                   double totalAmount, QString &refusal)
{
    if (!isPaid)
        return printRecibo() ? tr("Recibo impreso.") : QString();

    const QString reqId = m_coordinator->submitInvoice(ticketNum, invoiceDate, totalAmount);

    // The direct client's QR needs no answer from AEAT (which may make the
    // next submission wait up to TiempoEsperaEnvio): print at once.
    QPixmap qrCode = reqId.isEmpty() ? QPixmap() : m_coordinator->qrBeforeReply(ticketNum, invoiceDate, totalAmount);
    const bool qrBeforeReply = !qrCode.isNull();
    bool gotSuccessfulReply = false;
    if (!reqId.isEmpty() && !qrBeforeReply) {
        const VerifactuCoordinator::AeatReply reply = m_coordinator->waitForReply(reqId, ticketNum);
        refusal = reply.refusal;
        gotSuccessfulReply = reply.success;
        qrCode = reply.qrCode;
    }

    qDebug() << "saveTicket: ticket" << ticketNum
             << "isPaid=true qrBeforeReply=" << qrBeforeReply << "gotSuccessfulReply=" << gotSuccessfulReply
             << "(either -> factura with QR + paid recibo, neither -> paid recibo)";
    if (qrBeforeReply || gotSuccessfulReply) {
        // An accepted reply's QR always prints with the leyenda, also if its row write failed.
        return printFra(qrCode, qrBeforeReply || gotSuccessfulReply)
                   ? tr("Factura con QR (cliente) y recibo pagado (establecimiento) impresos.") : QString();
    }
    // AEAT has not confirmed within the bounded wait, but the request
    // may still be in flight (the transport timeout is longer). Print
    // the paid recibo now and flag the event so a late confirmation
    // tells the operator the factura with QR can be printed.
    const QString printed = printRecibo() ? tr("Recibo impreso (pagado); la factura con QR se puede "
                                               "reimprimir en Recogida cuando AEAT confirme.") : QString();
    m_coordinator->markPrintedWithoutQr(reqId);
    return printed;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_coordinator->isWaitingForAeat()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

QString MainWindow::savedMessage(const SavedTicket &ticket) const
{
    return UiKit::okHtml(tr("Ticket %1 guardado: %2, %3 prenda(s), %4 €, %5.")
                             .arg(ticket.number, ticket.client.toHtmlEscaped()).arg(ticket.garments)
                             .arg(displayMoney(ticket.total),
                                  ticket.paid ? tr("pagado") : tr("sin cobrar")))
           + "<br>" + (ticket.printed.isEmpty()
                           ? tr("No se ha impreso (impresión desactivada o impresora sin respuesta).")
                           : ticket.printed)
           + (ticket.paid ? "<br>" + tr("El envío a AEAT se confirma en la barra de estado.") : QString())
           + (m_clientNote.isEmpty() ? QString() : "<br>" + UiKit::warnHtml(m_clientNote))
           + (ticket.refusal.isEmpty() ? QString() : "<br>" + ticket.refusal)
           + (ticket.numberError.isEmpty() ? QString() : "<br>" + ticket.numberError);
}

void MainWindow::on_pb_reset_clicked()
{
    resetAllContents();
    m_result->showInfo(tr("Ticket borrado. Listo para un nuevo ticket."));
}

void MainWindow::on_cb_client_editTextChanged(const QString &arg1)
{
    if (arg1 != "") {
        ui->le_phone->setText(searchItemFromClient(db, "tel_fijo", arg1, false));
        ui->le_mobile->setText(searchItemFromClient(db, "movil", arg1, false));
        ui->le_addr->setText(searchItemFromClient(db, "direccion", arg1, false));
    }
}

void MainWindow::on_table_ticket_cellChanged(int row, int column)
{
    if (column == TABLE_TICKET_QNTY || column == TABLE_TICKET_SIZE) {
        updateRowPrice(row);
    }
    else if (column == TABLE_TICKET_PRIC) {
        double totalPrice = 0.0;
        const QSignalBlocker block(ui->table_ticket);   // normalising a price must not re-enter here
        for (int rowCnt = 0; rowCnt < ui->table_ticket->rowCount(); rowCnt++) {
            QTableWidgetItem *priceItem = ui->table_ticket->item(rowCnt, column);
            if (priceItem && !priceItem->text().isEmpty()) {
                const double priceValue = parseDecimal(priceItem->text());
                priceItem->setText(displayMoney(priceValue));
                priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                totalPrice += roundToCents(priceValue);
            }
        }
        ui->le_cost_total->setText(displayMoney(totalPrice));
    }
}

void MainWindow::on_pb_add_row_clicked()
{
    pbAddedRows++;
    ui->table_ticket->insertRow(ui->table_ticket->rowCount());
    setServiceToCb(ui->table_ticket->rowCount() - 1);
    setGarmentToCbAndPopulate(ui->table_ticket->rowCount() - 1);
}

/********************************************************************************************
 * FUNCTIONS FOR TASKBAR OBJECTS
 *******************************************************************************************/

void MainWindow::on_actionCerrar_triggered()
{
    close();
}

// One list window; its object name stays the Spanish title (the tests and the UI smoke find it by it).
void MainWindow::openListado(const QString &tableName, const QString &objectName, const QString &title)
{
    auto *listado = new Listado(db, this);
    listado->setAttribute(Qt::WA_DeleteOnClose);
    listado->tableName = tableName;
    listado->setObjectName(objectName);
    listado->lbl_title->setText(title);
    listado->setWindowTitle(title);
    listado->populateTable();
    connect(listado, &Listado::populateClientes, this, &MainWindow::repopulateClientes);
    connect(listado, &Listado::populatePrendas, this, &MainWindow::repopulatePrendas);
    listado->show();
}

void MainWindow::openImprimir(bool isRecibo, bool isCompleteInvoice, const QString &title)
{
    auto *imprimir = new Imprimir(db, this);
    imprimir->setAttribute(Qt::WA_DeleteOnClose);
    imprimir->isRecibo = isRecibo;
    imprimir->isCompleteInvoice = isCompleteInvoice;
    imprimir->verifactuIntegration = integration();
    imprimir->setWindowTitle(title);
    imprimir->show();
}

void MainWindow::on_actionIngresos_triggered()
{
    openListado("ingresos", "Ingresos", tr("Ingresos"));
}

void MainWindow::on_actionGastos_triggered()
{
    openListado("gastos", "Gastos", tr("Gastos"));
}

void MainWindow::repopulatePrendas()
{
    // Each row keeps its garment and price; only the choices are refreshed.
    const QStringList garments = readColumnFromTable(db, "nombre", "prendas", "");
    for (int row = 0; row < ui->table_ticket->rowCount(); ++row) {
        auto *combo = qobject_cast<QComboBox *>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
        if (!combo)
            continue;
        const QString current = combo->currentText();
        const QSignalBlocker block(combo);
        combo->clear();
        combo->addItems(garments);
        combo->setCurrentText(current);
    }
}

void MainWindow::on_actionListado_de_prendas_triggered()
{
    openListado("prendas", "Listado de prendas", tr("Listado de prendas"));
}

void MainWindow::repopulateClientes()
{
    populateCbClient();
}

void MainWindow::on_actionListado_de_clientes_triggered()
{
    openListado("clientes", "Listado de clientes", tr("Listado de clientes"));
}

void MainWindow::on_actionListado_de_proveedores_triggered()
{
    openListado("proveedores", "Listado de proveedores", tr("Listado de proveedores"));
}

void MainWindow::on_actionListado_de_servicios_triggered()
{
    openListado("servicios", "Listado de servicios", tr("Listado de servicios"));
}

void MainWindow::on_actionRecogida_de_prendas_triggered()
{
    RecogPrendas *ui_recog;
    ui_recog = new RecogPrendas(db, this);
    ui_recog->setAttribute(Qt::WA_DeleteOnClose);
    ui_recog->m_verifactuIntegration = integration();
    //ui_recog->setWindowState(Qt::WindowMaximized);
    ui_recog->show();
}

void MainWindow::on_actionRecibo_triggered()
{
    openImprimir(true, false, tr("Imprimir recibo"));
}

void MainWindow::on_actionFactura_triggered()
{
    openImprimir(false, false, tr("Imprimir factura"));
}

void MainWindow::on_actionFactura_completa_triggered()
{
    openImprimir(false, true, tr("Imprimir factura completa"));
}

void MainWindow::on_actionGenerar_contabilidad_triggered()
{
    Contabilidad *ui_contabilidad;
    ui_contabilidad = new Contabilidad(db, this);
    ui_contabilidad->setAttribute(Qt::WA_DeleteOnClose);
    ui_contabilidad->show();
}

void MainWindow::on_actionRevertir_contabilidad_triggered()
{
    Contabilidad *ui_rev_cont;
    ui_rev_cont = new Contabilidad(db, this, Contabilidad::Revert);
    ui_rev_cont->setAttribute(Qt::WA_DeleteOnClose);
    ui_rev_cont->show();
}

void MainWindow::on_actionFormulario_facturas_triggered()
{
    Facturas *ui_facturas;
    ui_facturas = new Facturas(db, this);
    ui_facturas->setAttribute(Qt::WA_DeleteOnClose);
    ui_facturas->show();
}

void MainWindow::on_actionAnadir_nuevas_prendas_triggered()
{
    AddGarment *ui_add_garment;
    ui_add_garment = new AddGarment(db, this);
    ui_add_garment->setAttribute(Qt::WA_DeleteOnClose);
    ui_add_garment->invoicing = invoicingPolicyOf(integration());
    // Submitted now, like a paid ticket on save; if AEAT does not answer, the row stays
    // PENDIENTE for the startup recovery.
    connect(ui_add_garment, &AddGarment::paidGarmentSaved, this,
            [this](const QString &ticketNum, const QDate &paymentDate, double amount, int seq) {
        const QString reqId = m_coordinator->submitInvoice(ticketNum, paymentDate, amount, seq);
        if (reqId.isEmpty() && integration() && integration()->isConfigured())
            m_result->setText(UiKit::errorHtml(tr("El ticket %1 no se ha enviado a AEAT.")
                                                   .arg(verifactuInvoiceId(ticketNum, seq).toHtmlEscaped()))
                              + "<br>" + integration()->getLastError().toHtmlEscaped());
    });
    ui_add_garment->show();
}

void MainWindow::on_actionAnular_factura_verifactu_triggered()
{
    if (!integration() || !integration()->isConfigured()) {
        qWarning() << "Cancel invoice action: Verifactu not configured";
        m_result->setText(UiKit::errorHtml(tr("Verifactu no está configurado."))
                          + "<br>" + tr("Configure las credenciales en Archivo \u2192 Configuración."));
        return;
    }
    CancelInvoiceDialog dlg(db, this);
    dlg.m_verifactu = integration();
    dlg.exec();
}

// Art. 8.2.a RD 1007/2023 - rectificativa (R1-R5) is one of the two legal
// correction paths for an already-registered factura (the other is anulacion).
void MainWindow::on_actionRectificar_factura_verifactu_triggered()
{
    if (!integration() || !integration()->isConfigured()) {
        qWarning() << "Rectify invoice action: Verifactu not configured";
        m_result->setText(UiKit::errorHtml(tr("Verifactu no está configurado."))
                          + "<br>" + tr("Configure las credenciales en Archivo \u2192 Configuración."));
        return;
    }
    RectifyInvoiceDialog dlg(db, this);
    dlg.m_verifactu = integration();
    dlg.exec();
    // Rectificativa eagerly INSERTs its row (claims the next n_recibo) on submit,
    // so the local counter has advanced regardless of AEAT success/failure. Refresh
    // the MainWindow ticket-number field so the next save uses a fresh number.
    setNextTicketNumber();
}

// Art. 14.1 RD 1007/2023 requires "acceso completo e inmediato" to the AEAT records
// in legible XML. We persist the AEAT-style XML returned by Irene Solutions in
// ingresos.verifactu_xml; this action dumps a date range into a single envelope file
// that can be handed to Hacienda on request.
void MainWindow::on_actionExportar_registros_aeat_triggered()
{
    (new AeatExportDialog(db, this))->show();
}

void MainWindow::on_actionComprobar_tras_actualizar_triggered()
{
    // Its own process, as from the Start menu: the check swaps in a copy of the settings.
    const bool started = QProcess::startDetached(QCoreApplication::applicationFilePath(),
        { QStringLiteral("--settings"), AppSettings::instance()->filePath(), QStringLiteral("--self-check") });
    m_result->setText(started ? tr("La comprobación se abre en su propia ventana; este programa puede seguir abierto.")
                              : UiKit::errorHtml(tr("No se pudo iniciar la comprobación: use Inicio \u2192 La Ideal - "
                                                    "Comprobar tras actualizar.")));
}

void MainWindow::on_actionMostrar_log_triggered()
{
    const QString path = AppLogger::logFilePath();
    m_result->setText(UiKit::fileLinkHtml(path, tr("Log de depuración")) + "<br>"
                      + tr("Envíe este archivo al soporte técnico cuando tenga un problema (clic para abrirlo)."));
}

// Declaración responsable visible in the software, as required by Art. 13
// RD 1007/2023. The producer is La Ideal's own, fixed in ProducerDeclaration;
// only the software version varies.
void MainWindow::on_actionAcerca_de_Verifactu_triggered()
{
    const QString version = Updater::currentVersion();
    const QString declaration = ProducerDeclaration::html(version);

    QDialog dlg(this);
    UiKit::setUpDialog(&dlg, tr("Acerca de Verifactu"), 640);
    auto *layout = new QVBoxLayout(&dlg);

    auto *body = new QTextEdit(&dlg);
    body->setObjectName("txtDeclaration");
    body->setReadOnly(true);
    body->setHtml(declaration);
    body->setMinimumHeight(420);
    layout->addWidget(body);

    auto *panel = new UiKit::ResultPanel(QString(), &dlg);
    panel->setObjectName("lblResult");
    QStringList notes;
    if (!ProducerDeclaration::isComplete())
        notes << UiKit::warnHtml(tr("Faltan datos del productor (dirección, fecha y lugar de la firma): "
                                    "la declaración está pendiente de firmar."));
    // Art. 15.4: another producer's component brings its own declaración.
    if (!AppSettings::instance()->verifactuDirectAeat())
        notes << UiKit::warnHtml(tr("Con la conexión a través de IreneSolutions, su servicio es un componente de otro "
                                    "productor y necesita su propia declaración responsable (art. 15.4 de la Orden "
                                    "HAC/1177/2024)."));
    panel->setText(notes.join("<br>"));
    panel->setVisible(!notes.isEmpty());
    layout->addWidget(panel);

    auto *row = new QHBoxLayout;
    auto *save = UiKit::secondaryButton(tr("Guardar como PDF..."), "btnSavePdf");
    row->addWidget(save);
    row->addStretch();
    auto *close = UiKit::primaryButton(tr("Cerrar"), "btnClose");
    row->addWidget(close);
    layout->addLayout(row);
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    // Art. 15.3: the client can have it in a free, common format.
    connect(save, &QPushButton::clicked, &dlg, [&dlg, panel, declaration, version]() {
        const QString suggested = QDir(AppSettings::instance()->reportsRoot())
                                      .filePath(QStringLiteral("declaracion_responsable_La_Ideal_%1.pdf").arg(version));
        const QString file = QFileDialog::getSaveFileName(&dlg, tr("Guardar la declaración responsable"), suggested,
                                                          tr("PDF (*.pdf)"));
        if (file.isEmpty())
            return;
        QDir().mkpath(QFileInfo(file).absolutePath());
        const bool written = ReportHtml::writePdf(file, QStringLiteral("<html><body>%1</body></html>").arg(declaration));
        panel->setVisible(true);
        panel->setText(written ? UiKit::okHtml(tr("Declaración guardada en %1").arg(file.toHtmlEscaped()))
                               : UiKit::errorHtml(tr("No se pudo guardar %1").arg(file.toHtmlEscaped())));
    });

    dlg.exec();
}

// Reads the bundled release notes in the configured language (releases_notes.txt
// or releases_notes_es.txt, Qt resources compiled into the exe via
// resources/laideal.qrc) and shows the full version history in a read-only
// monospace dialog. Same content the Inno Setup installer shows at install time;
// the GitHub release page reuses the English latest section.
void MainWindow::on_actionNotas_de_la_version_triggered()
{
    const bool english = AppSettings::instance()->language() == QLatin1String("en");
    const QString title = english ? QStringLiteral("Release notes") : tr("Notas de la versión");
    const QString resource = AppLanguage::releaseNotesResource(AppSettings::instance()->language());
    QFile f(resource);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_result->setText(UiKit::errorHtml(tr("No se pudieron cargar las notas de la versión (%1).")
                                               .arg(resource.toHtmlEscaped())));
        return;
    }
    QString notes = QString::fromUtf8(f.readAll());
    f.close();
    if (notes.startsWith(QChar(0xFEFF)))   // the files carry a UTF-8 BOM for Inno Setup
        notes.remove(0, 1);

    QDialog dlg(this);
    UiKit::setUpDialog(&dlg, title, 680);
    dlg.resize(680, 560);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(UiKit::introPanel(english
        ? QStringLiteral("What changed in each version of La Ideal, newest first.")
        : tr("Qué ha cambiado en cada versión de La Ideal, la más reciente primero.")));
    auto *view = new QTextEdit(&dlg);
    view->setReadOnly(true);
    view->setLineWrapMode(QTextEdit::WidgetWidth);
    view->setPlainText(notes);
    // Land at the top so the newest release shows first.
    view->moveCursor(QTextCursor::Start);
    layout->addWidget(view, 1);
    layout->addLayout(UiKit::closeRow(&dlg, english ? QStringLiteral("Close") : tr("Cerrar")));

    dlg.exec();
}

// Manual menu trigger: always reports the outcome, even no-update / failure.
void MainWindow::on_actionBuscar_actualizaciones_triggered()
{
    m_updater->checkForUpdates(/*silentOnNoUpdate=*/false);
}

void MainWindow::onUpdateAvailable(const QString &latestVersion,
                                   const QString &releaseNotes,
                                   const QUrl &installerUrl)
{
    UpdaterDialog dlg(m_updater, latestVersion, releaseNotes, installerUrl, this);
    dlg.exec();
}

void MainWindow::onUpdaterNoUpdateAvailable()
{
    if (m_updater->isSilentCheck())
        return;
    m_result->setText(UiKit::okHtml(tr("Está usando la versión más reciente (%1).").arg(Updater::currentVersion())));
}

void MainWindow::onUpdaterCheckFailed(const QString &error)
{
    if (m_updater->isSilentCheck()) {
        qDebug() << "Updater: silent check failed -" << error;
        return;
    }
    m_result->setText(UiKit::errorHtml(tr("No se pudo comprobar si hay actualizaciones."))
                      + "<br>" + error.toHtmlEscaped());
}

void MainWindow::on_actionHacer_copia_de_seguridad_triggered()
{
    // The start-up copy reads the same manager from its own thread.
    if (m_backupThread && m_backupThread->isRunning()) {
        m_result->setText(UiKit::warnHtml(tr("La copia de seguridad automática se está haciendo ahora.")) + "<br>"
                          + tr("Espere unos segundos a que termine y repita la copia."));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_backupManager->setSecondFolder(AppSettings::instance()->backupSecondFolder());
    const auto res = m_backupManager->performBackup();
    QApplication::restoreOverrideCursor();
    BackupManager::recordSuccess(res);
    BackupManager::recordFailure(res);

    if (res.skipped) {
        m_result->setText(UiKit::warnHtml(res.errorMessage.toHtmlEscaped()));
        return;
    }
    if (!res.success) {
        m_result->setText(UiKit::errorHtml(tr("No se pudo crear la copia de seguridad."))
                          + "<br>" + res.errorMessage.toHtmlEscaped());
        return;
    }
    m_result->setText(UiKit::okHtml(tr("Copia de seguridad creada (%1 KB).").arg(res.bytesWritten / 1024))
                      + "<br>" + UiKit::fileLinkHtml(res.backupPath, tr("Copia"))
                      + (!res.secondCopyPath.isEmpty() ? "<br>" + UiKit::fileLinkHtml(res.secondCopyPath, tr("Copia en la segunda carpeta"))
                         : !res.secondCopyError.isEmpty() ? "<br>" + UiKit::warnHtml(res.secondCopyError.toHtmlEscaped())
                                                          : QString()));
}
