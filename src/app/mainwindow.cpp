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
#include "version.h"
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
            mgr.getConfig()->setEmitterData(nif, name, name);
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
        float price = qntyItem->text().toFloat() * readGarmentPrice(db, garmentText, serviceText);
        if (price < 0) {
            price = 0.0;
        } else {
            // Check if any size is filled
            QTableWidgetItem *sizeItem(ui->table_ticket->item(garmentRow, TABLE_TICKET_SIZE));
            if (sizeItem && sizeItem->text() != "" && sizeItem->text().toFloat() != 0.0) {
                float size = sizeItem->text().toFloat() * price;
                item->setText(QString::number(size, 'f', 2));
            }
            else
                item->setText(QString::number(price, 'f', 2));
        }
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

void MainWindow::verifactuSubmitInvoice(const QString &ticketNum, const QDate &invoiceDate, double totalAmount)
{
    if (!m_verifactuIntegration || !m_verifactuIntegration->isConfigured()) {
        qDebug() << "Verifactu not configured - skipping invoice submission for ticket" << ticketNum;
        return;
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
        return;
    }
    m_pendingSubmits.insert(reqId, ticketNum);
    statusBar()->showMessage(tr("Enviando ticket %1 a AEAT...").arg(ticketNum));
}

void MainWindow::onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result)
{
    auto it = m_pendingSubmits.find(requestId);
    if (it == m_pendingSubmits.end()) return; // not one of ours (cancel/QR/etc. or other consumer)
    const QString ticketNum = it.value();
    m_pendingSubmits.erase(it);

    updateTicketVerifactuFields(ticketNum, result);

    if (result.isSuccess()) {
        statusBar()->showMessage(tr("Ticket %1 enviado a AEAT (CSV: %2)").arg(ticketNum, result.csv), 10000);
    } else {
        statusBar()->showMessage(
            tr("Error al enviar ticket %1: %2").arg(ticketNum, result.errorDescription), 15000);
        qWarning() << "Verifactu submission failed for ticket" << ticketNum << "-" << result.errorDescription;
    }
}

void MainWindow::updateTicketVerifactuFields(const QString &ticketNum, const VerifactuResult &result)
{
    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    const QString estado    = verifactuEstadoToString(
        result.isSuccess() ? VerifactuEstado::Enviada : VerifactuEstado::Error);
    qDebug() << "MainWindow::updateTicketVerifactuFields: ticket" << ticketNum
             << "estado=" << estado
             << "csv=" << (result.isSuccess() ? result.csv : QString())
             << "hash=" << (result.isSuccess() ? result.rawHash : QString())
             << "xml_len=" << (result.isSuccess() ? result.rawXml.size() : 0)
             << "error=" << (result.isSuccess() ? QString() : result.errorDescription);
    db.open();
    QSqlQuery q;
    q.prepare("UPDATE ingresos SET verifactu_csv = :csv, verifactu_timestamp = :ts, "
              "verifactu_estado = :estado, verifactu_error = :error, verifactu_url_qr = :url, "
              "verifactu_xml = :xml, verifactu_hash = :hash "
              "WHERE n_recibo = :n_recibo");
    if (result.isSuccess()) {
        q.bindValue(":csv",    result.csv);
        q.bindValue(":ts",     timestamp);
        q.bindValue(":estado", estado);
        q.bindValue(":error",  "");
        q.bindValue(":url",    result.validationUrl);
        q.bindValue(":xml",    result.rawXml);
        q.bindValue(":hash",   result.rawHash);
    } else {
        q.bindValue(":csv",    "");
        q.bindValue(":ts",     timestamp);
        q.bindValue(":estado", estado);
        q.bindValue(":error",  result.errorDescription);
        q.bindValue(":url",    "");
        q.bindValue(":xml",    "");
        q.bindValue(":hash",   "");
    }
    q.bindValue(":n_recibo", ticketNum);
    if (!q.exec())
        qWarning() << "updateTicketVerifactuFields UPDATE failed for ticket" << ticketNum
                   << "-" << q.lastError().text();
    db.close();
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
    ui_impr = new Imprimir(this);
    ui_impr->db = db;
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

void MainWindow::printFra()
{
    Imprimir *ui_impr;
    ui_impr = new Imprimir(this);
    ui_impr->db = db;
    ui_impr->isRecibo = false;
    ui_impr->isCompleteInvoice = false;
    ui_impr->verifactuIntegration = nullptr;
    ui_impr->le_n_ticket->setText(ui->le_nr_ticket->text());
    ui_impr->getTicketInfo();
    ui_impr->createTicketExcel(false, false);
    if (AppSettings::instance()->enablePrinting()) {
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
                verifactuSubmitInvoice(ticketNum, invoiceDate, totalAmount);
                printFra();
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
    ui_listado = new Listado(this);
    ui_listado->tableName = "ingresos";
    ui_listado->db = db;
    ui_listado->setObjectName(title);
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
    ui_listado = new Listado(this);
    ui_listado->tableName = "gastos";
    ui_listado->db = db;
    ui_listado->setObjectName(title);
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
    ui_listado = new Listado(this);
    ui_listado->tableName = "prendas";
    ui_listado->db = db;
    ui_listado->setObjectName(title);
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
    ui_listado = new Listado(this);
    ui_listado->tableName = "clientes";
    ui_listado->db = db;
    ui_listado->setObjectName(title);
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
    ui_listado = new Listado(this);
    ui_listado->tableName = "proveedores";
    ui_listado->db = db;
    ui_listado->setObjectName(title);
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
    ui_listado = new Listado(this);
    ui_listado->tableName = "servicios";
    ui_listado->db = db;
    ui_listado->setObjectName(title);
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
    ui_recog = new RecogPrendas(this);
    ui_recog->db = db;
    ui_recog->m_verifactuIntegration = m_verifactuIntegration;
    //ui_recog->setWindowState(Qt::WindowMaximized);
    ui_recog->show();
}

void MainWindow::on_actionRecibo_triggered()
{
    Imprimir *ui_impr;
    ui_impr = new Imprimir(this);
    ui_impr->db = db;
    ui_impr->isRecibo = true;
    ui_impr->isCompleteInvoice = false;
    ui_impr->verifactuIntegration = m_verifactuIntegration;
    ui_impr->setWindowTitle("Imprimir recibo");
    ui_impr->show();
}

void MainWindow::on_actionFactura_triggered()
{
    Imprimir *ui_impr;
    ui_impr = new Imprimir(this);
    ui_impr->db = db;
    ui_impr->isRecibo = false;
    ui_impr->isCompleteInvoice = false;
    ui_impr->verifactuIntegration = m_verifactuIntegration;
    ui_impr->setWindowTitle("Imprimir factura");
    ui_impr->show();
}

void MainWindow::on_actionFactura_completa_triggered()
{
    Imprimir *ui_impr;
    ui_impr = new Imprimir(this);
    ui_impr->db = db;
    ui_impr->isRecibo = false;
    ui_impr->isCompleteInvoice = true;
    ui_impr->verifactuIntegration = m_verifactuIntegration;
    ui_impr->setWindowTitle("Imprimir factura completa");
    ui_impr->show();
}

void MainWindow::on_actionGenerar_contabilidad_triggered()
{
    Contabilidad *ui_contabilidad;
    ui_contabilidad = new Contabilidad(this);
    ui_contabilidad->db = db;
    ui_contabilidad->show();
}

void MainWindow::on_actionRevertir_contabilidad_triggered()
{
    Contabilidad *ui_rev_cont;
    ui_rev_cont = new Contabilidad(this);
    ui_rev_cont->db = db;
    ui_rev_cont->setWindowTitle("Revertir Contabilidad");
    ui_rev_cont->revertirOn = true;
    ui_rev_cont->resetAllContents();
    ui_rev_cont->show();
}

void MainWindow::on_actionFormulario_facturas_triggered()
{
    Facturas *ui_facturas;
    ui_facturas = new Facturas(this);
    ui_facturas->db = db;
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
    ui_add_garment = new AddGarment(this);
    ui_add_garment->db = db;
    ui_add_garment->show();
}

void MainWindow::on_actionCrear_hash_en_ingresos_triggered()
{
    // Change the cursor to a loading icon
    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Perform time consuming process
    int cnt = 0;
    db.open();
    QSqlQuery q;
    if (q.exec("SELECT * FROM ingresos")) {
        while (q.next()) {
            if (q.value(INGRESOS_IDX_HASH).toString() == "") {
                QString nRecibo = q.value(INGRESOS_IDX_ID).toString();
                QString cliente = q.value(INGRESOS_IDX_CLIENT).toString();
                QString fechaRecepcion = q.value(INGRESOS_IDX_DATE_RCP).toString();
                QString fechaPago = q.value(INGRESOS_IDX_DATE_PAY).toString();
                QString fechaRecogida = q.value(INGRESOS_IDX_DATE_PKU).toString();
                QString importe = q.value(INGRESOS_IDX_IMPORTE).toString();
                QString pagado = q.value(INGRESOS_IDX_PAYED).toString();
                QString estado = q.value(INGRESOS_IDX_STATE).toString();
                QString cantidad = q.value(INGRESOS_IDX_CANTIDAD).toString();
                QString prenda = q.value(INGRESOS_IDX_PRENDA).toString();
                QString size = q.value(INGRESOS_IDX_SIZE).toString();
                QString servicio = q.value(INGRESOS_IDX_SERVICIO).toString();
                QString observaciones = q.value(INGRESOS_IDX_OBSV).toString();
                QString editLock = q.value(INGRESOS_IDX_EDIT_LOCK).toString();

                QSqlQuery q1;
                QString newHash = genHash16();

                q1.prepare("UPDATE ingresos SET hash = :newHash WHERE n_recibo = :n_recibo AND "
                          "cliente = :cliente AND fecha_recepcion = :fecha_recepcion AND fecha_pago = :fecha_pago AND "
                          "fecha_recogida = :fecha_recogida AND importe = :importe AND pagado = :pagado AND estado = :estado AND "
                          "cantidad = :cantidad AND prenda = :prenda AND size = :size AND servicio = :servicio AND "
                          "observaciones = :observaciones AND edit_lock = :edit_lock");
                q1.bindValue(":newHash", newHash);
                q1.bindValue(":n_recibo", nRecibo);
                q1.bindValue(":cliente", cliente);
                q1.bindValue(":fecha_recepcion", fechaRecepcion);
                q1.bindValue(":fecha_pago", fechaPago);
                q1.bindValue(":fecha_recogida", fechaRecogida);
                q1.bindValue(":importe", importe);
                q1.bindValue(":pagado", pagado);
                q1.bindValue(":estado", estado);
                q1.bindValue(":cantidad", cantidad);
                q1.bindValue(":prenda", prenda);
                q1.bindValue(":size", size);
                q1.bindValue(":servicio", servicio);
                q1.bindValue(":observaciones", observaciones);
                q1.bindValue(":edit_lock", editLock);
                q1.exec();

                cnt++;
            }
        }
    }

    // Check how many tickets contains duplicated hashes
    QList<int> duplicatedTickets;
    if (q.exec("SELECT n_recibo, COUNT(*) FROM ingresos GROUP BY hash HAVING COUNT(*) > 1")) {
        while (q.next()) {
            duplicatedTickets.append(q.value(0).toInt());
        }
    }
    std::sort(duplicatedTickets.begin(), duplicatedTickets.end());
    QStringList duplicatedTicketsS;
    for (const int& num : duplicatedTickets) {
        duplicatedTicketsS.append(QString::number(num));
    }

    // Restore the cursor to default
    QApplication::restoreOverrideCursor();

    if (duplicatedTickets.isEmpty()) {
        qDebug() << "No duplicated hashes found in ingresos after update.";
        QMessageBox::information(this, "Crear hash en ingresos",
                                 "Se han actualizado correctamente " + QString::number(cnt) + " filas de la tabla ingresos.",
                                 QMessageBox::Ok, QMessageBox::Ok);
    } else {
        qWarning() << "Duplicated hashes found in ingresos after update for tickets:" << duplicatedTicketsS;
        QMessageBox::warning(this, "Crear hash en ingresos",
                                 "Se han actualizado correctamente " + QString::number(cnt) + " filas de la tabla ingresos.\n\n"
                                 "Los siguientes recibos tienen hashes duplicados: " + duplicatedTicketsS.join(", "),
                                 QMessageBox::Ok, QMessageBox::Ok);
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
    CancelInvoiceDialog dlg(this);
    dlg.db = db;
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
    RectifyInvoiceDialog dlg(this);
    dlg.db = db;
    dlg.m_verifactu = m_verifactuIntegration;
    dlg.exec();
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
                "en el momento de su generación.</p>")
            .arg(name.toHtmlEscaped(), nif.toHtmlEscaped(),
                 address.toHtmlEscaped(), city.toHtmlEscaped(),
                 software, version));
    layout->addWidget(body);

    QPushButton *btnClose = new QPushButton("Cerrar", &dlg);
    layout->addWidget(btnClose, 0, Qt::AlignRight);
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
}
