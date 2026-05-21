#include "recog_prendas.h"
#include "ui_recog_prendas.h"
#include "sql_lite.h"
#include "imprimir.h"
#include "appsettings.h"
#include "textcolordelegate.h"
#include "numberformatdelegate.h"
#include <QDateTime>
#include <QDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QSqlQuery>

RecogPrendas::RecogPrendas(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::RecogPrendas)
{
    ui->setupUi(this);
    initialSettings();
}

RecogPrendas::~RecogPrendas()
{
    delete ui;
}

void RecogPrendas::initialSettings()
{
    resetAllContents();
    ui->pb_payment->setStyleSheet("background-color: red; font-size: 18px");
    ui->pb_state->setStyleSheet("background-color: red; font-size: 18px");
    ui->le_search->setFocus();
    ui->tableView->verticalHeader()->setVisible(false);
}

void RecogPrendas::resetAllContents()
{
    isCellClicked = false;
    ui->le_nr_ticket->clear();
    ui->le_phone->clear();
    ui->le_client->clear();
    ui->le_garm->clear();
    ui->le_qty->clear();
    ui->le_servic->clear();
    ui->le_size->clear();
    ui->le_price->clear();
    ui->le_obsv->clear();
    ui->le_total_price->clear();
    ui->pb_payment->setChecked(false);
    ui->pb_state->setChecked(false);
    ui->pb_payment->setEnabled(false);
    ui->pb_state->setEnabled(false);
    ui->pb_pay_all->setEnabled(false);
    ui->pb_pku_all->setEnabled(false);
    ui->pb_separ_garm->setEnabled(false);
    ui->pb_print->setEnabled(false);
    ui->pb_verifactu->setEnabled(false);
    ui->de_date_recep->setDate(QDate::currentDate());
    ui->de_date_paym->setDate(QDate::currentDate());
    ui->de_date_pickup->setDate(QDate::currentDate());
}

void RecogPrendas::updateDb(UpdateDBop op, int nGarm)
{
    bool editLock = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_EDIT_LOCK)).toBool();
    QSqlQuery q;
    switch (op) {
    case PAY_YES:
        // If editLock payment info cannot be changed
        if (!editLock) {
            // dont update payment date for blocked quarters
            if (readLockForMonthAndYear(db, "ingresos", ui->de_date_paym->date().month(), ui->de_date_paym->date().year()) == 1)
                QMessageBox::warning(this, tr("Trimestre bloqueado"),
                                     tr("La fecha de pago pertenece a un trimestre que se encuentra bloqueado por la contabilidad."),
                                     QMessageBox::Ok, QMessageBox::Ok);
            else {
                db.open();
                q.prepare("UPDATE ingresos SET fecha_pago = :new_fecha_pago, pagado = :new_pagado WHERE "
                          "n_recibo = :n_re AND hash = :hash");
                // Set new values
                q.bindValue(":new_fecha_pago", ui->de_date_paym->date().toString("dd-MM-yyyy"));
                q.bindValue(":new_pagado",     ui->pb_payment->text());
                // Set id values
                q.bindValue(":n_re", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_TICKET)).toString());
                q.bindValue(":hash", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_HASH)).toString());
                // Write to db
                q.exec();
                q.clear();
                db.close();
            }
        }
        else
            QMessageBox::warning(this, tr("Ticket bloqueado"),
                                 tr("El ticket actual se encuentra bloqueado por la contabilidad."),
                                 QMessageBox::Ok, QMessageBox::Ok);
        break;
    case PAY_NO:
        // If editLock payment info cannot be changed
        if (!editLock) {
            db.open();
            QSqlQuery q;
            q.prepare("UPDATE ingresos SET fecha_pago = :new_fecha_pago, pagado = :new_pagado WHERE "
                      "n_recibo = :n_re AND hash = :hash");
            // Set new values
            q.bindValue(":new_fecha_pago", "");
            q.bindValue(":new_pagado",     ui->pb_payment->text());
            // Set id values
            q.bindValue(":n_re", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_TICKET)).toString());
            q.bindValue(":hash", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_HASH)).toString());
            // Write to db
            q.exec();
            q.clear();
            db.close();
        }
        else
            QMessageBox::warning(this, tr("Ticket bloqueado"),
                                 tr("El ticket actual se encuentra bloqueado por la contabilidad."),
                                 QMessageBox::Ok, QMessageBox::Ok);
        break;
    case PKU_YES:
        db.open();
        q.prepare("UPDATE ingresos SET fecha_recogida = :new_fecha_recogida, estado = :new_estado WHERE "
                  "n_recibo = :n_re AND hash = :hash");
        // Set new values
        q.bindValue(":new_fecha_recogida", ui->de_date_pickup->date().toString("dd-MM-yyyy"));
        q.bindValue(":new_estado",         ui->pb_state->text());
        // Set id values
        q.bindValue(":n_re", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_TICKET)).toString());
        q.bindValue(":hash", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_HASH)).toString());
        // Write to db
        q.exec();
        q.clear();
        db.close();
        break;
    case PKU_NO:
        db.open();
        q.prepare("UPDATE ingresos SET fecha_recogida = :new_fecha_recogida, estado = :new_estado WHERE "
                  "n_recibo = :n_re AND hash = :hash");
        // Set new values
        q.bindValue(":new_fecha_recogida", "");
        q.bindValue(":new_estado",         ui->pb_state->text());
        // Set id values
        q.bindValue(":n_re", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_TICKET)).toString());
        q.bindValue(":hash", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_HASH)).toString());
        // Write to db
        q.exec();
        q.clear();
        db.close();
        break;
    case OBSV:
        db.open();
        q.prepare("UPDATE ingresos SET observaciones = :new_observaciones WHERE "
                  "n_recibo = :n_re AND hash = :hash");
        // Set new values
        q.bindValue(":new_observaciones", ui->le_obsv->text());
        // Set id values
        q.bindValue(":n_re", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_TICKET)).toString());
        q.bindValue(":hash", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_HASH)).toString());
        // Write to db
        q.exec();
        q.clear();
        db.close();
        break;
    case SIZE_AND_PRICE:
        if (!editLock && ui->pb_payment->text() == "NO") {
            db.open();
            q.prepare("UPDATE ingresos SET size = :new_size, importe = :new_importe WHERE "
                      "n_recibo = :n_re AND hash = :hash");
            // Set new values
            q.bindValue(":new_size", ui->le_size->text());
            q.bindValue(":new_importe", ui->le_price->text());
            // Set id values
            q.bindValue(":n_re", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_TICKET)).toString());
            q.bindValue(":hash", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_HASH)).toString());
            // Write to db
            q.exec();
            q.clear();
            db.close();
        }
        break;
    case SEPARATE_GARM:
        // If editLock payment info cannot be changed
        if (!editLock) {
            // Update current garments
            int newQtyUpd = ui->le_qty->text().toInt() - nGarm;
            float newImpUpd = QString::number(newQtyUpd).toFloat() * readGarmentPrice(db, ui->le_garm->text(), ui->le_servic->text());
            if (newImpUpd < 0) {
                break;
            }
            db.open();
            q.prepare("UPDATE ingresos SET cantidad = :new_cant, importe = :new_impor WHERE "
                      "n_recibo = :n_re AND hash = :hash");
            // Set new values
            q.bindValue(":new_cant",  QString::number(newQtyUpd));
            q.bindValue(":new_impor", QString::number(newImpUpd, 'f', 2).replace(",","."));
            // Set id values
            q.bindValue(":n_re", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_TICKET)).toString());
            q.bindValue(":hash", sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_HASH)).toString());
            // Write to db
            q.exec();
            q.clear();
            db.close();

            // Insert separated garments
            float newImpIns = nGarm * readGarmentPrice(db, ui->le_garm->text(), ui->le_servic->text());
            QString hash = genHash16();
            db.open();
            q.prepare("INSERT INTO ingresos (n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, observaciones, edit_lock, hash) "
                      "VALUES (:n_recibo, :cliente, :fecha_recepcion, :fecha_pago, :fecha_recogida, :importe, :pagado, :estado, :cantidad, :prenda, :size, :servicio, :observaciones, :edit_lock, :hash);");
            q.bindValue(":n_recibo", ui->le_nr_ticket->text());
            q.bindValue(":cliente", ui->le_client->text());
            q.bindValue(":fecha_recepcion", ui->de_date_recep->date().toString("dd-MM-yyyy"));
            q.bindValue(":pagado", ui->pb_payment->text());
            if (ui->pb_payment->isChecked())
                q.bindValue(":fecha_pago", ui->de_date_paym->date().toString("dd-MM-yyyy"));
            else
                q.bindValue(":fecha_pago", "");
            q.bindValue(":estado", ui->pb_state->text());
            if (ui->pb_state->isChecked())
                q.bindValue(":fecha_recogida", ui->de_date_pickup->date().toString("dd-MM-yyyy"));
            else
                q.bindValue(":fecha_recogida", "");
            q.bindValue(":importe", QString::number(newImpIns, 'f', 2).replace(",","."));
            q.bindValue(":cantidad", QString::number(nGarm));
            q.bindValue(":prenda", ui->le_garm->text());
            q.bindValue(":size", ui->le_size->text().replace(",","."));
            q.bindValue(":observaciones", ui->le_obsv->text());
            q.bindValue(":servicio", ui->le_servic->text());
            q.bindValue(":edit_lock", "0");
            q.bindValue(":hash", hash);
            q.exec();
            q.clear();
            db.close();
        }
        else
            QMessageBox::warning(this, tr("Ticket bloqueado"),
                                 tr("El ticket actual se encuentra bloqueado por la contabilidad."),
                                 QMessageBox::Ok, QMessageBox::Ok);
        break;
    default:
        break;
    }
    // Search again
    on_pb_search_clicked();
    // Load again data from table
    updateRowClickedToFields();
    isCellClicked = true;
}

void RecogPrendas::updateRowClickedToFields()
{
    // Update content from clicked row
    ui->le_nr_ticket->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_TICKET)).toString());
    ui->le_client->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_CLIENT)).toString());
    ui->le_phone->setText(selectFromWhereLike(db, "movil", "clientes", "nombre", ui->le_client->text(), true, false));
    ui->le_garm->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_GARMENT)).toString());
    ui->le_qty->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_QUANTITY)).toString());
    ui->le_servic->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_SERVICE)).toString());
    ui->le_size->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_SIZE)).toString());
    ui->le_price->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_PRICE)).toString());
    ui->le_obsv->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_OBSERV)).toString());
    ui->pb_payment->setChecked(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_IS_PAYED)).toString() == "SI");
    ui->pb_state->setChecked(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_STATE)).toString() == "Recogido");
    ui->de_date_recep->setDate(QDate::fromString(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_DATE_RCP)).toString(),"dd-MM-yyyy"));
    ui->de_date_paym->setDate(QDate::fromString(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_DATE_PAY)).toString(),"dd-MM-yyyy"));
    ui->de_date_pickup->setDate(QDate::fromString(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_DATE_PKU)).toString(),"dd-MM-yyyy"));
    ui->pb_payment->setEnabled(true);
    ui->pb_state->setEnabled(true);
    ui->pb_pay_all->setEnabled(true);
    ui->pb_pku_all->setEnabled(true);
    ui->pb_separ_garm->setEnabled(true);
    ui->pb_print->setEnabled(true);
    QString verifactuEstado = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_VERIFACTU_ESTADO)).toString();
    ui->pb_verifactu->setEnabled(!verifactuEstado.isEmpty());
}

float RecogPrendas::calculatePrice()
{
    float itemPrice = readGarmentPrice(db, ui->le_garm->text(), ui->le_servic->text());
    if (ui->le_size->text().contains(",")) {
        QStringList sizeSplitted = ui->le_size->text().split(",");
        ui->le_size->setText(sizeSplitted.first() + "." + sizeSplitted.last());
    }
    float calculatedPrice = itemPrice * ui->le_qty->text().toFloat() * ui->le_size->text().toFloat();
    return calculatedPrice;
}

void RecogPrendas::on_le_search_returnPressed()
{
    on_pb_search_clicked();
}

void RecogPrendas::on_cb_search_date_currentTextChanged(const QString &arg1)
{
    on_pb_search_clicked();
}

void RecogPrendas::on_pb_search_clicked()
{
    resetAllContents();
    if (ui->le_search->text() != "") {
        bool ok = false, totalPriceActive = true;
        QString nameSearchFilter;
        ui->le_search->text().toUInt(&ok);
        if (ok) {
            if (ui->le_search->text().length() >= 9) {
                // Phone number — search tel_fijo OR movil
                db.open();
                QSqlQuery phoneQ(db);
                phoneQ.prepare("SELECT nombre FROM clientes WHERE tel_fijo LIKE :phone OR movil LIKE :movil");
                phoneQ.bindValue(":phone", ui->le_search->text());
                phoneQ.bindValue(":movil", ui->le_search->text());
                phoneQ.exec();
                QString clientFromPhone;
                if (phoneQ.first())
                    clientFromPhone = phoneQ.value(0).toString();
                else
                    QMessageBox::warning(this, tr("Búsqueda vacía"),
                                          tr("No se ha encontrado ningún cliente con el teléfono indicado."),
                                          QMessageBox::Ok, QMessageBox::Ok);
                db.close();

                if (!clientFromPhone.isNull()) {
                    db.open();
                    QSqlQuery q(db);
                    q.prepare("SELECT * FROM ingresos WHERE cliente = :cliente");
                    q.bindValue(":cliente", clientFromPhone);
                    q.exec();
                    sqlQueryModel->setQuery(std::move(q));
                    db.close();
                }
            }
            else {
                // Ticket number
                db.open();
                QSqlQuery q(db);
                q.prepare("SELECT * FROM ingresos WHERE n_recibo = :n_recibo");
                q.bindValue(":n_recibo", ui->le_search->text());
                q.exec();
                sqlQueryModel->setQuery(std::move(q));
                db.close();
                totalPriceActive = true;
            }
        }
        else if (ui->le_search->text().isSimpleText()) {
            QDate dateSlash = QDate::fromString(ui->le_search->text(), "dd/MM/yyyy");
            QDate dateDash = QDate::fromString(ui->le_search->text(), "dd-MM-yyyy");
            QDate date = (!dateSlash.isNull()) ? dateSlash :
                         (!dateDash.isNull()) ? dateDash: QDate::currentDate();
            // date_type values come from a hard-coded ComboBox — not user input
            QString dateType = (ui->cb_search_date->currentText() == "Recepción") ? "fecha_recepcion" :
                               (ui->cb_search_date->currentText() == "Pago") ? "fecha_pago" :
                               (ui->cb_search_date->currentText() == "Recogida") ? "fecha_recogida" : "";
            if (!dateSlash.isNull() || !dateDash.isNull()) {
                db.open();
                QSqlQuery q(db);
                q.prepare("SELECT * FROM ingresos WHERE " + dateType + " = :date");
                q.bindValue(":date", date.toString("dd-MM-yyyy"));
                q.exec();
                sqlQueryModel->setQuery(std::move(q));
                db.close();
            }
            else {
                // SQLite LIKE is ASCII-only and won't match García when searching "garcia".
                // Load all rows and filter client-side so normalization handles diacritics.
                db.open();
                QSqlQuery q(db);
                q.prepare("SELECT * FROM ingresos");
                q.exec();
                sqlQueryModel->setQuery(std::move(q));
                db.close();
                nameSearchFilter = MySortFilterProxyModel::removeDiacritics(ui->le_search->text()).toLower();
            }
        }
        else
            QMessageBox::warning(this, tr("Búsqueda incorrecta"),
                                  tr("El contenido de la búsqueda no se ha identificado.\n"
                                  "Hablar con Germán..."),
                                  QMessageBox::Ok, QMessageBox::Ok);
        // Complete model and set to the view
        sqlQueryModel->setHeaderData(TABLE_TICKET   , Qt::Horizontal, tr("Nº"));
        sqlQueryModel->setHeaderData(TABLE_CLIENT   , Qt::Horizontal, tr("Cliente"));
        sqlQueryModel->setHeaderData(TABLE_DATE_RCP , Qt::Horizontal, tr("Recepción"));
        sqlQueryModel->setHeaderData(TABLE_DATE_PAY , Qt::Horizontal, tr("Pago"));
        sqlQueryModel->setHeaderData(TABLE_DATE_PKU , Qt::Horizontal, tr("Recogida"));
        sqlQueryModel->setHeaderData(TABLE_PRICE    , Qt::Horizontal, tr("Importe"));
        sqlQueryModel->setHeaderData(TABLE_IS_PAYED , Qt::Horizontal, tr("Pagado"));
        sqlQueryModel->setHeaderData(TABLE_STATE    , Qt::Horizontal, tr("Estado"));
        sqlQueryModel->setHeaderData(TABLE_SIZE     , Qt::Horizontal, tr("Cant."));
        sqlQueryModel->setHeaderData(TABLE_GARMENT  , Qt::Horizontal, tr("Prenda"));
        sqlQueryModel->setHeaderData(TABLE_SIZE     , Qt::Horizontal, tr("Tam."));
        sqlQueryModel->setHeaderData(TABLE_SERVICE  , Qt::Horizontal, tr("Serv."));
        sqlQueryModel->setHeaderData(TABLE_OBSERV   , Qt::Horizontal, tr("Obs."));
        sqlQueryModel->setHeaderData(TABLE_EDIT_LOCK, Qt::Horizontal, tr("Bloqueo"));
        // Set model to table
        proxyModel = new MySortFilterProxyModel(this);
        if (!nameSearchFilter.isEmpty())
            proxyModel->setNormalizedFilter(nameSearchFilter, TABLE_CLIENT);
        // Fetch all source rows before handing to proxy (needed for name search with full SELECT)
        while (sqlQueryModel->canFetchMore())
            sqlQueryModel->fetchMore();
        proxyModel->setSourceModel(sqlQueryModel);
        ui->tableView->setModel(proxyModel);
        ui->tableView->sortByColumn(0, Qt::DescendingOrder);
        // Hide internal columns not meant for display
        ui->tableView->setColumnHidden(TABLE_HASH,                true);
        ui->tableView->setColumnHidden(TABLE_VERIFACTU_CSV,       true);
        ui->tableView->setColumnHidden(TABLE_VERIFACTU_TIMESTAMP, true);
        ui->tableView->setColumnHidden(TABLE_VERIFACTU_ESTADO,    true);
        ui->tableView->setColumnHidden(TABLE_VERIFACTU_ERROR,     true);
        ui->tableView->setColumnHidden(TABLE_VERIFACTU_URL_QR,    true);
        ui->tableView->setItemDelegateForColumn(TABLE_PRICE, new NumberFormatDelegate(this));
        ui->tableView->setItemDelegateForColumn(TABLE_IS_PAYED, new TextColorDelegate(ui->tableView, this));
        ui->tableView->setItemDelegateForColumn(TABLE_STATE, new TextColorDelegate(ui->tableView, this));
        ui->tableView->resizeColumnsToContents();
        ui->tableView->resizeRowsToContents();
        // Fill total_price from proxy rows (reflects the filtered set in all search modes)
        if (totalPriceActive) {
            float totalPrice = 0.0;
            for (int row = 0; row < proxyModel->rowCount(); row++)
                totalPrice += proxyModel->data(proxyModel->index(row, TABLE_PRICE)).toFloat();
            ui->le_total_price->setText(QString::number(totalPrice, 'f', 2));
        }
        else
            ui->le_total_price->setText("");
    }
    else {
        sqlQueryModel->clear();
        ui->tableView->setModel(sqlQueryModel);
    }
}

void RecogPrendas::on_pb_reset_clicked()
{
    resetAllContents();
    ui->le_search->setText("");
    on_pb_search_clicked();
}

void RecogPrendas::on_pb_payment_toggled(bool checked)
{
    if (checked) {
        ui->pb_payment->setText("SI");
        ui->pb_payment->setStyleSheet("background-color: green; font-size: 18px");
        if (isCellClicked)
            updateDb(PAY_YES);
    }
    else {
        ui->pb_payment->setText("NO");
        ui->pb_payment->setStyleSheet("background-color: red; font-size: 18px");
        if (isCellClicked)
            updateDb(PAY_NO);
    }
}

void RecogPrendas::on_pb_state_toggled(bool checked)
{
    if (checked) {
        ui->pb_state->setText("Recogido");
        ui->pb_state->setStyleSheet("background-color: green; font-size: 18px");
        if (isCellClicked)
            updateDb(PKU_YES);
    }
    else {
        ui->pb_state->setText("En tienda");
        ui->pb_state->setStyleSheet("background-color: red; font-size: 18px");
        if (isCellClicked)
            updateDb(PKU_NO);
    }
}

void RecogPrendas::on_tableView_clicked(const QModelIndex &index)
{
    // Check if another row is clicked
    if (index.row() != rowClickedCell)
        isCellClicked = false;
    // Update pointers to cell clicked
    rowClickedCell = index.row();
    columnClickedCell = index.column();
    updateRowClickedToFields();
    // Enable action buttons now that a row is selected
    ui->pb_payment->setEnabled(true);
    ui->pb_state->setEnabled(true);
    ui->pb_pay_all->setEnabled(true);
    ui->pb_pku_all->setEnabled(true);
    ui->pb_separ_garm->setEnabled(true);
    ui->pb_print->setEnabled(true);
    // Set clicked cell
    isCellClicked = true;
}

void RecogPrendas::on_le_obsv_editingFinished()
{
    if (isCellClicked)
        updateDb(OBSV);
}

void RecogPrendas::on_le_size_editingFinished()
{
    if (isCellClicked && ui->le_garm->text().contains("m2")) {
        float price = calculatePrice();
        if (price > 0) {
            ui->le_price->setText(QString::number(price));
            updateDb(SIZE_AND_PRICE);
        }
    }
}

void RecogPrendas::on_pb_pay_all_clicked()
{
    int currentRow = rowClickedCell;
    for (int row = 0; row < sqlQueryModel->rowCount(); row++) {
        on_tableView_clicked(sqlQueryModel->index(row, 0));
        on_pb_payment_toggled(true);
    }
    if (currentRow >= 0)
        on_tableView_clicked(sqlQueryModel->index(currentRow, 0));
    else
        resetAllContents();
}

void RecogPrendas::on_pb_pku_all_clicked()
{
    int currentRow = rowClickedCell;
    for (int row = 0; row < sqlQueryModel->rowCount(); row++) {
        on_tableView_clicked(sqlQueryModel->index(row, 0));
        on_pb_state_toggled(true);
    }
    if (currentRow >= 0)
        on_tableView_clicked(sqlQueryModel->index(currentRow, 0));
    else
        resetAllContents();
}

void RecogPrendas::on_pb_print_clicked()
{
    if (!ui->le_nr_ticket->text().isEmpty()) {
        Imprimir *ui_impr;
        ui_impr = new Imprimir(this);
        ui_impr->db = db;
        ui_impr->isRecibo = false;
        ui_impr->isCompleteInvoice = false;
        ui_impr->verifactuIntegration = m_verifactuIntegration;
        ui_impr->le_n_ticket->setText(ui->le_nr_ticket->text());
        ui_impr->getTicketInfo();
        ui_impr->createTicketExcel(false, false);
        ui_impr->printTicket();
        this->close();
    }
}

void RecogPrendas::on_pb_verifactu_clicked()
{
    if (!isCellClicked) return;

    QString ticketNum   = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_TICKET)).toString();
    QString estado      = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_VERIFACTU_ESTADO)).toString();
    QString csv         = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_VERIFACTU_CSV)).toString();
    QString timestamp   = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_VERIFACTU_TIMESTAMP)).toString();
    QString error       = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_VERIFACTU_ERROR)).toString();
    QString url         = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_VERIFACTU_URL_QR)).toString();
    QString dateStr     = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, TABLE_DATE_RCP)).toString();
    QDate invoiceDate   = QDate::fromString(dateStr, "dd-MM-yyyy");

    QDialog *dlg = new QDialog(this);
    dlg->setWindowTitle("Verifactu — Ticket " + ticketNum);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout *layout = new QVBoxLayout(dlg);

    auto addRow = [&](const QString &label, const QString &value) {
        QLabel *lbl = new QLabel(QString("<b>%1</b> %2").arg(label, value.isEmpty() ? "—" : value));
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(lbl);
    };

    addRow("Estado:", estado);
    addRow("CSV:", csv);
    addRow("Fecha envío:", timestamp);
    if (!error.isEmpty())
        addRow("Error:", error);
    if (!url.isEmpty()) {
        QLabel *urlLabel = new QLabel(
            QString("<b>Validación AEAT:</b> <a href='%1'>Abrir en AEAT</a>").arg(url));
        urlLabel->setOpenExternalLinks(true);
        urlLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
        layout->addWidget(urlLabel);
    }

    if (estado == "ERROR" && m_verifactuIntegration && m_verifactuIntegration->isConfigured()) {
        QPushButton *btnRetry = new QPushButton("Reintentar envío a AEAT", dlg);
        connect(btnRetry, &QPushButton::clicked, this, [this, dlg, ticketNum, invoiceDate]() {
            dlg->accept();
            retryVerifactuSubmit(ticketNum, invoiceDate);
        });
        layout->addWidget(btnRetry);
    }

    QPushButton *btnClose = new QPushButton("Cerrar", dlg);
    connect(btnClose, &QPushButton::clicked, dlg, &QDialog::accept);
    layout->addWidget(btnClose);

    dlg->setMinimumWidth(420);
    dlg->exec();
}

void RecogPrendas::retryVerifactuSubmit(const QString &ticketNum, const QDate &invoiceDate)
{
    double total = 0.0;
    db.open();
    {
        QSqlQuery q;
        q.prepare("SELECT SUM(CAST(importe AS REAL)) FROM ingresos WHERE n_recibo = :n_recibo");
        q.bindValue(":n_recibo", ticketNum);
        q.exec();
        if (q.next())
            total = q.value(0).toDouble();
    }
    db.close();

    double ivaRate = AppSettings::instance()->ivaRate();
    VerifactuResult result = m_verifactuIntegration->submitSimplifiedInvoice(
        ticketNum,
        invoiceDate,
        total / (1.0 + ivaRate / 100.0),
        ivaRate,
        "Servicios de lavanderia"
    );

    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    db.open();
    {
        QSqlQuery upd;
        if (result.isSuccess()) {
            upd.prepare("UPDATE ingresos SET "
                        "verifactu_csv = :csv, verifactu_timestamp = :ts, "
                        "verifactu_estado = :estado, verifactu_error = :error, verifactu_url_qr = :url "
                        "WHERE n_recibo = :n_recibo");
            upd.bindValue(":csv",    result.csv);
            upd.bindValue(":ts",     timestamp);
            upd.bindValue(":estado", "ENVIADA");
            upd.bindValue(":error",  "");
            upd.bindValue(":url",    result.validationUrl);
        } else {
            upd.prepare("UPDATE ingresos SET "
                        "verifactu_csv = :csv, verifactu_timestamp = :ts, "
                        "verifactu_estado = :estado, verifactu_error = :error, verifactu_url_qr = :url "
                        "WHERE n_recibo = :n_recibo");
            upd.bindValue(":csv",    "");
            upd.bindValue(":ts",     timestamp);
            upd.bindValue(":estado", "ERROR");
            upd.bindValue(":error",  result.errorDescription);
            upd.bindValue(":url",    "");
        }
        upd.bindValue(":n_recibo", ticketNum);
        upd.exec();
    }
    db.close();

    on_pb_search_clicked();
    updateRowClickedToFields();
    isCellClicked = true;

    if (result.isSuccess()) {
        QMessageBox::information(this, "Verifactu enviado",
                                 "Factura enviada correctamente a la AEAT.\n\nCSV: " + result.csv,
                                 QMessageBox::Ok);
    } else {
        qWarning() << "Verifactu retry failed for ticket" << ticketNum << "—" << result.errorDescription;
        QMessageBox::warning(this, "Error al enviar a Verifactu",
                             "No se ha podido enviar la factura a la AEAT.\n\nError: " + result.errorDescription,
                             QMessageBox::Ok);
    }
}

void RecogPrendas::on_pb_separ_garm_clicked()
{
    if (ui->le_size->text() == "") {
        if (ui->le_qty->text().toInt() > 1) {
            bool ok;
            int number = QInputDialog::getInt(this, "Separar prendas",
                                              "¿Cuántas prendas se van a pagar o entregar?", 1, 1, ui->le_qty->text().toInt() - 1, 1, &ok);
            if (ok) {
                updateDb(SEPARATE_GARM, number);
            }
        } else
            QMessageBox::warning(this, "Separar prendas",
                                     "La cantidad de la prenda seleccionada no es mayor de 1, no se puede separar en varios recibos.",
                                     QMessageBox::Ok, QMessageBox::Ok);
    } else
        QMessageBox::warning(this, "Separar prendas",
                                 "No se pueden separar prendas que tienen un tamaño distinto de 0.",
                                 QMessageBox::Ok, QMessageBox::Ok);
}
