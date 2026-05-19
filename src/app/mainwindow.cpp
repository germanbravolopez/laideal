#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "sql_lite.h"
#include <QDateTime>
#include "listado.h"
#include "recog_prendas.h"
#include "imprimir.h"
#include "contabilidad.h"
#include "facturas.h"
#include "add_garment.h"
#include "appsettings.h"
#include "settingsdialog.h"

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

    // Settings action — prepended to Archivo menu
    QAction *actionConfig = new QAction(tr("Configuración..."), this);
    connect(actionConfig, &QAction::triggered, this, [this]() {
        SettingsDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            // Apply icon change immediately (no restart needed)
            QString iconPath = AppSettings::instance()->iconPath();
            if (!iconPath.isEmpty()) {
                QIcon icon(iconPath);
                QApplication::setWindowIcon(icon);
                setWindowIcon(icon);
            }
            // Reload Verifactu with potentially new credentials
            initializeVerifactu();
        }
    });
    ui->menuArchivo->insertAction(ui->menuArchivo->actions().first(), actionConfig);
    ui->menuArchivo->insertSeparator(ui->menuArchivo->actions().at(1));

    // Taskbar
    ui->menuArchivo->setToolTipsVisible(true);
    ui->menuHerramientas->setToolTipsVisible(true);
    ui->menuImprimir_ticket->setToolTipsVisible(true);
    ui->menuVisualizar->setToolTipsVisible(true);
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
        QMessageBox::warning(this, "Advertencia",
            QString("No se pudo inicializar Verifactu:\n%1\n\n"
                    "Las facturas se guardarán localmente.\n"
                    "Configura Verifactu más tarde.")
            .arg(m_verifactuIntegration->getLastError()));
    }
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
    else
        QMessageBox::warning(nullptr, "Error en la casilla de cantidad",
                              "Cantidad de prendas está vacía.",
                              QMessageBox::Ok, QMessageBox::Ok);
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
        else

            QMessageBox::information(this, "Listado de clientes",
                                  "Cliente encontrado en la base de datos tras suprimir carácteres especiales como tildes o 'ñ'.\n"
                                  "Los datos introducidos para el cliente en este recibo no se han añadido al cliente en el listado de clientes. "
                                  "Si se desean actualizar los datos, añadir manualmente en el listado de clientes.",
                                  QMessageBox::Ok,
                                  QMessageBox::Ok);
    }

}

VerifactuResult MainWindow::verifactuSubmitInvoice()
{
    if (m_verifactuIntegration && m_verifactuIntegration->isConfigured()) {
        double ivaRate = AppSettings::instance()->ivaRate();
        VerifactuResult result = m_verifactuIntegration->submitSimplifiedInvoice(
            ui->le_nr_ticket->text(),
            ui->de_date_recep->date(),
            ui->le_cost_total->text().toDouble() / (1.0 + ivaRate / 100.0),
            ivaRate,
            "Servicios de lavanderia"
        );

        if (result.isSuccess()) {
            qDebug() << "Invoice submitted with CSV:" << result.csv;
            showQrToClient(result);
        } else {
            qDebug() << "Invoice submission failed:" << result.errorDescription;
        }
        return result;
    } else {
        qDebug() << "Verifactu not configured — skipping invoice submission";
        return VerifactuResult{};
    }
}

void MainWindow::showQrToClient(const VerifactuResult &result)
{
    QDialog *qrDialog = new QDialog(this);
    qrDialog->setWindowTitle("Código QR de Validación");
    qrDialog->setModal(true);
    qrDialog->setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout *layout = new QVBoxLayout();

    if (!result.qrCode.isNull()) {
        QLabel *labelQR = new QLabel();
        labelQR->setPixmap(result.qrCode.scaledToWidth(300, Qt::SmoothTransformation));
        layout->addWidget(labelQR, 0, Qt::AlignCenter);
    }

    QLabel *labelCSV = new QLabel(
        QString("<b>CSV Verifactu:</b> %1").arg(result.csv));
    layout->addWidget(labelCSV, 0, Qt::AlignCenter);

    if (!result.validationUrl.isEmpty()) {
        QLabel *labelUrl = new QLabel(
            QString("<a href='%1'>Validar en AEAT</a>")
            .arg(result.validationUrl));
        labelUrl->setOpenExternalLinks(true);
        layout->addWidget(labelUrl, 0, Qt::AlignCenter);
    }

    QPushButton *btnCerrar = new QPushButton("Cerrar");
    connect(btnCerrar, &QPushButton::clicked, qrDialog, &QDialog::accept);
    layout->addWidget(btnCerrar);

    qrDialog->setLayout(layout);
    qrDialog->exec();
}

void MainWindow::saveTicket(const VerifactuResult &verifactuResult)
{
    const QString timestamp = verifactuResult.isSuccess()
        ? QDateTime::currentDateTime().toString(Qt::ISODate) : "";

    for (int row = 0; row < ui->table_ticket->rowCount(); row++) {
        // If there is any content in price of that row then save
        if (ui->table_ticket->item(row, TABLE_TICKET_PRIC)) {
            QString hash = genHash16();
            db.open();
            QSqlQuery q;
            q.prepare("INSERT INTO ingresos "
                      "(n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, "
                      "cantidad, prenda, size, servicio, observaciones, edit_lock, hash, "
                      "verifactu_csv, verifactu_timestamp, verifactu_estado, verifactu_error, verifactu_url_qr) "
                      "VALUES "
                      "(:n_recibo, :cliente, :fecha_recepcion, :fecha_pago, :fecha_recogida, :importe, :pagado, :estado, "
                      ":cantidad, :prenda, :size, :servicio, :observaciones, :edit_lock, :hash, "
                      ":verifactu_csv, :verifactu_timestamp, :verifactu_estado, :verifactu_error, :verifactu_url_qr);");
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
            if (verifactuResult.isSuccess()) {
                q.bindValue(":verifactu_csv",       verifactuResult.csv);
                q.bindValue(":verifactu_timestamp", timestamp);
                q.bindValue(":verifactu_estado",    "ENVIADA");
                q.bindValue(":verifactu_error",     "");
                q.bindValue(":verifactu_url_qr",    verifactuResult.validationUrl);
            } else if (verifactuResult.status == VerifactuResult::ERROR
                       || verifactuResult.status == VerifactuResult::NETWORK_ERROR) {
                q.bindValue(":verifactu_csv",       "");
                q.bindValue(":verifactu_timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));
                q.bindValue(":verifactu_estado",    "ERROR");
                q.bindValue(":verifactu_error",     verifactuResult.errorDescription);
                q.bindValue(":verifactu_url_qr",    "");
            } else {
                q.bindValue(":verifactu_csv",       "");
                q.bindValue(":verifactu_timestamp", "");
                q.bindValue(":verifactu_estado",    "");
                q.bindValue(":verifactu_error",     "");
                q.bindValue(":verifactu_url_qr",    "");
            }
            q.exec();
            q.clear();
            db.close();
        }
    }
}

void MainWindow::printRecibo()
{
    Imprimir *ui_impr;
    ui_impr = new Imprimir(this);
    ui_impr->db = db;
    ui_impr->isRecibo = true;
    ui_impr->isCompleteInvoice = false;
    ui_impr->le_n_ticket->setText(ui->le_nr_ticket->text());
    ui_impr->getTicketInfo();
    ui_impr->createTicketExcel(true, ui->pb_payment->isChecked());
    ui_impr->printTicket();
    ui_impr->createTicketExcel(false, ui->pb_payment->isChecked());
    ui_impr->printTicket();
}

void MainWindow::printFra()
{
    Imprimir *ui_impr;
    ui_impr = new Imprimir(this);
    ui_impr->db = db;
    ui_impr->isRecibo = false;
    ui_impr->isCompleteInvoice = false;
    ui_impr->le_n_ticket->setText(ui->le_nr_ticket->text());
    ui_impr->getTicketInfo();
    ui_impr->createTicketExcel(false, false);
    ui_impr->printTicket();
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
            VerifactuResult verifactuResult = verifactuSubmitInvoice();
            saveTicket(verifactuResult);
            if (AppSettings::instance()->enablePrinting()) {
                printRecibo();
                if (ui->pb_payment->text() == "SI") {
                    printFra();
                }
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
            if (q.value(14).toString() == "") {
                QString nRecibo = q.value(0).toString();
                QString cliente = q.value(1).toString();
                QString fechaRecepcion = q.value(2).toString();
                QString fechaPago = q.value(3).toString();
                QString fechaRecogida = q.value(4).toString();
                QString importe = q.value(5).toString();
                QString pagado = q.value(6).toString();
                QString estado = q.value(7).toString();
                QString cantidad = q.value(8).toString();
                QString prenda = q.value(9).toString();
                QString size = q.value(10).toString();
                QString servicio = q.value(11).toString();
                QString observaciones = q.value(12).toString();
                QString editLock = q.value(13).toString();

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

    if (duplicatedTickets.isEmpty())
        QMessageBox::information(this, "Crear hash en ingresos",
                                 "Se han actualizado correctamente " + QString::number(cnt) + " filas de la tabla ingresos.",
                                 QMessageBox::Ok, QMessageBox::Ok);
    else
        QMessageBox::information(this, "Crear hash en ingresos",
                                 "Se han actualizado correctamente " + QString::number(cnt) + " filas de la tabla ingresos.\n\n"
                                 "Los siguientes recibos tienen hashes duplicados: " + duplicatedTicketsS.join(", "),
                                 QMessageBox::Ok, QMessageBox::Ok);
}
