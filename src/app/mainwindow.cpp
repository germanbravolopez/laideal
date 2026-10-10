#include "mainwindow.h"
#include "sql_lite.h"
#include "applogger.h"
#include <QDateTime>
#include <QSqlError>
#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>
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
#include "updaterdialog.h"
#include "pendingsubmitsdialog.h"
#include "version.h"
#include "aeatexport.h"
#include "aeatexportdialog.h"
#include "aeatcertificate.h"
#include "aeatselftest.h"
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
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeySequence>
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
    db.setDatabaseName(DB_PATH);
    mainwindowInitialSettings();
    initializeVerifactu();

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
    QTimer::singleShot(4000, this, [this]() {
        auto *dlg = new PendingSubmitsDialog(db, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        if (!dlg->loadPending()) {
            dlg->deleteLater();
            return;
        }
        connect(dlg, &PendingSubmitsDialog::retryRequested,
                this, [this](const QString &ticketNum, int seq, const QDate &invoiceDate, double totalAmount) {
            verifactuSubmitInvoice(ticketNum, invoiceDate, totalAmount, seq);
        });
        dlg->open();
    });

    // Verifactu Req. 4: trigger a backup at startup once per 24h. Deferred so
    // it doesn't block the first paint of the window. Silent on success - the
    // operator only sees a message if the backup fails (since the regulatory
    // requirement is durable storage, a failed snapshot is worth surfacing).
    m_backupManager = new BackupManager(this);
    if (m_backupManager->needsBackup()) {
        QTimer::singleShot(3000, this, [this]() {
            // performBackup() does VACUUM INTO + integrity_check (seconds on a
            // large DB) and uses its own dedicated DB connections, so run it on
            // a worker thread to keep the UI responsive; marshal the Result back
            // to the GUI thread (Qt::QueuedConnection on `this`) for the message.
            QThread *worker = QThread::create([this]() {
                const BackupManager::Result res = m_backupManager->performBackup();
                QMetaObject::invokeMethod(this, [this, res]() {
                    if (!res.success) {
                        m_result->setText(UiKit::errorHtml(tr("No se pudo crear la copia de seguridad automática."))
                                          + "<br>" + res.errorMessage.toHtmlEscaped());
                    } else {
                        statusBar()->showMessage(
                            tr("Copia de seguridad creada (%1).").arg(res.backupPath), 8000);
                    }
                }, Qt::QueuedConnection);
            });
            connect(worker, &QThread::finished, worker, &QObject::deleteLater);
            worker->start();
        });
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::buildUi()
{
    setObjectName("MainWindow");
    setWindowTitle("La Ideal");
    setLocale(QLocale(QLocale::Spanish, QLocale::Spain));
    resize(1240, 760);

    // Actions (names as in the former .ui: the on_action<Name>_triggered slots connect by name).
    QAction *actionRecibo = new QAction(tr("Recibo"), this);
    actionRecibo->setObjectName("actionRecibo");
    actionRecibo->setToolTip(tr("Imprimir recibo"));
    actionRecibo->setShortcut(QKeySequence("Alt+P, R"));
    QAction *actionFactura = new QAction(tr("Factura"), this);
    actionFactura->setObjectName("actionFactura");
    actionFactura->setToolTip(tr("Imprimir factura"));
    actionFactura->setShortcut(QKeySequence("Alt+P, F"));
    QAction *actionRecogida_de_prendas = new QAction(tr("Recogida de prendas"), this);
    actionRecogida_de_prendas->setObjectName("actionRecogida_de_prendas");
    actionRecogida_de_prendas->setShortcut(QKeySequence("Alt+R"));
    QAction *actionFormulario_facturas = new QAction(tr("Añadir factura de gastos"), this);
    actionFormulario_facturas->setObjectName("actionFormulario_facturas");
    actionFormulario_facturas->setShortcut(QKeySequence("Alt+F"));
    QAction *actionGenerar_contabilidad = new QAction(tr("Generar contabilidad"), this);
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
    QAction *actionFactura_completa = new QAction(tr("Factura completa"), this);
    actionFactura_completa->setObjectName("actionFactura_completa");
    actionFactura_completa->setToolTip(tr("Imprimir factura completa"));
    actionFactura_completa->setShortcut(QKeySequence("Alt+P, Alt+F"));
    QAction *actionCerrar = new QAction(tr("Cerrar"), this);
    actionCerrar->setObjectName("actionCerrar");
    actionCerrar->setToolTip(tr("Cerrar aplicación"));
    actionCerrar->setShortcut(QKeySequence("Alt+C"));
    QAction *actionRevertir_contabilidad = new QAction(tr("Revertir contabilidad"), this);
    actionRevertir_contabilidad->setObjectName("actionRevertir_contabilidad");
    actionRevertir_contabilidad->setShortcut(QKeySequence("Alt+Shift+R"));
    QAction *actionAnadir_nuevas_prendas = new QAction(tr("Añadir nuevas prendas"), this);
    actionAnadir_nuevas_prendas->setObjectName("actionAnadir_nuevas_prendas");
    actionAnadir_nuevas_prendas->setToolTip(tr("Añadir nuevas prendas a un ticket guardado"));
    actionAnadir_nuevas_prendas->setShortcut(QKeySequence("Alt+Shift+N"));
    QAction *actionLimpiar_base_de_datos = new QAction(tr("Limpiar base de datos"), this);
    actionLimpiar_base_de_datos->setObjectName("actionLimpiar_base_de_datos");
    actionLimpiar_base_de_datos->setToolTip(tr("Limpiar comas ',' en importes guardados en la base de datos"));
    actionLimpiar_base_de_datos->setShortcut(QKeySequence("Alt+L"));
    QAction *actionCrear_hash_en_ingresos = new QAction(tr("Crear hash en ingresos"), this);
    actionCrear_hash_en_ingresos->setObjectName("actionCrear_hash_en_ingresos");
    actionCrear_hash_en_ingresos->setToolTip(tr("Crear hashes para los recibos que no tengan ya un hash en ingresos"));
    QAction *actionAnular_factura_verifactu = new QAction(tr("Anular factura Verifactu..."), this);
    actionAnular_factura_verifactu->setObjectName("actionAnular_factura_verifactu");
    actionAnular_factura_verifactu->setToolTip(tr("Anular una factura previamente enviada a la AEAT a través de Verifactu"));
    QAction *actionRectificar_factura_verifactu = new QAction(tr("Rectificar factura Verifactu..."), this);
    actionRectificar_factura_verifactu->setObjectName("actionRectificar_factura_verifactu");
    actionRectificar_factura_verifactu->setToolTip(tr("Emitir una factura rectificativa (R1-R5) de una factura previamente enviada a la AEAT (Art. 8.2.a RD 1007/2023)"));
    QAction *actionExportar_registros_aeat = new QAction(tr("Exportar registros AEAT (XML)..."), this);
    actionExportar_registros_aeat->setObjectName("actionExportar_registros_aeat");
    actionExportar_registros_aeat->setToolTip(tr("Exporta los XML AEAT de los tickets de un rango de fechas a un único archivo (Art. 14.1 RD 1007/2023)"));
    QAction *actionMostrar_log = new QAction(tr("Log de depuración..."), this);
    actionMostrar_log->setObjectName("actionMostrar_log");
    actionMostrar_log->setToolTip(tr("Muestra la ubicación del archivo de log para enviar al soporte técnico"));
    QAction *actionAcerca_de_Verifactu = new QAction(tr("Acerca de Verifactu..."), this);
    actionAcerca_de_Verifactu->setObjectName("actionAcerca_de_Verifactu");
    actionAcerca_de_Verifactu->setToolTip(tr("Declaración responsable del productor del sistema informático (Art. 13 RD 1007/2023)"));
    QAction *actionHacer_copia_de_seguridad = new QAction(tr("Hacer copia de seguridad ahora..."), this);
    actionHacer_copia_de_seguridad->setObjectName("actionHacer_copia_de_seguridad");
    actionHacer_copia_de_seguridad->setToolTip(tr("Crea una copia íntegra de la base de datos en la carpeta de copias (Art. 8.2.c RD 1007/2023)"));
    QAction *actionBuscar_actualizaciones = new QAction(tr("Buscar actualizaciones..."), this);
    actionBuscar_actualizaciones->setObjectName("actionBuscar_actualizaciones");
    actionBuscar_actualizaciones->setToolTip(tr("Comprueba en GitHub si hay una versión más reciente y la instala"));
    QAction *actionNotas_de_la_version = new QAction(tr("Notas de la versión..."), this);
    actionNotas_de_la_version->setObjectName("actionNotas_de_la_version");
    actionNotas_de_la_version->setToolTip(tr("Muestra el historial de cambios de todas las versiones publicadas"));

    // Menus
    QMenu *menuArchivo = menuBar()->addMenu(tr("Archivo"));
    menuArchivo->setObjectName("menuArchivo");
    menuArchivo->setToolTipsVisible(true);
    menuArchivo->addAction(actionLimpiar_base_de_datos);
    menuArchivo->addAction(actionCrear_hash_en_ingresos);
    menuArchivo->addSeparator();
    menuArchivo->addAction(actionCerrar);
    QMenu *menuHerramientas = menuBar()->addMenu(tr("Herramientas"));
    menuHerramientas->setObjectName("menuHerramientas");
    menuHerramientas->setToolTipsVisible(true);
    menuHerramientas->addAction(actionRecogida_de_prendas);
    menuHerramientas->addAction(actionAnadir_nuevas_prendas);
    QMenu *menuImprimir_ticket = menuHerramientas->addMenu(tr("Imprimir"));
    menuImprimir_ticket->setObjectName("menuImprimir_ticket");
    menuImprimir_ticket->setToolTipsVisible(true);
    menuImprimir_ticket->addAction(actionRecibo);
    menuImprimir_ticket->addAction(actionFactura);
    menuImprimir_ticket->addAction(actionFactura_completa);
    menuHerramientas->addSeparator();
    menuHerramientas->addAction(actionAnular_factura_verifactu);
    menuHerramientas->addAction(actionRectificar_factura_verifactu);
    menuHerramientas->addAction(actionExportar_registros_aeat);
    menuHerramientas->addSeparator();
    menuHerramientas->addAction(actionFormulario_facturas);
    menuHerramientas->addSeparator();
    menuHerramientas->addAction(actionGenerar_contabilidad);
    menuHerramientas->addAction(actionRevertir_contabilidad);
    menuHerramientas->addSeparator();
    menuHerramientas->addAction(actionHacer_copia_de_seguridad);
    QMenu *menuVisualizar = menuBar()->addMenu(tr("Ver"));
    menuVisualizar->setObjectName("menuVisualizar");
    menuVisualizar->setToolTipsVisible(true);
    menuVisualizar->addAction(actionIngresos);
    menuVisualizar->addAction(actionGastos);
    menuVisualizar->addSeparator();
    menuVisualizar->addAction(actionListado_de_prendas);
    menuVisualizar->addAction(actionListado_de_clientes);
    menuVisualizar->addAction(actionListado_de_proveedores);
    menuVisualizar->addAction(actionListado_de_servicios);
    QMenu *menuAyuda = menuBar()->addMenu(tr("Ayuda"));
    menuAyuda->setObjectName("menuAyuda");
    menuAyuda->setToolTipsVisible(true);
    menuAyuda->addAction(actionBuscar_actualizaciones);
    menuAyuda->addAction(actionNotas_de_la_version);
    menuAyuda->addSeparator();
    menuAyuda->addAction(actionAcerca_de_Verifactu);

    ui->menuArchivo = menuArchivo;
    ui->actionMostrar_log = actionMostrar_log;

    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);

    // Heading, explanation and the windows used most, one click away -------------
    QHBoxLayout *top = new QHBoxLayout();
    const QString shop = AppSettings::instance()->businessName();
    ui->lbl_title = UiKit::heading(shop.isEmpty() ? tr("Tintorería La Ideal") : shop);
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
    QFont numberFont = ui->le_nr_ticket->font();
    numberFont.setPointSizeF(numberFont.pointSizeF() + 3);
    numberFont.setBold(true);
    ui->le_nr_ticket->setFont(numberFont);
    ui->le_nr_ticket->setMaximumWidth(160);
    ui->de_date_recep = UiKit::dateEdit(QDate::currentDate(), "de_date_recep");
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
    QFont totalFont = lblTotal->font();
    totalFont.setPointSizeF(totalFont.pointSizeF() + 3);
    totalFont.setBold(true);
    lblTotal->setFont(totalFont);
    garmentFooter->addWidget(lblTotal);
    ui->le_cost_total = new QLineEdit();
    ui->le_cost_total->setObjectName("le_cost_total");
    ui->le_cost_total->setReadOnly(true);   // always the sum of the rows
    ui->le_cost_total->setFont(totalFont);
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
    migrateDatabase(db);

    // Settings action - prepended to Archivo menu
    QAction *actionConfig = new QAction(tr("Configuración..."), this);
    connect(actionConfig, &QAction::triggered, this, [this]() {
        SettingsDialog dlg(this);
        QList<QPair<QString, QString>> certificates;
        for (const AeatCertificate::Info &c : AeatCertificate::personalCertificates())
            certificates << qMakePair(tr("%1 (caduca el %2)").arg(c.subject, c.expiry.toString("dd-MM-yyyy")), c.thumbprint);
        dlg.setCertificateChoices(certificates);
        dlg.setDirectPendingCount(aeatPendingRecordCount(db));
        connect(&dlg, &SettingsDialog::aeatSelfTestRequested, &dlg,
                [this, &dlg](const QString &nif, const QString &name, const QString &thumbprint,
                             const QString &file, const QString &password) {
            runAeatSelfTest(&dlg, nif, name, thumbprint, file, password);
        });
        connect(&dlg, &SettingsDialog::testConnectionRequested,
                &dlg, [&dlg](const QString &nif, const QString &name,
                             const QString &serviceKey, bool production) {
            if (nif.isEmpty() || name.isEmpty() || serviceKey.isEmpty()) {
                QMessageBox::warning(&dlg, tr("Datos incompletos"),
                    tr("Introduce NIF, nombre y clave de servicio antes de probar la conexión."));
                return;
            }
            VerifactuManager mgr;
            mgr.getConfig()->setEmitterData(nif, name);
            mgr.getConfig()->setServiceKey(serviceKey);
            mgr.getConfig()->setEnvironment(production
                ? VerifactuConfig::PRODUCTION
                : VerifactuConfig::TESTING);

            VerifactuResult result = mgr.testConnection();
            const QString env = production ? "PRODUCCIÓN" : "TESTING";
            const QString endpoint = mgr.getConfig()->getEndpointUrl();
            if (result.isSuccess()) {
                QMessageBox::information(&dlg, tr("Conexión correcta"),
                    tr("Servidor accesible (%1).\n\nEntorno: %2\nNIF: %3\nEndpoint: %4")
                        .arg(result.errorDescription, env, nif, endpoint));
            } else {
                QMessageBox::warning(&dlg, tr("Conexión fallida"),
                    tr("No se pudo conectar al servidor.\n\nDetalle: %1\nEntorno: %2\nEndpoint: %3")
                        .arg(result.errorDescription, env, endpoint));
            }
        });
        if (dlg.exec() == QDialog::Accepted) {
            // Reload Verifactu with potentially new credentials
            initializeVerifactu();
        }
    });
    ui->menuArchivo->insertAction(ui->menuArchivo->actions().first(), actionConfig);
    ui->menuArchivo->insertAction(ui->menuArchivo->actions().at(1), ui->actionMostrar_log);
    ui->menuArchivo->insertSeparator(ui->menuArchivo->actions().at(2));

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
    m_result->showInfo(tr("Listo para un nuevo ticket."));
}

void MainWindow::initializeVerifactu()
{
    m_verifactuIntegration = new VerifactuIntegration(this);

    if (!m_verifactuIntegration->initialize()) {
        qWarning() << "Verifactu initialization failed:" << m_verifactuIntegration->getLastError();
        m_result->setText(UiKit::warnHtml(tr("Verifactu no está disponible: %1")
                                              .arg(m_verifactuIntegration->getLastError().toHtmlEscaped()))
                          + "<br>" + tr("Los tickets se guardan en local; configure Verifactu en Archivo → Configuración."));
    }

    connect(m_verifactuIntegration, &VerifactuIntegration::requestFinished,
            this, &MainWindow::onVerifactuRequestFinished);
    // Direct client: outcomes nobody waited for (records sent at start or retried on
    // their own) still settle their rows.
    connect(m_verifactuIntegration, &VerifactuIntegration::recordSettled, this,
            [this](const QString &invoiceId, bool cancellation, const VerifactuResult &result) {
        if (cancellation) {
            // AEAT holds the invoice cancelled: the books must say so (dated today, in an open quarter).
            const bool marked = applySettledVerifactuCancellation(db, invoiceId, QDate::currentDate(), result);
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
    });
    if (m_verifactuIntegration->directBackend() && m_verifactuIntegration->isConfigured()) {
        if (!m_verifactuIntegration->directBackend()->sendingError().isEmpty())
            m_result->setText(UiKit::warnHtml(tr("Conexión directa con la AEAT: los registros se guardan pero no se "
                                                 "pueden enviar: %1")
                                                  .arg(m_verifactuIntegration->directBackend()->sendingError().toHtmlEscaped())));
        continueDirectChain();
    }
}

void MainWindow::continueDirectChain()
{
    // Before the first direct record: continue the issuer's chain after the last record
    // the gateway sent (AEAT's newest registration, or a newer stored cancellation).
    m_verifactuIntegration->directBackend()->continueChainFromAeat(
        verifactuStoredRecordXmls(db), [this](bool ok, const QString &message) {
            qDebug() << "MainWindow: direct AEAT chain -" << ok << message;
            if (!ok)
                m_result->setText(UiKit::warnHtml(tr("Conexión directa con la AEAT: %1").arg(message.toHtmlEscaped())));
            statusBar()->showMessage(tr("Conexión directa con la AEAT: %1").arg(message), 10000);
        });
}

void MainWindow::runAeatSelfTest(QWidget *parent, const QString &nif, const QString &name, const QString &thumbprint,
                                 const QString &certificateFile, const QString &certificatePassword)
{
    if (nif.isEmpty() || name.isEmpty()) {
        QMessageBox::warning(parent, tr("Datos incompletos"), tr("Introduce el NIF y el nombre del emisor antes de la prueba."));
        return;
    }
    QDialog dlg(parent);
    UiKit::setUpDialog(&dlg, tr("Prueba con la AEAT"), 620);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(UiKit::introPanel(tr("Registra, consulta y anula una factura de prueba en el entorno de pruebas "
                                           "de la AEAT, sin efectos tributarios, con el certificado elegido.")));
    auto *panel = new UiKit::ResultPanel(tr("Conectando con la AEAT..."));
    panel->setObjectName("lblSelfTest");
    layout->addWidget(panel);
    layout->addLayout(UiKit::closeRow(&dlg));

    AeatDirectBackend::Config c;
    c.testEnvironment = true;
    c.issuerNif = nif;
    c.issuerName = name;
    c.system = { name, nif, QStringLiteral("LAIDEAL"), QStringLiteral("LI"),
                 QStringLiteral("%1.%2").arg(PROJECT_VERSION_MAJOR).arg(PROJECT_VERSION_MINOR),
                 AppSettings::instance()->aeatInstallationNumber() };
    c.certificateThumbprint = thumbprint;
    c.certificatePath = certificateFile;
    c.certificatePassword = certificatePassword;
    AeatSelfTest selfTest(c, db);
    QStringList lines;
    connect(&selfTest, &AeatSelfTest::progress, &dlg, [&lines, panel](bool ok, const QString &text) {
        lines << (ok ? UiKit::okHtml(text.toHtmlEscaped()) : UiKit::errorHtml(text.toHtmlEscaped()));
        panel->setText(lines.join("<br>"));
    });
    QTimer::singleShot(0, &selfTest, &AeatSelfTest::run);
    dlg.exec();
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
    ui->de_date_recep->setDate(QDate::currentDate());
    ui->pb_payment->setChecked(false);
    on_pb_payment_toggled(false);
}

/********************************************************************************************
 * CUSTOM FUNCTIONS
 *******************************************************************************************/

void MainWindow::setNextTicketNumber()
{
    ui->le_nr_ticket->setText(QString::number(readMaxValueInColumnFromTable(db, "n_recibo", "ingresos") + 1));
}

void MainWindow::populateCbClient()
{
    ui->cb_client->addItems(readColumnFromTable(db, "nombre", "clientes", ""));
    ui->cb_client->setCurrentText("");
}

void MainWindow::resizeTable()
{
    for (int row = 0; row < pbAddedRows; row++) {
        ui->table_ticket->removeRow(ui->table_ticket->rowCount() - 1);
    }
    pbAddedRows = 0;
}

void MainWindow::setServiceToCb(int initialRow = 0)
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

void MainWindow::setGarmentToCbAndPopulate(int initialRow = 0)
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
    auto *amount = new QTableWidgetItem(QString::number(importe, 'f', 2));
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
    if (ui->cb_client->currentText().trimmed().isEmpty())
        return refuse(tr("No se ha introducido ningún cliente."));
    double totalCost = 0.0;
    bool sizePending = false;   // an m2 garment can be left at the shop before it is measured
    for (int row = 0; row < ui->table_ticket->rowCount(); row++) {
        if (!ui->table_ticket->item(row, TABLE_TICKET_PRIC))
            continue;
        auto *cbGarment = qobject_cast<QComboBox *>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
        QTableWidgetItem *qty = ui->table_ticket->item(row, TABLE_TICKET_QNTY);
        const QString garment = cbGarment ? cbGarment->currentText() : QString();
        if (garment.isEmpty() && ui->table_ticket->item(row, TABLE_TICKET_PRIC)->text().isEmpty())
            continue;   // an empty slot
        if (!qty || qty->text().toInt() == 0)
            return refuse(tr("La prenda de la fila %1 no tiene cantidad.").arg(row + 1));
        if (garment.isEmpty())
            return refuse(tr("La fila %1 tiene importe pero no prenda.").arg(row + 1));
        QTableWidgetItem *size = ui->table_ticket->item(row, TABLE_TICKET_SIZE);
        if (garmentUnmeasured(garment, size ? size->text() : QString()))
            sizePending = true;
        const double price = ui->table_ticket->item(row, TABLE_TICKET_PRIC)->text().replace(',', '.').toDouble();
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

QString MainWindow::removeSpecialChar(QString str)
{
    // Logic lives in sql_lite::removeSpecialChars (unit-tested); kept as a thin
    // member so the existing call sites stay unchanged.
    return removeSpecialChars(str);
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
        currentClient = removeSpecialChar(currentClient.simplified().toLower());
        bool clientFound = false;
        for (int idx = 0; idx < ui->cb_client->count(); idx++) {
            QString clientInCb = removeSpecialChar(ui->cb_client->itemText(idx).simplified().toLower());
            if (currentClient == clientInCb)
                clientFound = true;
        }
        if (!clientFound)
            addNewClient(db, ui->cb_client->currentText(), ui->le_phone->text(), ui->le_addr->text(), ui->le_mobile->text());
        else {
            qDebug() << "checkClientData: client found in database after removing special characters like accents or 'ñ'. "
                     << "The data entered for the client in this receipt has not been added to the client in the client list. "
                     << "If you want to update the data, add manually in the client list.";
            m_clientNote = tr("El cliente ya existía escrito de otra forma (tildes o 'ñ'): sus teléfonos y "
                              "dirección no se han cambiado; actualícelos en Listado de clientes si hace falta.");
        }
    }

}

QString MainWindow::verifactuSubmitInvoice(const QString &ticketNum, const QDate &invoiceDate,
                                           double totalAmount, int seq)
{
    if (!m_verifactuIntegration || !m_verifactuIntegration->isConfigured()) {
        qDebug() << "Verifactu not configured - skipping invoice submission for ticket" << ticketNum;
        return QString();
    }
    double ivaRate = AppSettings::instance()->ivaRate();
    // The InvoiceID carries the seq (bare n_recibo for seq 0, "<n>-<seq>" else)
    // so a recovered partial-pay event re-submits under the same ID it stored.
    const QString invoiceId = verifactuInvoiceId(ticketNum, seq);
    const QString reqId = m_verifactuIntegration->submitSimplifiedInvoiceAsync(
        invoiceId,
        invoiceDate,
        // Tax base from the IVA-inclusive ticket total (same split as the Facturas dialog).
        Facturas::taxBaseFromGross(totalAmount, ivaRate),
        ivaRate,
        "Servicios de lavanderia"
    );
    if (reqId.isEmpty()) {
        qWarning() << "Verifactu submit rejected for ticket" << invoiceId;
        return QString();
    }
    m_pendingSubmits.insert(reqId, { ticketNum, seq });
    statusBar()->showMessage(tr("Enviando ticket %1 a AEAT...").arg(invoiceId));
    return reqId;
}

void MainWindow::onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result)
{
    auto it = m_pendingSubmits.find(requestId);
    if (it == m_pendingSubmits.end()) return; // not one of ours (cancel/QR/etc. or other consumer)
    const QString ticketNum = it.value().ticketNum;
    const int     seq       = it.value().seq;
    const bool    lateReply = it.value().printedWithoutQr;
    m_pendingSubmits.erase(it);

    const int changed = updateTicketVerifactuFields(db, ticketNum, result, seq);
    if (changed <= 0) {
        statusBar()->showMessage(changed == 0
            ? tr("Respuesta de AEAT para el ticket %1 ignorada: la factura ya estaba registrada")
                  .arg(verifactuInvoiceId(ticketNum, seq))
            : tr("No se pudo guardar la respuesta de AEAT del ticket %1").arg(verifactuInvoiceId(ticketNum, seq)),
            15000);
        return;
    }

    if (result.isSuccess()) {
        statusBar()->showMessage(
            lateReply ? tr("AEAT ha confirmado el ticket %1 - ya se puede imprimir la factura con QR "
                           "desde Recogida de Prendas").arg(verifactuInvoiceId(ticketNum, seq))
                      : tr("Ticket %1 enviado a AEAT (CSV: %2)").arg(ticketNum, result.csv),
            lateReply ? 30000 : 10000);
    } else {
        statusBar()->showMessage(
            tr("Error al enviar ticket %1: %2").arg(ticketNum, result.errorDescription), 15000);
        qWarning() << "Verifactu submission failed for ticket" << ticketNum << "-" << result.errorDescription;
    }
}

bool MainWindow::saveTicket(double &storedTotal)
{
    // A paid garment starts PENDIENTE and the async submit handler patches
    // CSV/timestamp/estado once AEAT replies (see onVerifactuRequestFinished());
    // an unpaid one starts SIN COBRAR - there is no invoice to send yet.
    // table_ticket has a fixed set of empty row slots - only rows with a price are saved,
    // so log the count of garments actually inserted, not the slot count.
    QList<IngresoGarmentRow> rows;
    storedTotal = 0.0;
    for (int row = 0; row < ui->table_ticket->rowCount(); row++) {
        // If there is any content in price of that row then save
        QComboBox *cbGarment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
        // A slot with no garment (e.g. a quantity typed and left) is not a garment.
        if (ui->table_ticket->item(row, TABLE_TICKET_PRIC) && cbGarment && !cbGarment->currentText().isEmpty()) {
            QComboBox *cbService = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_SERV));

            IngresoGarmentRow r;
            r.nRecibo        = ui->le_nr_ticket->text();
            r.cliente        = ui->cb_client->currentText();
            r.fechaRecepcion = ui->de_date_recep->date().toString("dd-MM-yyyy");
            // A paid-at-save ticket books the payment on the reception date.
            r.fechaPago      = ui->pb_payment->isChecked() ? r.fechaRecepcion : QString("");
            r.fechaRecogida  = "";
            r.importe        = ui->table_ticket->item(row, TABLE_TICKET_PRIC)->text().replace(",",".");
            r.pagado         = ui->pb_payment->isChecked() ? "SI" : "NO";
            r.estado         = "En tienda";
            r.cantidad       = ui->table_ticket->item(row, TABLE_TICKET_QNTY)->text();
            r.prenda         = cbGarment->currentText();
            r.size           = ui->table_ticket->item(row, TABLE_TICKET_SIZE)
                                   ? ui->table_ticket->item(row, TABLE_TICKET_SIZE)->text().replace(",",".") : QString("");
            r.servicio       = cbService->currentText();
            r.observaciones  = ui->table_ticket->item(row, TABLE_TICKET_OBSE)
                                   ? ui->table_ticket->item(row, TABLE_TICKET_OBSE)->text() : QString("");
            r.editLock       = "0";
            r.hash           = genHash16();
            // An unpaid row has no invoice to send, so it is SIN COBRAR, not PENDIENTE;
            // a paid one starts PENDIENTE and the async AEAT submit patches it on reply.
            r.verifactuEstado = verifactuEstadoToString(
                r.pagado == QLatin1String("SI") ? VerifactuEstado::NotSubmitted
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
    qDebug() << "saveTicket: ticket" << ui->le_nr_ticket->text()
             << "-" << rows.size() << "garment(s) saved, total" << storedTotal;
    return true;
}

bool MainWindow::printRecibo()
{
    // Save-time print: AEAT submission is in flight (async). DB still has empty CSV,
    // so the QR cannot be fetched yet. Print without QR/CSV; the customer can be
    // given a reprint via RecogPrendas after AEAT replies.
    Imprimir *ui_impr;
    ui_impr = new Imprimir(db, this);
    ui_impr->isRecibo = true;
    ui_impr->isCompleteInvoice = false;
    ui_impr->verifactuIntegration = nullptr;
    ui_impr->le_n_ticket->setText(ui->le_nr_ticket->text());
    ui_impr->getTicketInfo();
    ui_impr->buildTicket(true, ui->pb_payment->isChecked());
    if (!AppSettings::instance()->enablePrinting())
        return false;
    const bool printed = ui_impr->printTicket();
    ui_impr->buildTicket(false, ui->pb_payment->isChecked());
    return ui_impr->printTicket() && printed;
}

bool MainWindow::printFra(const QPixmap &qrCode)
{
    Imprimir *ui_impr;
    ui_impr = new Imprimir(db, this);
    ui_impr->isRecibo = false;
    ui_impr->isCompleteInvoice = false;
    ui_impr->verifactuIntegration = nullptr;
    ui_impr->qrCode = qrCode;
    ui_impr->le_n_ticket->setText(ui->le_nr_ticket->text());
    ui_impr->getTicketInfo();
    ui_impr->buildTicket(true, false);
    if (!AppSettings::instance()->enablePrinting())
        return false;
    const bool printed = ui_impr->printTicket();
    ui_impr->buildTicket(false, false);
    return ui_impr->printTicket() && printed;
}

/********************************************************************************************
 * FUNCTIONS FOR WIDGETS
 *******************************************************************************************/

void MainWindow::on_pb_payment_toggled(bool checked)
{
    ui->lbl_payment_badge->setText(checked ? UiKit::okHtml(tr("SÍ")) : UiKit::errorHtml(tr("NO")));
}

void MainWindow::on_pb_save_clicked()
{
    {
        if (validateTicket()) {
            m_clientNote.clear();
            checkClientData();
            const QString ticketNum   = ui->le_nr_ticket->text();
            const QDate   invoiceDate = ui->de_date_recep->date();
            const bool    isPaid      = ui->pb_payment->isChecked();
            QString printedWhat;

            // The invoice amount is what was stored, row by row, not the on-screen total.
            double totalAmount = 0.0;
            if (!saveTicket(totalAmount)) {
                // Nothing is sent nor printed for a ticket that was not stored whole.
                m_result->setText(UiKit::errorHtml(tr("No se pudo guardar el ticket %1 completo.").arg(ticketNum))
                                  + "<br>" + tr("No se ha guardado ninguna prenda ni se ha enviado a AEAT o impreso. "
                                                "Revise el log (Archivo → Log de depuración) antes de repetirlo."));
                return;
            }
            if (isPaid) {
                const QString reqId = verifactuSubmitInvoice(ticketNum, invoiceDate, totalAmount);

                QPixmap qrCode;
                bool gotSuccessfulReply = false;

                if (!reqId.isEmpty()) {
                    QEventLoop loop;
                    QTimer timeout;
                    timeout.setSingleShot(true);
                    QMetaObject::Connection conn = connect(m_verifactuIntegration,
                        &VerifactuIntegration::requestFinished, this,
                        [&](const QString &id, const VerifactuResult &res) {
                            if (id != reqId) return;
                            gotSuccessfulReply = res.isSuccess() && !res.qrCode.isNull();
                            qrCode = res.qrCode;
                            loop.quit();
                        });
                    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
                    timeout.start(3000);
                    loop.exec();
                    QObject::disconnect(conn);
                }

                qDebug() << "saveTicket: ticket" << ticketNum
                         << "isPaid=true gotSuccessfulReply=" << gotSuccessfulReply
                         << "(true -> printFra with QR, false -> printRecibo with Importe pagado)";
                if (gotSuccessfulReply) {
                    printedWhat = printFra(qrCode) ? tr("Factura con QR impresa.") : QString();
                } else {
                    // AEAT has not confirmed within the bounded wait, but the request
                    // may still be in flight (the transport timeout is longer). Print
                    // the paid recibo now and flag the event so a late confirmation
                    // tells the operator the factura with QR can be printed.
                    printedWhat = printRecibo() ? tr("Recibo impreso (pagado); la factura con QR se puede "
                                                     "reimprimir en Recogida cuando AEAT confirme.") : QString();
                    auto it = m_pendingSubmits.find(reqId);
                    if (it != m_pendingSubmits.end())
                        it.value().printedWithoutQr = true;
                }
            } else {
                printedWhat = printRecibo() ? tr("Recibo impreso.") : QString();
            }
            const int garments = [this]() {
                int n = 0;
                for (int row = 0; row < ui->table_ticket->rowCount(); ++row) {
                    auto *garment = qobject_cast<QComboBox *>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
                    if (ui->table_ticket->item(row, TABLE_TICKET_PRIC) && garment && !garment->currentText().isEmpty())
                        ++n;
                }
                return n;
            }();
            const QString client = ui->cb_client->currentText();
            resetAllContents();
            m_result->setText(UiKit::okHtml(tr("Ticket %1 guardado: %2, %3 prenda(s), %4 €, %5.")
                                                .arg(ticketNum, client.toHtmlEscaped()).arg(garments)
                                                .arg(moneyText(totalAmount).replace('.', ','),
                                                     isPaid ? tr("pagado") : tr("sin cobrar")))
                              + "<br>" + (printedWhat.isEmpty()
                                              ? tr("No se ha impreso (impresión desactivada o impresora sin respuesta).")
                                              : printedWhat)
                              + (isPaid ? "<br>" + tr("El envío a AEAT se confirma en la barra de estado.") : QString())
                              + (m_clientNote.isEmpty() ? QString() : "<br>" + UiKit::warnHtml(m_clientNote)));
        }
    }
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
                const double priceValue = priceItem->text().replace(',', '.').toDouble();
                priceItem->setText(moneyText(priceValue));
                priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                totalPrice += roundToCents(priceValue);
            }
        }
        ui->le_cost_total->setText(moneyText(totalPrice));
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
    QCoreApplication::quit();
}

void MainWindow::on_actionIngresos_triggered()
{
    QString title = "Ingresos";
    Listado *ui_listado;
    ui_listado = new Listado(db, this);
    ui_listado->tableName = "ingresos";    ui_listado->setObjectName(title);
    ui_listado->lbl_title->setText(title);
    ui_listado->setWindowTitle(title);
    ui_listado->populateTable();
    connect(ui_listado, &Listado::populateClientes, this, &MainWindow::repopulateClientes);
    connect(ui_listado, &Listado::populatePrendas, this, &MainWindow::repopulatePrendas);
    ui_listado->show();
}

void MainWindow::on_actionGastos_triggered()
{
    QString title = "Gastos";
    Listado *ui_listado;
    ui_listado = new Listado(db, this);
    ui_listado->tableName = "gastos";    ui_listado->setObjectName(title);
    ui_listado->lbl_title->setText(title);
    ui_listado->setWindowTitle(title);
    ui_listado->populateTable();
    connect(ui_listado, &Listado::populateClientes, this, &MainWindow::repopulateClientes);
    connect(ui_listado, &Listado::populatePrendas, this, &MainWindow::repopulatePrendas);
    ui_listado->show();
}

void MainWindow::repopulatePrendas()
{
    setGarmentToCbAndPopulate(0);
    cleanDatabase(false);
}

void MainWindow::on_actionListado_de_prendas_triggered()
{
    QString title = "Listado de prendas";
    Listado *ui_listado;
    ui_listado = new Listado(db, this);
    ui_listado->tableName = "prendas";    ui_listado->setObjectName(title);
    ui_listado->lbl_title->setText(title);
    ui_listado->setWindowTitle(title);
    ui_listado->populateTable();
    connect(ui_listado, &Listado::populateClientes, this, &MainWindow::repopulateClientes);
    connect(ui_listado, &Listado::populatePrendas, this, &MainWindow::repopulatePrendas);
    ui_listado->show();
}

void MainWindow::repopulateClientes()
{
    populateCbClient();
}

void MainWindow::on_actionListado_de_clientes_triggered()
{
    QString title = "Listado de clientes";
    Listado *ui_listado;
    ui_listado = new Listado(db, this);
    ui_listado->tableName = "clientes";    ui_listado->setObjectName(title);
    ui_listado->lbl_title->setText(title);
    ui_listado->setWindowTitle(title);
    ui_listado->populateTable();
    connect(ui_listado, &Listado::populateClientes, this, &MainWindow::repopulateClientes);
    connect(ui_listado, &Listado::populatePrendas, this, &MainWindow::repopulatePrendas);
    ui_listado->show();
}

void MainWindow::on_actionListado_de_proveedores_triggered()
{
    QString title = "Listado de proveedores";
    Listado *ui_listado;
    ui_listado = new Listado(db, this);
    ui_listado->tableName = "proveedores";    ui_listado->setObjectName(title);
    ui_listado->lbl_title->setText(title);
    ui_listado->setWindowTitle(title);
    ui_listado->populateTable();
    connect(ui_listado, &Listado::populateClientes, this, &MainWindow::repopulateClientes);
    connect(ui_listado, &Listado::populatePrendas, this, &MainWindow::repopulatePrendas);
    ui_listado->show();
}

void MainWindow::on_actionListado_de_servicios_triggered()
{
    QString title = "Listado de servicios";
    Listado *ui_listado;
    ui_listado = new Listado(db, this);
    ui_listado->tableName = "servicios";    ui_listado->setObjectName(title);
    ui_listado->lbl_title->setText(title);
    ui_listado->setWindowTitle(title);
    ui_listado->populateTable();
    connect(ui_listado, &Listado::populateClientes, this, &MainWindow::repopulateClientes);
    connect(ui_listado, &Listado::populatePrendas, this, &MainWindow::repopulatePrendas);
    ui_listado->show();
}

void MainWindow::on_actionRecogida_de_prendas_triggered()
{
    RecogPrendas *ui_recog;
    ui_recog = new RecogPrendas(db, this);
    ui_recog->m_verifactuIntegration = m_verifactuIntegration;
    //ui_recog->setWindowState(Qt::WindowMaximized);
    ui_recog->show();
}

void MainWindow::on_actionRecibo_triggered()
{
    Imprimir *ui_impr;
    ui_impr = new Imprimir(db, this);
    ui_impr->isRecibo = true;
    ui_impr->isCompleteInvoice = false;
    ui_impr->verifactuIntegration = m_verifactuIntegration;
    ui_impr->setWindowTitle("Imprimir recibo");
    ui_impr->show();
}

void MainWindow::on_actionFactura_triggered()
{
    Imprimir *ui_impr;
    ui_impr = new Imprimir(db, this);
    ui_impr->isRecibo = false;
    ui_impr->isCompleteInvoice = false;
    ui_impr->verifactuIntegration = m_verifactuIntegration;
    ui_impr->setWindowTitle("Imprimir factura");
    ui_impr->show();
}

void MainWindow::on_actionFactura_completa_triggered()
{
    Imprimir *ui_impr;
    ui_impr = new Imprimir(db, this);
    ui_impr->isRecibo = false;
    ui_impr->isCompleteInvoice = true;
    ui_impr->verifactuIntegration = m_verifactuIntegration;
    ui_impr->setWindowTitle("Imprimir factura completa");
    ui_impr->show();
}

void MainWindow::on_actionGenerar_contabilidad_triggered()
{
    Contabilidad *ui_contabilidad;
    ui_contabilidad = new Contabilidad(db, this);
    ui_contabilidad->show();
}

void MainWindow::on_actionRevertir_contabilidad_triggered()
{
    Contabilidad *ui_rev_cont;
    ui_rev_cont = new Contabilidad(db, this);
    ui_rev_cont->revertirOn = true;
    ui_rev_cont->resetAllContents();
    ui_rev_cont->show();
}

void MainWindow::on_actionFormulario_facturas_triggered()
{
    Facturas *ui_facturas;
    ui_facturas = new Facturas(db, this);
    ui_facturas->populateEmpresas();
    ui_facturas->populateServicios();
    ui_facturas->show();
}

void MainWindow::on_actionLimpiar_base_de_datos_triggered()
{
    cleanDatabase(true);
}

void MainWindow::cleanDatabase(bool print)
{
    // Change the cursor to a loading icon
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int gastosCnt = updateComasInDecimalData(db, "gastos", "importe");
    int ingresosCnt = updateComasInDecimalData(db, "ingresos", "importe");
    ingresosCnt += updateComasInDecimalData(db, "ingresos", "size");
    int prendasCnt = updateComasInDecimalData(db, "prendas", "precio_limpieza");
    prendasCnt += updateComasInDecimalData(db, "prendas", "precio_plancha");
    // Restore the cursor to default
    QApplication::restoreOverrideCursor();
    if (print)
        m_result->setText(UiKit::okHtml(tr("Limpieza de la base de datos terminada."))
                          + "<br>" + tr("Importes con ',' corregidos: %1 en gastos, %2 en ingresos, %3 en la lista de prendas.")
                                         .arg(gastosCnt).arg(ingresosCnt).arg(prendasCnt));
}

void MainWindow::on_actionAnadir_nuevas_prendas_triggered()
{
    AddGarment *ui_add_garment;
    ui_add_garment = new AddGarment(db, this);
    // Submitted now, like a paid ticket on save; if AEAT does not answer, the row stays
    // PENDIENTE for the startup recovery.
    connect(ui_add_garment, &AddGarment::paidGarmentSaved, this,
            [this](const QString &ticketNum, const QDate &paymentDate, double amount) {
        verifactuSubmitInvoice(ticketNum, paymentDate, amount, 0);
    });
    ui_add_garment->show();
}

void MainWindow::on_actionCrear_hash_en_ingresos_triggered()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);

    db.open();

    // Phase A: fill empty / NULL hashes. UPDATE keyed on rowid (SQLite's
    // intrinsic per-row identifier) instead of the all-fields equality used
    // by the legacy version, which could stamp two rows with the same hash
    // when ticket lines happened to be identical across every column.
    int filled = 0;
    {
        QSqlQuery q(db);
        if (q.exec("SELECT rowid FROM ingresos WHERE hash IS NULL OR hash = ''")) {
            QList<qint64> emptyRowids;
            while (q.next())
                emptyRowids.append(q.value(0).toLongLong());
            QSqlQuery u(db);
            u.prepare("UPDATE ingresos SET hash = :h WHERE rowid = :rid");
            for (qint64 rid : emptyRowids) {
                u.bindValue(":h",   genHash16());
                u.bindValue(":rid", rid);
                if (u.exec()) ++filled;
                else qWarning() << "Crear hash: fill UPDATE failed for rowid" << rid
                                 << "-" << u.lastError().text();
            }
        } else {
            qWarning() << "Crear hash: SELECT empties failed -" << q.lastError().text();
        }
    }

    // Phase B: resolve collisions (legacy data from the pre-QUuid genHash16
    // era). For every hash shared by >1 row, keep the first row's hash and
    // regenerate the rest with the new UUID-based hash so the column ends
    // up globally unique.
    int regenerated = 0;
    int remainingGroups = 0;
    {
        QSqlQuery q(db);
        if (q.exec("SELECT hash FROM ingresos "
                   "WHERE hash IS NOT NULL AND hash != '' "
                   "GROUP BY hash HAVING COUNT(*) > 1")) {
            QStringList collidingHashes;
            while (q.next())
                collidingHashes.append(q.value(0).toString());

            QSqlQuery picker(db);
            QSqlQuery upd(db);
            upd.prepare("UPDATE ingresos SET hash = :h WHERE rowid = :rid");
            for (const QString &h : collidingHashes) {
                picker.prepare("SELECT rowid FROM ingresos WHERE hash = :h ORDER BY rowid");
                picker.bindValue(":h", h);
                if (!picker.exec()) {
                    qWarning() << "Crear hash: group SELECT failed for hash" << h
                               << "-" << picker.lastError().text();
                    continue;
                }
                bool skipFirst = true;
                while (picker.next()) {
                    if (skipFirst) { skipFirst = false; continue; }
                    upd.bindValue(":h",   genHash16());
                    upd.bindValue(":rid", picker.value(0).toLongLong());
                    if (upd.exec()) ++regenerated;
                    else qWarning() << "Crear hash: regen UPDATE failed for rowid"
                                     << picker.value(0) << "-" << upd.lastError().text();
                }
            }
            remainingGroups = collidingHashes.size(); // before regen
        } else {
            qWarning() << "Crear hash: collision SELECT failed -" << q.lastError().text();
        }
    }

    // Phase C: defensive re-check - after Phase B the column should be unique;
    // anything still in there indicates a logic error worth surfacing.
    int stillColliding = 0;
    {
        QSqlQuery q(db);
        if (q.exec("SELECT COUNT(*) FROM (SELECT hash FROM ingresos "
                   "WHERE hash IS NOT NULL AND hash != '' "
                   "GROUP BY hash HAVING COUNT(*) > 1)")) {
            if (q.next()) stillColliding = q.value(0).toInt();
        }
    }
    db.close();

    QApplication::restoreOverrideCursor();

    qDebug() << "Crear hash: filled=" << filled
             << "collision_groups=" << remainingGroups
             << "regenerated=" << regenerated
             << "still_colliding_groups=" << stillColliding;

    const QString body = tr("Tabla de ingresos: %1 fila(s) sin hash rellenadas, %2 grupo(s) de hash duplicado "
                            "detectados, %3 fila(s) regeneradas.").arg(filled).arg(remainingGroups).arg(regenerated);
    m_result->setText(stillColliding > 0
        ? UiKit::warnHtml(tr("Quedan %1 grupo(s) de hash duplicado: vuelva a ejecutar la acción; si persiste, "
                             "consulte el log.").arg(stillColliding)) + "<br>" + body
        : UiKit::okHtml(tr("Hashes de ingresos revisados.")) + "<br>" + body);
}

void MainWindow::on_actionAnular_factura_verifactu_triggered()
{
    if (!m_verifactuIntegration || !m_verifactuIntegration->isConfigured()) {
        qWarning() << "Cancel invoice action: Verifactu not configured";
        m_result->setText(UiKit::errorHtml(tr("Verifactu no está configurado."))
                          + "<br>" + tr("Configure las credenciales en Archivo → Configuración."));
        return;
    }
    CancelInvoiceDialog dlg(db, this);
    dlg.m_verifactu = m_verifactuIntegration;
    dlg.exec();
}

// Art. 8.2.a RD 1007/2023 - rectificativa (R1-R5) is one of the two legal
// correction paths for an already-registered factura (the other is anulacion).
void MainWindow::on_actionRectificar_factura_verifactu_triggered()
{
    if (!m_verifactuIntegration || !m_verifactuIntegration->isConfigured()) {
        qWarning() << "Rectify invoice action: Verifactu not configured";
        m_result->setText(UiKit::errorHtml(tr("Verifactu no está configurado."))
                          + "<br>" + tr("Configure las credenciales en Archivo → Configuración."));
        return;
    }
    RectifyInvoiceDialog dlg(db, this);
    dlg.m_verifactu = m_verifactuIntegration;
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

void MainWindow::on_actionMostrar_log_triggered()
{
    const QString path = AppLogger::logFilePath();
    m_result->setText(UiKit::fileLinkHtml(path, tr("Log de depuración")) + "<br>"
                      + tr("Envíe este archivo al soporte técnico cuando tenga un problema (clic para abrirlo)."));
}

// Declaración responsable visible in the software, as required by Art. 13
// RD 1007/2023. Producer data is taken from AppSettings (the same NIF/name
// used as Verifactu emitter, since the software is deployed bespoke for the
// business that uses it). The compliance text is fixed; only producer data
// and the software version vary across installations.
void MainWindow::on_actionAcerca_de_Verifactu_triggered()
{
    AppSettings *s = AppSettings::instance();
    const QString nif     = s->verifactuNif().isEmpty()    ? QStringLiteral("-") : s->verifactuNif();
    const QString name    = !s->verifactuName().isEmpty()  ? s->verifactuName()
                          : (!s->businessName().isEmpty() ? s->businessName() : QStringLiteral("-"));
    const QString address = s->businessAddress().isEmpty() ? QStringLiteral("-") : s->businessAddress();
    const QString city    = s->businessCity().isEmpty()    ? QStringLiteral("-") : s->businessCity();
    const QString version = QStringLiteral("%1.%2").arg(PROJECT_VERSION_MAJOR).arg(PROJECT_VERSION_MINOR);
    const QString software = QStringLiteral("La Ideal");

    QDialog dlg(this);
    UiKit::setUpDialog(&dlg, "Acerca de Verifactu", 600);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);

    QLabel *body = UiKit::introPanel(QString());
    body->setTextFormat(Qt::RichText);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->setText(
        QString("<h3 align=\"center\">DECLARACIÓN RESPONSABLE</h3>"
                "<p align=\"center\"><i>Artículo 13 del Real Decreto 1007/2023, de 5 de diciembre</i></p>"
                "<p><b>%1</b>, con NIF <b>%2</b> y domicilio en %3, %4, "
                "en calidad de productor del sistema informático de facturación denominado "
                "<b>%5</b> versión <b>%6</b>,</p>"
                "<p><b>DECLARA</b> bajo su responsabilidad:</p>"
                "<p>Que el sistema informático arriba identificado cumple con todos los requisitos "
                "establecidos en el Real Decreto 1007/2023, de 5 de diciembre, por el que se aprueba "
                "el Reglamento que establece los requisitos que deben adoptar los sistemas informáticos "
                "de facturación, y en la Orden HAC/1177/2024, de 17 de octubre, que lo desarrolla.</p>"
                "<p>Que el sistema opera en modalidad <b>VERI*FACTU</b>, remitiendo automáticamente "
                "los registros de facturación a la Agencia Estatal de Administración Tributaria (AEAT) "
                "en el momento de su generación.</p>"
                "<p>Que el sistema se utiliza en una instalación <b>monoperador</b>: existe un único "
                "usuario operativo, identificado de forma implícita por la sesión de Windows del puesto "
                "en el que se ejecuta. Bajo este alcance se da por cumplido el requisito de trazabilidad "
                "por usuario establecido en el artículo 8.1 del Real Decreto 1007/2023. La incorporación "
                "de un segundo operador exigirá habilitar previamente la identificación individual por "
                "evento de facturación.</p>")
            .arg(name.toHtmlEscaped(), nif.toHtmlEscaped(),
                 address.toHtmlEscaped(), city.toHtmlEscaped(),
                 software, version));
    layout->addWidget(body);
    layout->addLayout(UiKit::closeRow(&dlg));

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
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto res = m_backupManager->performBackup();
    QApplication::restoreOverrideCursor();

    if (!res.success) {
        m_result->setText(UiKit::errorHtml(tr("No se pudo crear la copia de seguridad."))
                          + "<br>" + res.errorMessage.toHtmlEscaped());
        return;
    }
    m_result->setText(UiKit::okHtml(tr("Copia de seguridad creada (%1 KB).").arg(res.bytesWritten / 1024))
                      + "<br>" + UiKit::fileLinkHtml(res.backupPath, tr("Copia")));
}
