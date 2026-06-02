#include "mainwindow.h"
#include "./ui_mainwindow.h"
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
#include "settingsdialog.h"
#include "verifactumanager.h"
#include "verifactuconfig.h"
#include "updaterdialog.h"
#include "pendingsubmitsdialog.h"
#include "version.h"
#include <QTimer>
#include <QThread>
#include <QEventLoop>
#include <QTextEdit>
#include <QFontDatabase>
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
#include <QRegularExpression>
#include <QXmlStreamWriter>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
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
                this, [this](const QString &ticketNum, const QDate &invoiceDate, double totalAmount) {
            verifactuSubmitInvoice(ticketNum, invoiceDate, totalAmount);
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
                        QMessageBox::warning(this, tr("Copia de seguridad"),
                                             tr("No se pudo crear la copia automática:\n%1")
                                                 .arg(res.errorMessage));
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

void MainWindow::mainwindowInitialSettings()
{
    migrateDatabase(db);

    // Settings action - prepended to Archivo menu
    QAction *actionConfig = new QAction(tr("Configuración..."), this);
    connect(actionConfig, &QAction::triggered, this, [this]() {
        SettingsDialog dlg(this);
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

    // Taskbar
    ui->menuArchivo->setToolTipsVisible(true);
    ui->menuHerramientas->setToolTipsVisible(true);
    ui->menuImprimir_ticket->setToolTipsVisible(true);
    ui->menuVisualizar->setToolTipsVisible(true);
    ui->menuAyuda->setToolTipsVisible(true);
    // Table settings
    ui->table_ticket->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->table_ticket->verticalHeader()->setVisible(false);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_QNTY, 60);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_GARM, 400);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_SERV, 70);
    ui->table_ticket->setColumnWidth(TABLE_TICKET_OBSE, 150);
    // Push button settings
    ui->pb_payment->setStyleSheet("background-color: red; font-size: 20px");
    resetAllContents();
}

void MainWindow::initializeVerifactu()
{
    m_verifactuIntegration = new VerifactuIntegration(this);

    if (!m_verifactuIntegration->initialize()) {
        qWarning() << "Verifactu initialization failed:" << m_verifactuIntegration->getLastError();
        QMessageBox::warning(this, "Advertencia",
            QString("No se pudo inicializar Verifactu:\n%1\n\n"
                    "Las facturas se guardarán localmente.\n"
                    "Configura Verifactu más tarde.")
            .arg(m_verifactuIntegration->getLastError()));
    }

    connect(m_verifactuIntegration, &VerifactuIntegration::requestFinished,
            this, &MainWindow::onVerifactuRequestFinished);
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

void MainWindow::setGarmentPrice(int garmentRow,
                                 QString garmentText,
                                 QString serviceText)
{
    QTableWidgetItem *qntyItem(ui->table_ticket->item(garmentRow, TABLE_TICKET_QNTY));
    QTableWidgetItem *item = new QTableWidgetItem;
    item->setText("");
    if (qntyItem) {
        // Comma-decimal normalisation + size factor live in sql_lite::garmentImporte
        // (unit-tested); see its comment for why the comma matters (m2 garments).
        QTableWidgetItem *sizeItem(ui->table_ticket->item(garmentRow, TABLE_TICKET_SIZE));
        const double importe = garmentImporte(qntyItem->text(),
                                              sizeItem ? sizeItem->text() : QString(),
                                              readGarmentPrice(db, garmentText, serviceText));
        item->setText(QString::number(importe, 'f', 2));
    }
    else {
        qWarning() << "setGarmentPrice: quantity field is empty for row" << garmentRow;
        QMessageBox::warning(nullptr, "Error en la casilla de cantidad",
                              "Cantidad de prendas está vacía.",
                              QMessageBox::Ok, QMessageBox::Ok);
    }
    ui->table_ticket->setItem(garmentRow, TABLE_TICKET_PRIC, item);
}

void MainWindow::cbGarmChanged(const QString &text)
{
    int garmentRow = ui->table_ticket->currentRow();
    QComboBox *cbService = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(garmentRow, TABLE_TICKET_SERV));
    QComboBox *cbGarment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(garmentRow, TABLE_TICKET_GARM));
    // Check garment is included in combobox
    if (cbGarment->findText(text, Qt::MatchExactly) != -1)
        setGarmentPrice(garmentRow, text, cbService->currentText());
    else if (cbGarment->currentText() == "") {
        QTableWidgetItem *item = new QTableWidgetItem;
        ui->table_ticket->setItem(garmentRow, TABLE_TICKET_PRIC, item);
    }
}

void MainWindow::cbServChanged(const QString &text)
{
    int serviceRow = ui->table_ticket->currentRow();
    QComboBox *cbGarment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(serviceRow, TABLE_TICKET_GARM));
    if (cbGarment->currentText() != "")
        setGarmentPrice(serviceRow, cbGarment->currentText(), text);
}

bool MainWindow::validateTicket()
{
    QMessageBox msgBox;
    float totalCost = 0.0;
    if (ui->cb_client->currentText().isEmpty()) {
        msgBox.setText("No se ha introducido ningún cliente.");
        msgBox.setInformativeText("No se va a guardar nada en la tabla de ingresos.");
        msgBox.exec();
        return 0;
    }
    else {
        // Calculate the expected total cost
        for (int row = 0; row < ui->table_ticket->rowCount(); row++) {
            if (ui->table_ticket->item(row, TABLE_TICKET_PRIC)) {
                if (!ui->table_ticket->item(row, TABLE_TICKET_QNTY)) {
                    msgBox.setText("El valor del IMPORTE individual de la prenda " + QString::number(row + 1) + " no se puede cambiar directamente.");
                    msgBox.setInformativeText("La cantidad no puede estar vacía. No se va a guardar nada en la tabla de ingresos.");
                    msgBox.exec();
                    return 0;
                }
                else if (ui->table_ticket->item(row, TABLE_TICKET_QNTY)->text().toInt() == 0) {
                    msgBox.setText("El valor del IMPORTE individual de la prenda " + QString::number(row + 1) + " no se puede cambiar directamente.");
                    msgBox.setInformativeText("La cantidad no puede ser 0. No se va a guardar nada en la tabla de ingresos.");
                    msgBox.exec();
                    return 0;
                }
                else {
                    QComboBox *cbGarment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
                    if (cbGarment->currentText().isEmpty()) {
                        msgBox.setText("La prenda " + QString::number(row + 1) + " no puede tener el nombre vacío.");
                        msgBox.setInformativeText("No se va a guardar nada en la tabla de ingresos.");
                        msgBox.exec();
                        return 0;
                    }
                }
                // If there is data in the row get the float and accumulate
                totalCost = totalCost + ui->table_ticket->item(row, TABLE_TICKET_PRIC)->text().toFloat();
            }
        }
        if (totalCost == 0.0) {
            QComboBox *cbGarment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(0, TABLE_TICKET_GARM));
            QString leftSide = cbGarment->currentText().left(8);
            if (leftSide == "Alfombra")
                return 1;
            msgBox.setText("La suma de los IMPORTES individuales es 0.");
            msgBox.setInformativeText("No se va a guardar nada en la tabla de ingresos.");
            msgBox.exec();
            return 0;
        }
        else {
            // If there is data in lbl_cost_total it has to match with the previous calculation
            if (ui->le_cost_total->text().toFloat() != totalCost) {
                msgBox.setText("El valor del IMPORTE TOTAL no puede ser diferente al de los IMPORTES individuales.");
                msgBox.setInformativeText("No se va a guardar nada en la tabla de ingresos.");
                msgBox.exec();
                return 0;
            }
            else {
                // if the current date belongs to a locked quarter, data cannot be saved
                if (readLockForMonthAndYear(db, "ingresos", ui->de_date_recep->date().month(), ui->de_date_recep->date().year()) == 1) {
                    msgBox.setText("No se puede introducir un nuevo recibo en un trimestre que tiene la contabilidad cerrada.");
                    msgBox.setInformativeText("No se va a guardar nada en la tabla de ingresos.");
                    msgBox.exec();
                    return 0;
                }
                else
                    return 1;
            }
        }
    }
}

QString MainWindow::removeSpecialChar(QString str)
{
    str = str.normalized(QString::NormalizationForm_D).toLatin1();
    int index = str.indexOf("?");
    while (index != -1) {
        str = str.remove(index, 1);
        index = str.indexOf("?");
    }
    return str;
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
            QMessageBox::information(this, "Listado de clientes",
                                  "Cliente encontrado en la base de datos tras suprimir carácteres especiales como tildes o 'ñ'.\n"
                                  "Los datos introducidos para el cliente en este recibo no se han añadido al cliente en el listado de clientes. "
                                  "Si se desean actualizar los datos, añadir manualmente en el listado de clientes.",
                                  QMessageBox::Ok,
                                  QMessageBox::Ok);
        }
    }

}

QString MainWindow::verifactuSubmitInvoice(const QString &ticketNum, const QDate &invoiceDate, double totalAmount)
{
    if (!m_verifactuIntegration || !m_verifactuIntegration->isConfigured()) {
        qDebug() << "Verifactu not configured - skipping invoice submission for ticket" << ticketNum;
        return QString();
    }
    double ivaRate = AppSettings::instance()->ivaRate();
    const QString reqId = m_verifactuIntegration->submitSimplifiedInvoiceAsync(
        ticketNum,
        invoiceDate,
        totalAmount / (1.0 + ivaRate / 100.0),
        ivaRate,
        "Servicios de lavanderia"
    );
    if (reqId.isEmpty()) {
        qWarning() << "Verifactu submit rejected for ticket" << ticketNum;
        return QString();
    }
    m_pendingSubmits.insert(reqId, ticketNum);
    statusBar()->showMessage(tr("Enviando ticket %1 a AEAT...").arg(ticketNum));
    return reqId;
}

void MainWindow::onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result)
{
    auto it = m_pendingSubmits.find(requestId);
    if (it == m_pendingSubmits.end()) return; // not one of ours (cancel/QR/etc. or other consumer)
    const QString ticketNum = it.value();
    m_pendingSubmits.erase(it);

    updateTicketVerifactuFields(db, ticketNum, result);

    if (result.isSuccess()) {
        statusBar()->showMessage(tr("Ticket %1 enviado a AEAT (CSV: %2)").arg(ticketNum, result.csv), 10000);
    } else {
        statusBar()->showMessage(
            tr("Error al enviar ticket %1: %2").arg(ticketNum, result.errorDescription), 15000);
        qWarning() << "Verifactu submission failed for ticket" << ticketNum << "-" << result.errorDescription;
    }
}

void MainWindow::saveTicket()
{
    // Rows are always written with verifactu_estado = PENDIENTE (or empty if Verifactu is
    // not configured for this ticket). The async submit handler patches the rows with
    // CSV/timestamp/estado once AEAT replies. See onVerifactuRequestFinished().
    qDebug() << "saveTicket: ticket" << ui->le_nr_ticket->text()
             << "rows:" << ui->table_ticket->rowCount();

    for (int row = 0; row < ui->table_ticket->rowCount(); row++) {
        // If there is any content in price of that row then save
        if (ui->table_ticket->item(row, TABLE_TICKET_PRIC)) {
            QString hash = genHash16();
            qDebug() << "saveTicket: INSERT row" << row << "ticket=" << ui->le_nr_ticket->text()
                     << "importe=" << ui->table_ticket->item(row, TABLE_TICKET_PRIC)->text()
                     << "hash=" << hash;
            db.open();
            QSqlQuery q;
            q.prepare("INSERT INTO ingresos "
                      "(n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, "
                      "cantidad, prenda, size, servicio, observaciones, edit_lock, hash, "
                      "verifactu_csv, verifactu_timestamp, verifactu_estado, verifactu_error, verifactu_url_qr, "
                      "verifactu_xml, verifactu_hash) "
                      "VALUES "
                      "(:n_recibo, :cliente, :fecha_recepcion, :fecha_pago, :fecha_recogida, :importe, :pagado, :estado, "
                      ":cantidad, :prenda, :size, :servicio, :observaciones, :edit_lock, :hash, "
                      ":verifactu_csv, :verifactu_timestamp, :verifactu_estado, :verifactu_error, :verifactu_url_qr, "
                      ":verifactu_xml, :verifactu_hash);");
            q.bindValue(":n_recibo", ui->le_nr_ticket->text());
            q.bindValue(":cliente", ui->cb_client->currentText());
            q.bindValue(":fecha_recepcion", ui->de_date_recep->date().toString("dd-MM-yyyy"));
            if (ui->pb_payment->text() == "SI")
                q.bindValue(":fecha_pago", ui->de_date_recep->date().toString("dd-MM-yyyy"));
            else
                q.bindValue(":fecha_pago", "");
            q.bindValue(":fecha_recogida", "");
            q.bindValue(":importe", ui->table_ticket->item(row, TABLE_TICKET_PRIC)->text().replace(",","."));
            q.bindValue(":pagado", ui->pb_payment->text());
            q.bindValue(":estado", "En tienda");
            q.bindValue(":cantidad", ui->table_ticket->item(row, TABLE_TICKET_QNTY)->text());
            QComboBox *cbGarment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
            q.bindValue(":prenda", cbGarment->currentText());
            if (ui->table_ticket->item(row, TABLE_TICKET_SIZE))
                q.bindValue(":size", ui->table_ticket->item(row, TABLE_TICKET_SIZE)->text().replace(",","."));
            else
                q.bindValue(":size", "");
            if (ui->table_ticket->item(row, TABLE_TICKET_OBSE))
                q.bindValue(":observaciones", ui->table_ticket->item(row, TABLE_TICKET_OBSE)->text());
            else
                q.bindValue(":observaciones", "");
            QComboBox *cbService = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_SERV));
            q.bindValue(":servicio", cbService->currentText());
            q.bindValue(":edit_lock", "0");
            q.bindValue(":hash", hash);
            q.bindValue(":verifactu_csv",       "");
            q.bindValue(":verifactu_timestamp", "");
            q.bindValue(":verifactu_estado",    verifactuEstadoToString(VerifactuEstado::NotSubmitted));
            q.bindValue(":verifactu_error",     "");
            q.bindValue(":verifactu_url_qr",    "");
            q.bindValue(":verifactu_xml",       "");
            q.bindValue(":verifactu_hash",      "");
            if (!q.exec())
                qWarning() << "saveTicket INSERT failed for ticket" << ui->le_nr_ticket->text()
                           << "row" << row << "-" << q.lastError().text();
            q.clear();
            db.close();
        }
    }
}

void MainWindow::printRecibo()
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
    ui_impr->createTicketExcel(true, ui->pb_payment->isChecked());
    if (AppSettings::instance()->enablePrinting()) {
        ui_impr->printTicket();
        ui_impr->createTicketExcel(false, ui->pb_payment->isChecked());
        ui_impr->printTicket();
    }
}

void MainWindow::printFra(const QPixmap &qrCode)
{
    Imprimir *ui_impr;
    ui_impr = new Imprimir(db, this);
    ui_impr->isRecibo = false;
    ui_impr->isCompleteInvoice = false;
    ui_impr->verifactuIntegration = nullptr;
    ui_impr->qrCode = qrCode;
    ui_impr->le_n_ticket->setText(ui->le_nr_ticket->text());
    ui_impr->getTicketInfo();
    ui_impr->createTicketExcel(true, false);
    if (AppSettings::instance()->enablePrinting()) {
        ui_impr->printTicket();
        ui_impr->createTicketExcel(false, false);
        ui_impr->printTicket();
    }
}

/********************************************************************************************
 * FUNCTIONS FOR WIDGETS
 *******************************************************************************************/

void MainWindow::on_pb_payment_toggled(bool checked)
{
    if (checked) {
        ui->pb_payment->setText("SI");
        ui->pb_payment->setStyleSheet("background-color: green; font-size: 20px");
    }
    else
    {
        ui->pb_payment->setText("NO");
        ui->pb_payment->setStyleSheet("background-color: red; font-size: 20px");
    }
}

void MainWindow::on_bb_save_reset_clicked(QAbstractButton *button)
{
    if (button == ui->bb_save_reset->button(QDialogButtonBox::Reset))
        resetAllContents();
    else if (button == ui->bb_save_reset->button(QDialogButtonBox::Save)) {
        if (validateTicket()) {
            checkClientData();
            const QString ticketNum   = ui->le_nr_ticket->text();
            const QDate   invoiceDate = ui->de_date_recep->date();
            const bool    isPaid      = ui->pb_payment->text() == "SI";
            const double  totalAmount = ui->le_cost_total->text().toDouble();

            saveTicket();
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
                if (gotSuccessfulReply)
                    printFra(qrCode);
                else
                    printRecibo();
            } else {
                printRecibo();
            }
            resetAllContents();
        }
    }
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
    QComboBox *cbGarment = qobject_cast<QComboBox*>(ui->table_ticket->cellWidget(row, TABLE_TICKET_GARM));
    if (column == TABLE_TICKET_QNTY || column == TABLE_TICKET_SIZE) {
        if (cbGarment->currentText() != "")
            cbGarmChanged(cbGarment->currentText());
    }
    else if (column == TABLE_TICKET_PRIC) {
        float totalPrice = 0.0;
        for (int rowCnt = 0; rowCnt < ui->table_ticket->rowCount(); rowCnt++) {
            QTableWidgetItem *priceItem(ui->table_ticket->item(rowCnt, column));
            if (priceItem && priceItem->text() != "" && priceItem->text().toFloat() != 0.0) {
                float priceValue = priceItem->text().toFloat();
                priceItem->setText(QString::number(priceValue, 'f', 2));
                totalPrice = totalPrice + priceValue;
            }
        }
        ui->le_cost_total->setText(QString::number(totalPrice, 'f', 2));
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
    ui_rev_cont->setWindowTitle("Revertir Contabilidad");
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
        QMessageBox::information(this, "Limpieza de la base de datos",
                                 "Se han corregido los siguientes importes decimales que se encontraban"
                                 " en la base de datos con ',' en lugar de '.':\n"
                                 + QString::number(gastosCnt) + " en la tabla de gastos.\n"
                                 + QString::number(ingresosCnt) + " en la tabla de ingresos.\n"
                                 + QString::number(prendasCnt) + " en la tabla de lista de prendas.",
                                 QMessageBox::Ok, QMessageBox::Ok);
}

void MainWindow::on_actionAnadir_nuevas_prendas_triggered()
{
    AddGarment *ui_add_garment;
    ui_add_garment = new AddGarment(db, this);
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

    QString body = tr("Se actualizó la tabla ingresos:\n"
                      "  • %1 filas con hash vacío rellenadas\n"
                      "  • %2 grupos de hash duplicado detectados\n"
                      "  • %3 filas regeneradas para resolverlos")
                       .arg(filled).arg(remainingGroups).arg(regenerated);
    if (stillColliding > 0) {
        body += tr("\n\nAtención: quedan %1 grupos colisionando tras la "
                   "regeneración. Vuelve a ejecutar la acción; si persiste, "
                   "consulta el log.").arg(stillColliding);
        QMessageBox::warning(this, tr("Crear hash en ingresos"), body);
    } else {
        QMessageBox::information(this, tr("Crear hash en ingresos"), body);
    }
}

void MainWindow::on_actionAnular_factura_verifactu_triggered()
{
    if (!m_verifactuIntegration || !m_verifactuIntegration->isConfigured()) {
        qWarning() << "Cancel invoice action: Verifactu not configured";
        QMessageBox::warning(this, "Verifactu no configurado",
                             "Verifactu no está configurado correctamente.\n"
                             "Configura las credenciales en Archivo → Configuración.",
                             QMessageBox::Ok);
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
        QMessageBox::warning(this, "Verifactu no configurado",
                             "Verifactu no está configurado correctamente.\n"
                             "Configura las credenciales en Archivo → Configuración.",
                             QMessageBox::Ok);
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
    // Step 1: date-range + output-path picker (small inline dialog)
    QDialog dlg(this);
    dlg.setWindowTitle("Exportar registros AEAT (XML)");
    QFormLayout *form = new QFormLayout(&dlg);

    QDateEdit *fromDate = new QDateEdit(QDate::currentDate().addMonths(-3), &dlg);
    fromDate->setCalendarPopup(true);
    fromDate->setDisplayFormat("dd-MM-yyyy");

    QDateEdit *toDate = new QDateEdit(QDate::currentDate(), &dlg);
    toDate->setCalendarPopup(true);
    toDate->setDisplayFormat("dd-MM-yyyy");

    form->addRow("Desde:", fromDate);
    form->addRow("Hasta:", toDate);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    const QDate from = fromDate->date();
    const QDate to   = toDate->date();
    if (from > to) {
        QMessageBox::warning(this, "Exportar registros AEAT",
                             "La fecha 'Desde' debe ser anterior o igual a 'Hasta'.",
                             QMessageBox::Ok);
        return;
    }

    const QString suggestedName = QString("aeat_registros_%1_%2.xml")
        .arg(from.toString("yyyyMMdd"), to.toString("yyyyMMdd"));
    const QString filePath = QFileDialog::getSaveFileName(
        this, "Guardar archivo de registros AEAT",
        QDir::homePath() + "/" + suggestedName, "XML (*.xml)");
    if (filePath.isEmpty()) return;

    // Step 2: query rows with non-empty stored XML. We include every row that was
    // submitted to AEAT regardless of its current local estado (ENVIADA, ANULADA,
    // RECTIFICADA) - Hacienda has the corresponding records on their side too.
    // The verifactu_xml column itself is the proof of submission.
    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT n_recibo, fecha_recepcion, importe, verifactu_csv, verifactu_xml "
              "FROM ingresos "
              "WHERE verifactu_xml IS NOT NULL AND verifactu_xml != '' "
              "ORDER BY fecha_recepcion, n_recibo");
    if (!q.exec()) {
        qWarning() << "Exportar registros AEAT: SELECT failed -" << q.lastError().text();
        db.close();
        QMessageBox::critical(this, "Exportar registros AEAT",
                              "Error al consultar la base de datos.",
                              QMessageBox::Ok);
        return;
    }

    // Step 3: write the envelope file
    QFile out(filePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        db.close();
        QMessageBox::critical(this, "Exportar registros AEAT",
                              "No se pudo abrir el archivo de salida para escritura.",
                              QMessageBox::Ok);
        return;
    }

    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement("RegistrosFacturacionLaIdeal");
    w.writeAttribute("fechaDesde", from.toString("dd-MM-yyyy"));
    w.writeAttribute("fechaHasta", to.toString("dd-MM-yyyy"));
    w.writeAttribute("generadoEl", QDateTime::currentDateTime().toString(Qt::ISODate));
    w.writeAttribute("nif",        AppSettings::instance()->verifactuNif());
    w.writeAttribute("emisor",     AppSettings::instance()->verifactuName());

    // Strip the XML declaration of each stored payload so the outer document stays
    // well-formed when payloads are concatenated.
    static const QRegularExpression xmlDeclRx(QStringLiteral("^\\s*<\\?xml[^?]*\\?>\\s*"));

    int count = 0;
    while (q.next()) {
        const QString fechaStr = q.value("fecha_recepcion").toString();
        const QDate fecha = QDate::fromString(fechaStr, "dd-MM-yyyy");
        if (!fecha.isValid() || fecha < from || fecha > to)
            continue;

        QString payload = q.value("verifactu_xml").toString();
        payload.remove(xmlDeclRx);

        w.writeStartElement("Registro");
        w.writeAttribute("nRecibo",        q.value("n_recibo").toString());
        w.writeAttribute("fechaRecepcion", fechaStr);
        w.writeAttribute("importe",        q.value("importe").toString());
        w.writeAttribute("csv",            q.value("verifactu_csv").toString());
        // Flush the writer's state before injecting raw bytes (QXmlStreamWriter
        // writes directly to the device, so this keeps the byte stream consistent).
        w.writeCharacters(QString());
        out.write(payload.toUtf8());
        w.writeEndElement(); // Registro
        ++count;
    }

    w.writeEndElement(); // RegistrosFacturacionLaIdeal
    w.writeEndDocument();
    out.close();
    db.close();

    if (count == 0) {
        QMessageBox::information(this, "Exportar registros AEAT",
            QString("No se encontraron registros enviados a AEAT entre %1 y %2.\n"
                    "El archivo se ha creado vacío.")
                .arg(from.toString("dd-MM-yyyy"), to.toString("dd-MM-yyyy")),
            QMessageBox::Ok);
    } else {
        QMessageBox::information(this, "Exportar registros AEAT",
            QString("Se han exportado %1 registros al archivo:\n%2").arg(count).arg(filePath),
            QMessageBox::Ok);
    }
}

void MainWindow::on_actionMostrar_log_triggered()
{
    const QString path = AppLogger::logFilePath();
    QMessageBox msg(this);
    msg.setWindowTitle("Log de depuración");
    msg.setText(QString("El archivo de log se encuentra en:\n%1\n\n"
                        "Envíe este archivo al soporte técnico cuando tenga un problema.").arg(path));
    msg.setStandardButtons(QMessageBox::Ok);
    QPushButton *openBtn = msg.addButton("Abrir archivo", QMessageBox::ActionRole);
    msg.exec();
    if (msg.clickedButton() == openBtn)
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
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
    dlg.setWindowTitle("Acerca de Verifactu");
    dlg.setMinimumWidth(600);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);

    QLabel *body = new QLabel(&dlg);
    body->setTextFormat(Qt::RichText);
    body->setWordWrap(true);
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

    QPushButton *btnClose = new QPushButton("Cerrar", &dlg);
    layout->addWidget(btnClose, 0, Qt::AlignRight);
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
}

// Reads the bundled releases_notes.txt (a Qt resource compiled into the exe via
// resources/laideal.qrc) and shows the full version history in a read-only
// monospace dialog. Same content the Inno Setup installer shows at install time
// and the GitHub release page reuses for the latest section.
void MainWindow::on_actionNotas_de_la_version_triggered()
{
    QFile f(":/docs/releases_notes.txt");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Notas de la versión"),
            tr("No se pudieron cargar las notas de la versión "
               "(recurso :/docs/releases_notes.txt no disponible)."));
        return;
    }
    const QString notes = QString::fromUtf8(f.readAll());
    f.close();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Notas de la versión"));
    dlg.resize(680, 560);

    auto *layout = new QVBoxLayout(&dlg);
    auto *view = new QTextEdit(&dlg);
    view->setReadOnly(true);
    view->setLineWrapMode(QTextEdit::WidgetWidth);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    view->setFont(mono);
    view->setPlainText(notes);
    // Land at the top so the newest release shows first.
    view->moveCursor(QTextCursor::Start);
    layout->addWidget(view);

    auto *btn = new QPushButton(tr("Cerrar"), &dlg);
    layout->addWidget(btn, 0, Qt::AlignRight);
    connect(btn, &QPushButton::clicked, &dlg, &QDialog::accept);

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
    QMessageBox::information(this, tr("Sin actualizaciones"),
        tr("Está usando la versión más reciente (%1).").arg(Updater::currentVersion()));
}

void MainWindow::onUpdaterCheckFailed(const QString &error)
{
    if (m_updater->isSilentCheck()) {
        qDebug() << "Updater: silent check failed -" << error;
        return;
    }
    QMessageBox::warning(this, tr("Comprobación fallida"),
        tr("No se pudo comprobar si hay actualizaciones:\n%1").arg(error));
}

void MainWindow::on_actionHacer_copia_de_seguridad_triggered()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto res = m_backupManager->performBackup();
    QApplication::restoreOverrideCursor();

    if (!res.success) {
        QMessageBox::warning(this, tr("Copia de seguridad"),
                             tr("No se pudo crear la copia:\n%1").arg(res.errorMessage));
        return;
    }
    QMessageBox::information(this, tr("Copia de seguridad"),
        tr("Copia creada correctamente:\n%1\n\nTamaño: %2 KB")
            .arg(res.backupPath)
            .arg(res.bytesWritten / 1024));
}
