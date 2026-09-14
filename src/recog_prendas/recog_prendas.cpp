#include "recog_prendas.h"
#include "ui_recog_prendas.h"
#include "pay_dialog.h"
#include "sql_lite.h"
#include "imprimir.h"
#include "appsettings.h"
#include "textcolordelegate.h"
#include "numberformatdelegate.h"
#include "verifactuintegration.h"
#include "verifacturesponse.h"
#include <QAbstractSpinBox>
#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QLabel>
#include <QSqlError>
#include <QSqlQuery>
#include <QStatusBar>
#include <QIntValidator>

RecogPrendas::RecogPrendas(const QSqlDatabase &database, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::RecogPrendas),
    db(database)
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
    // Quantity is an integer count: reject any non-integer keystroke in le_qty.
    ui->le_qty->setValidator(new QIntValidator(1, 9999, this));
    ui->le_search->setFocus();
    ui->tableView->verticalHeader()->setVisible(false);
}

void RecogPrendas::resetAllContents()
{
    isCellClicked = false;
    ui->le_nr_ticket->clear();
    ui->le_phone->clear();
    ui->le_mobile->clear();
    ui->le_client->clear();
    ui->le_garm->clear();
    ui->le_qty->clear();
    ui->cb_servic->setCurrentIndex(-1);
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
    // Payment date is display-only: it is written solely by PayDialog (Cobrar),
    // never from here. Editing it would be worse than useless - fecha_pago is part
    // of the AEAT invoice identity (emisor, InvoiceID, fecha), so changing it after
    // submission makes a retry register a SECOND invoice instead of being rejected
    // as duplicate, breaks reconciliation matching, and can move income into a
    // locked quarter. Read-only + no spin buttons keeps it legible but inert.
    ui->de_date_paym->setReadOnly(true);
    ui->de_date_paym->setButtonSymbols(QAbstractSpinBox::NoButtons);
    ui->de_date_paym->setToolTip(tr("La fecha de pago se registra al cobrar y no se "
                                    "puede modificar aquí."));
    // Clear the SQL query model and the view
    sqlQueryModel->clear();
    ui->tableView->setModel(sqlQueryModel);
}

void RecogPrendas::updateDb(UpdateDBop op, int nGarm)
{
    bool editLock = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_EDIT_LOCK)).toBool();
    static const char *const opNames[] = {
        "PAY_YES", "PAY_NO", "PKU_YES", "PKU_NO", "OBSV", "SIZE_AND_PRICE",
        "QTY", "SERVICE", "PRICE", "SEPARATE_GARM"
    };
    const QString ticketNum = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_N_RECIBO)).toString();
    const QString rowHash   = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_HASH)).toString();
    qDebug() << "RecogPrendas::updateDb:" << opNames[op]
             << "ticket=" << ticketNum << "hash=" << rowHash
             << "editLock=" << editLock << "nGarm=" << nGarm;
    // A locally voided garment is immutable: block every write here too, not just
    // via the disabled buttons, so no edit path can revive or charge it. OBSV is the
    // one exception - notes must stay writable so the reason for the anulacion (or
    // any later remark) can be recorded on the voided row.
    if (op != OBSV
            && sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_ESTADO)).toString()
            == QLatin1String(INGRESOS_ESTADO_ANULADO)) {
        qWarning() << "RecogPrendas::updateDb: blocked" << opNames[op]
                   << "on anulado row, ticket" << ticketNum << "hash" << rowHash;
        return;
    }
    switch (op) {
    case PAY_YES:
        // If editLock payment info cannot be changed
        if (!editLock) {
            // dont update payment date for blocked quarters
            if (readLockForMonthAndYear(db, "ingresos", ui->de_date_paym->date().month(), ui->de_date_paym->date().year()) == 1) {
                qWarning() << "updateDb PAY_YES: attempt to update payment date for a blocked quarter:" << ui->de_date_paym->date().toString("dd-MM-yyyy");
                QMessageBox::warning(this, tr("Trimestre bloqueado"),
                                     tr("La fecha de pago pertenece a un trimestre que se encuentra bloqueado por la contabilidad."),
                                     QMessageBox::Ok, QMessageBox::Ok);
            } else {
                updateTicketPayment(db, ticketNum, rowHash,
                                    ui->de_date_paym->date().toString("dd-MM-yyyy"),
                                    ui->pb_payment->text());
                // Check verifactu_estado from DB (not model) so pay-all loop does not double-submit
                QString estadoDb = ticketVerifactuEstado(db, ticketNum);
                // Dedup pay-all loop: if a submit is already in flight for this ticket,
                // a per-row check of estadoDb is not enough because the async DB write
                // hasn't happened yet. hasPendingSubmit() consults the in-memory map.
                // Unsubmitted covers SIN COBRAR too: the row is still marked unpaid
                // here (updateTicketPayment does not touch verifactu_estado), so
                // testing PENDIENTE alone would skip the AEAT submit entirely.
                if (verifactuEstadoIsUnsubmitted(verifactuEstadoFromString(estadoDb))
                        && m_verifactuIntegration && m_verifactuIntegration->isConfigured()
                        && !hasPendingSubmit(ticketNum)) {
                    retryVerifactuSubmit(ticketNum, sqlQueryModel->data(sqlQueryModel->index(
                        rowClickedCell, INGRESOS_COL_VERIFACTU_INVOICE_SEQ)).toInt());
                }
            }
        }
        else {
            QMessageBox::warning(this, tr("Ticket bloqueado"),
                                 tr("El ticket actual se encuentra bloqueado por la contabilidad."),
                                 QMessageBox::Ok, QMessageBox::Ok);
        }
        break;
    case PAY_NO:
        // If editLock payment info cannot be changed
        if (!editLock) {
            updateTicketPayment(db, ticketNum, rowHash, "", ui->pb_payment->text());
        }
        else {
            QMessageBox::warning(this, tr("Ticket bloqueado"),
                                 tr("El ticket actual se encuentra bloqueado por la contabilidad."),
                                 QMessageBox::Ok, QMessageBox::Ok);
        }
        break;
    case PKU_YES:
        updateTicketPickup(db, ticketNum, rowHash,
                           ui->de_date_pickup->date().toString("dd-MM-yyyy"),
                           ui->pb_state->text());
        break;
    case PKU_NO:
        updateTicketPickup(db, ticketNum, rowHash, "", ui->pb_state->text());
        break;
    case OBSV:
        updateTicketObservations(db, ticketNum, rowHash, ui->le_obsv->text());
        break;
    case SIZE_AND_PRICE:
        if (!editLock && ui->pb_payment->text() == "NO") {
            updateTicketSizeAndPrice(db, ticketNum, rowHash,
                                     ui->le_size->text(), ui->le_price->text());
        }
        break;
    case QTY:
        // Editing quantity re-prices the row: importe = qty * unitPrice * size.
        if (!editLock && ui->pb_payment->text() == "NO") {
            const int newQty = ui->le_qty->text().toInt();
            if (newQty < 1)
                break;
            const double importe = garmentImporte(
                ui->le_qty->text(), ui->le_size->text(),
                readGarmentPrice(db, ui->le_garm->text(), ui->cb_servic->currentText()));
            updateGarmentQtyAndImporte(db, ticketNum, rowHash,
                                       QString::number(newQty),
                                       QString::number(importe, 'f', 2));
        }
        break;
    case SERVICE:
        // A service change re-prices the row against the new service's unit price.
        if (!editLock && ui->pb_payment->text() == "NO") {
            const double importe = garmentImporte(
                ui->le_qty->text(), ui->le_size->text(),
                readGarmentPrice(db, ui->le_garm->text(), ui->cb_servic->currentText()));
            updateGarmentServiceAndImporte(db, ticketNum, rowHash,
                                           ui->cb_servic->currentText(),
                                           QString::number(importe, 'f', 2));
        }
        break;
    case PRICE:
        // Manual importe override (comma-normalised); size is left as-is.
        if (!editLock && ui->pb_payment->text() == "NO") {
            updateTicketSizeAndPrice(db, ticketNum, rowHash,
                                     ui->le_size->text().replace(",", "."),
                                     ui->le_price->text().replace(",", "."));
        }
        break;
    case SEPARATE_GARM:
        // If editLock payment info cannot be changed
        if (!editLock) {
            // Update current garments
            int newQtyUpd = ui->le_qty->text().toInt() - nGarm;
            float newImpUpd = QString::number(newQtyUpd).toFloat() * readGarmentPrice(db, ui->le_garm->text(), ui->cb_servic->currentText());
            if (newImpUpd < 0) {
                break;
            }
            updateGarmentQtyAndImporte(db, ticketNum, rowHash,
                                       QString::number(newQtyUpd),
                                       QString::number(newImpUpd, 'f', 2).replace(",","."));

            // Insert separated garments. The split row intentionally leaves verifactu_*
            // columns at their defaults: the AEAT submission for this ticket covered the
            // full importe and the chained Huella is attached to the original rows -
            // re-submitting the split-off row would create a duplicate-InvoiceID error
            // at AEAT. Helpers treat empty verifactu_estado as legacy (NotSubmitted),
            // which is the desired accounting/print behaviour for split rows.
            float newImpIns = nGarm * readGarmentPrice(db, ui->le_garm->text(), ui->cb_servic->currentText());
            IngresoGarmentRow row;
            row.nRecibo        = ui->le_nr_ticket->text();
            row.cliente        = ui->le_client->text();
            row.fechaRecepcion = ui->de_date_recep->date().toString("dd-MM-yyyy");
            row.fechaPago      = ui->pb_payment->isChecked() ? ui->de_date_paym->date().toString("dd-MM-yyyy") : QString("");
            row.fechaRecogida  = ui->pb_state->isChecked() ? ui->de_date_pickup->date().toString("dd-MM-yyyy") : QString("");
            row.importe        = QString::number(newImpIns, 'f', 2).replace(",",".");
            row.pagado         = ui->pb_payment->text();
            row.estado         = ui->pb_state->text();
            row.cantidad       = QString::number(nGarm);
            row.prenda         = ui->le_garm->text();
            row.size           = ui->le_size->text().replace(",",".");
            row.servicio       = ui->cb_servic->currentText();
            row.observaciones  = ui->le_obsv->text();
            row.editLock       = "0";
            row.hash           = genHash16();
            insertGarmentRow(db, row);
        }
        else {
            QMessageBox::warning(this, tr("Ticket bloqueado"),
                                 tr("El ticket actual se encuentra bloqueado por la contabilidad."),
                                 QMessageBox::Ok, QMessageBox::Ok);
        }
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
    ui->le_nr_ticket->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_N_RECIBO)).toString());
    ui->le_client->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_CLIENTE)).toString());
    const QStringList phones = readClientPhones(db, ui->le_client->text()); // {tel_fijo, movil} in one query
    ui->le_phone->setText(phones.value(0));
    ui->le_mobile->setText(phones.value(1));
    ui->le_garm->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_PRENDA)).toString());
    ui->le_qty->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_CANTIDAD)).toString());
    // findText returns -1 for a legacy service not in the list -> blank rather than a wrong value.
    ui->cb_servic->setCurrentIndex(ui->cb_servic->findText(
        sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_SERVICIO)).toString()));
    ui->le_size->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_SIZE)).toString());
    ui->le_price->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_IMPORTE)).toString());
    ui->le_obsv->setText(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_OBSERVACIONES)).toString());
    const QString rowEstado = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_ESTADO)).toString();
    // A locally voided garment (estado "Anulado") is read-only everywhere except
    // observations: it can never be paid, picked up, split or re-priced again.
    const bool isAnulado = rowEstado == QLatin1String(INGRESOS_ESTADO_ANULADO);
    const bool isPaid = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_PAGADO)).toString() == "SI";
    const bool editLock = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_EDIT_LOCK)).toBool();
    // Quantity / service / importe / size are editable only before payment (and while
    // not accounting-locked or voided): a paid row was already submitted to AEAT.
    const bool priceEditable = !isAnulado && !isPaid && !editLock;
    ui->pb_payment->setChecked(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_PAGADO)).toString() == "SI");
    ui->pb_state->setChecked(rowEstado == "Recogido");
    ui->de_date_recep->setDate(QDate::fromString(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_FECHA_RECEPCION)).toString(),"dd-MM-yyyy"));
    ui->de_date_paym->setDate(QDate::fromString(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_FECHA_PAGO)).toString(),"dd-MM-yyyy"));
    ui->de_date_pickup->setDate(QDate::fromString(sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_FECHA_RECOGIDA)).toString(),"dd-MM-yyyy"));
    // pb_payment kept disabled - per-garment payment would submit a Verifactu invoice
    // for the full ticket per garment, causing duplicate InvoiceID at AEAT. Use pb_pay_all.
    ui->pb_state->setEnabled(!isAnulado);
    ui->pb_pay_all->setEnabled(!isAnulado);
    ui->pb_pku_all->setEnabled(!isAnulado);
    ui->pb_separ_garm->setEnabled(!isAnulado);
    ui->pb_print->setEnabled(true);
    // Observations stay editable on a voided row: the user documents why it was
    // anulada / adds later notes. Everything else remains locked.
    ui->le_obsv->setReadOnly(false);
    ui->le_size->setReadOnly(!priceEditable);
    ui->le_price->setReadOnly(!priceEditable);
    ui->le_qty->setReadOnly(!priceEditable);
    ui->cb_servic->setEnabled(priceEditable);
    QString verifactuEstado = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_VERIFACTU_ESTADO)).toString();
    ui->pb_verifactu->setEnabled(!verifactuEstado.isEmpty());
}

float RecogPrendas::calculatePrice()
{
    float itemPrice = readGarmentPrice(db, ui->le_garm->text(), ui->cb_servic->currentText());
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
    qDebug() << "RecogPrendas::on_pb_search_clicked: input=" << ui->le_search->text()
             << "dateMode=" << ui->cb_search_date->currentText();
    if (ui->le_search->text() != "") {
        bool ok = false, totalPriceActive = true;
        QString nameSearchFilter;
        ui->le_search->text().toUInt(&ok);
        if (ok) {
            if (ui->le_search->text().length() >= 9) {
                // Phone number - search tel_fijo OR movil
                db.open();
                QSqlQuery phoneQ(db);
                phoneQ.prepare("SELECT nombre FROM clientes WHERE tel_fijo LIKE :phone OR movil LIKE :movil");
                phoneQ.bindValue(":phone", ui->le_search->text());
                phoneQ.bindValue(":movil", ui->le_search->text());
                phoneQ.exec();
                QString clientFromPhone;
                if (phoneQ.first())
                    clientFromPhone = phoneQ.value(0).toString();
                else {
                    QMessageBox::warning(this, tr("Búsqueda vacía"),
                                          tr("No se ha encontrado ningún cliente con el teléfono indicado."),
                                          QMessageBox::Ok, QMessageBox::Ok);
                }
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
            // date_type values come from a hard-coded ComboBox - not user input
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
                // QSqlQueryModel lazy-fetches in 256-row batches and fetchMore() needs the
                // db connection open. Drain the full result set here, BEFORE closing - if
                // we close first, canFetchMore() returns false and the proxy filter only
                // sees the first 256 rows, silently dropping tickets for prolific clients
                // (or any client whose receipts landed past row 256 of ingresos).
                while (sqlQueryModel->canFetchMore())
                    sqlQueryModel->fetchMore();
                db.close();
                nameSearchFilter = MySortFilterProxyModel::removeDiacritics(ui->le_search->text()).toLower();
                qDebug() << "RecogPrendas::on_pb_search_clicked: name search loaded"
                         << sqlQueryModel->rowCount() << "rows; filter=" << nameSearchFilter;
            }
        }
        else {
            qWarning() << "Search: unrecognized input:" << ui->le_search->text();
            QMessageBox::warning(this, tr("Búsqueda incorrecta"),
                                  tr("El contenido de la búsqueda no se ha identificado.\n"
                                  "Hablar con Germán..."),
                                  QMessageBox::Ok, QMessageBox::Ok);
        }
        // Complete model and set to the view
        sqlQueryModel->setHeaderData(INGRESOS_COL_N_RECIBO   , Qt::Horizontal, tr("Nº"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_CLIENTE   , Qt::Horizontal, tr("Cliente"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_FECHA_RECEPCION , Qt::Horizontal, tr("Recepción"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_FECHA_PAGO , Qt::Horizontal, tr("Pago"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_FECHA_RECOGIDA , Qt::Horizontal, tr("Recogida"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_IMPORTE    , Qt::Horizontal, tr("Importe"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_PAGADO , Qt::Horizontal, tr("Pagado"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_ESTADO    , Qt::Horizontal, tr("Estado"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_SIZE     , Qt::Horizontal, tr("Cant."));
        sqlQueryModel->setHeaderData(INGRESOS_COL_PRENDA  , Qt::Horizontal, tr("Prenda"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_SIZE     , Qt::Horizontal, tr("Tam."));
        sqlQueryModel->setHeaderData(INGRESOS_COL_SERVICIO  , Qt::Horizontal, tr("Serv."));
        sqlQueryModel->setHeaderData(INGRESOS_COL_OBSERVACIONES   , Qt::Horizontal, tr("Obs."));
        sqlQueryModel->setHeaderData(INGRESOS_COL_EDIT_LOCK, Qt::Horizontal, tr("Bloqueo"));
        // Set model to table
        proxyModel = new MySortFilterProxyModel(this);
        // lessThan() keys on table_name to pick the date / numeric comparators.
        proxyModel->table_name = "ingresos";
        if (!nameSearchFilter.isEmpty())
            proxyModel->setNormalizedFilter(nameSearchFilter, INGRESOS_COL_CLIENTE);
        proxyModel->setSourceModel(sqlQueryModel);
        ui->tableView->setModel(proxyModel);
        ui->tableView->sortByColumn(INGRESOS_COL_N_RECIBO, Qt::DescendingOrder);
        // Hide internal columns not meant for display
        ui->tableView->setColumnHidden(INGRESOS_COL_EDIT_LOCK,           true);
        ui->tableView->setColumnHidden(INGRESOS_COL_HASH,                true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_CSV,       true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_TIMESTAMP, true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_ESTADO,    true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_ERROR,     true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_URL_QR,    true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_XML,        true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_HASH,       true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_RECTIFIES_N_RECIBO,  true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_RECTIFICATION_TYPE,  true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_INVOICE_SEQ,         true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_INVOICE_ID,          true);
        ui->tableView->setItemDelegateForColumn(INGRESOS_COL_IMPORTE, new NumberFormatDelegate(this));
        ui->tableView->setItemDelegateForColumn(INGRESOS_COL_PAGADO, new TextColorDelegate(ui->tableView, this));
        ui->tableView->setItemDelegateForColumn(INGRESOS_COL_ESTADO, new TextColorDelegate(ui->tableView, this));
        ui->tableView->resizeColumnsToContents();
        ui->tableView->resizeRowsToContents();
        qDebug() << "RecogPrendas::on_pb_search_clicked: result"
                 << proxyModel->rowCount() << "of" << sqlQueryModel->rowCount() << "rows match";
        // Fill total_price from proxy rows (reflects the filtered set in all search modes)
        if (totalPriceActive) {
            float totalPrice = 0.0;
            for (int row = 0; row < proxyModel->rowCount(); row++)
                totalPrice += proxyModel->data(proxyModel->index(row, INGRESOS_COL_IMPORTE)).toFloat();
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
    // index is in proxy coords; rowClickedCell is consumed as a source row.
    const QModelIndex sourceIndex = proxyModel ? proxyModel->mapToSource(index) : index;
    selectSourceRow(sourceIndex.row(), sourceIndex.column());
}

void RecogPrendas::selectSourceRow(int sourceRow, int sourceCol)
{
    if (sourceRow != rowClickedCell)
        isCellClicked = false;
    rowClickedCell = sourceRow;
    columnClickedCell = sourceCol;
    // updateRowClickedToFields() sets the per-row button enables (respecting the
    // Anulado read-only lock), so it is the single source of truth here.
    updateRowClickedToFields();
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

void RecogPrendas::on_le_qty_editingFinished()
{
    if (isCellClicked && !ui->le_qty->isReadOnly())
        updateDb(QTY);
}

void RecogPrendas::on_le_price_editingFinished()
{
    if (isCellClicked && !ui->le_price->isReadOnly())
        updateDb(PRICE);
}

void RecogPrendas::on_cb_servic_activated(int index)
{
    Q_UNUSED(index);
    if (isCellClicked && ui->cb_servic->isEnabled())
        updateDb(SERVICE);
}

void RecogPrendas::on_pb_pay_all_clicked()
{
    // Partial-pay (8.5+): open PayDialog for the selected ticket. The dialog
    // shows every unpaid row pre-checked - the operator can untick the ones
    // not being charged this time, fires one Verifactu submit for the subset
    // (InvoiceID "<n_recibo>-<seq>"), persists the chosen rows, and prints.
    if (!isCellClicked) return;
    const QString ticketNum = sqlQueryModel->data(
        sqlQueryModel->index(rowClickedCell, INGRESOS_COL_N_RECIBO)).toString();
    if (ticketNum.isEmpty()) return;

    PayDialog dlg(db, this);
    dlg.m_verifactu = m_verifactuIntegration;
    // Adopt a submission the dialog gave up waiting on, so a late reply still
    // patches the row instead of dying with the dialog.
    connect(&dlg, &PayDialog::submitAdopted, this,
            [this](const QString &reqId, const QString &ticketNum, int seq) {
        ensureVerifactuConnected();
        m_pendingSubmits.insert(reqId, { ticketNum, seq, /*adopted=*/true });
        qDebug() << "RecogPrendas: adopted in-flight submit" << reqId
                 << "for" << verifactuInvoiceId(ticketNum, seq);
    });
    if (!dlg.loadTicket(ticketNum)) {
        QMessageBox::information(this, tr("Sin prendas pendientes"),
                                 tr("El ticket %1 no tiene prendas pendientes de cobrar.")
                                     .arg(ticketNum));
        return;
    }
    dlg.exec();
    on_pb_search_clicked();
}

void RecogPrendas::on_pb_pku_all_clicked()
{
    // Mark every garment of the clicked ticket as Recogido. The legacy version
    // iterated sqlQueryModel->rowCount() source rows and fired updateDb(PKU_YES)
    // per row, which on a name search (SELECT * FROM ingresos) had the source
    // model holding the entire table - so the loop marked every ticket in the
    // DB picked up. Same blast-radius pattern that was fixed for pay_all in +8.4;
    // close it here with the same scope: read the clicked row's n_recibo and
    // run ONE UPDATE bounded to that ticket (markTicketPickedUp, which also
    // excludes Anulado rows so a voided garment is never revived to Recogido).
    if (!isCellClicked) return;
    const QString ticketNum = sqlQueryModel->data(
        sqlQueryModel->index(rowClickedCell, INGRESOS_COL_N_RECIBO)).toString();
    if (ticketNum.isEmpty()) return;

    markTicketPickedUp(db, ticketNum, ui->de_date_pickup->date().toString("dd-MM-yyyy"));

    on_pb_search_clicked();
}

void RecogPrendas::on_pb_print_clicked()
{
    if (ui->le_nr_ticket->text().isEmpty())
        return;
    // Reprint scope = the clicked row's payment event (its verifactu_invoice_seq).
    // Refuse when the clicked row is unpaid: seq=0 there is the DEFAULT, not
    // a real event, and Imprimir would filter to that row's siblings and
    // print garments the customer never paid for.
    int seq = -1;
    if (isCellClicked) {
        const QString pagado = sqlQueryModel->data(sqlQueryModel->index(
                                   rowClickedCell, INGRESOS_COL_PAGADO)).toString();
        if (pagado != "SI") {
            QMessageBox::information(this, tr("Reimprimir factura"),
                tr("La prenda seleccionada no está pagada. Selecciona una prenda "
                   "ya pagada del evento de pago que quieras reimprimir."));
            return;
        }
        seq = sqlQueryModel->data(sqlQueryModel->index(
                  rowClickedCell, INGRESOS_COL_VERIFACTU_INVOICE_SEQ)).toInt();
    }
    printFactura(ui->le_nr_ticket->text(), /*askSecondCopy=*/false, seq);
    resetAllContents();
}

void RecogPrendas::printFactura(const QString &ticketNum, bool askSecondCopy, int invoiceSeq)
{
    if (ticketNum.isEmpty())
        return;
    qDebug() << "RecogPrendas::printFactura: ticket=" << ticketNum
             << "askSecondCopy=" << askSecondCopy << "invoiceSeq=" << invoiceSeq;
    Imprimir *ui_impr = new Imprimir(db, this);
    ui_impr->isRecibo = false;
    ui_impr->isCompleteInvoice = false;
    ui_impr->verifactuIntegration = m_verifactuIntegration;
    ui_impr->invoiceSeq = invoiceSeq;
    ui_impr->le_n_ticket->setText(ticketNum);
    ui_impr->getTicketInfo();
    ui_impr->buildTicket(false, false);
    if (!AppSettings::instance()->enablePrinting())
        return;
    ui_impr->printTicket();
    if (!askSecondCopy)
        return;
    const auto resp = QMessageBox::question(this, tr("Segunda copia"),
                                            tr("¿Imprimir una segunda copia de la factura?"),
                                            QMessageBox::Yes | QMessageBox::No,
                                            QMessageBox::No);
    if (resp == QMessageBox::Yes)
        ui_impr->printTicket();
}

void RecogPrendas::on_pb_verifactu_clicked()
{
    if (!isCellClicked) return;

    QString ticketNum   = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_N_RECIBO)).toString();
    QString state       = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_VERIFACTU_ESTADO)).toString();
    QString csv         = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_VERIFACTU_CSV)).toString();
    QString timestamp   = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_VERIFACTU_TIMESTAMP)).toString();
    QString error       = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_VERIFACTU_ERROR)).toString();
    QString url         = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_VERIFACTU_URL_QR)).toString();
    // The retry needs the row's payment event, not the reception date: AEAT keys
    // an invoice on (emisor, InvoiceID, fecha), so retryVerifactuSubmit re-reads
    // the event's own fecha_pago from the DB.
    const int rowSeq    = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_VERIFACTU_INVOICE_SEQ)).toInt();

    QDialog *dlg = new QDialog(this);
    dlg->setWindowTitle("Verifactu - Ticket " + ticketNum);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout *layout = new QVBoxLayout(dlg);

    auto addRow = [&](const QString &label, const QString &value) {
        QLabel *lbl = new QLabel(QString("<b>%1</b> %2").arg(label, value.isEmpty() ? "-" : value));
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(lbl);
    };

    addRow("Estado:", state);
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

    const VerifactuEstado stateEnum = verifactuEstadoFromString(state);
    const bool verifactuUsable = m_verifactuIntegration && m_verifactuIntegration->isConfigured();
    if (stateEnum == VerifactuEstado::Error && verifactuUsable) {
        QPushButton *btnRetry = new QPushButton("Reintentar envío a AEAT", dlg);
        connect(btnRetry, &QPushButton::clicked, this, [this, dlg, ticketNum, rowSeq]() {
            dlg->accept();
            retryVerifactuSubmit(ticketNum, rowSeq);
        });
        layout->addWidget(btnRetry);
    }
    // Offered on ANY paid row, not just unsettled ones: the query is read-only, so
    // on an already-settled row it simply confirms what AEAT holds (the apply
    // button stays disabled there). An unpaid row has no invoice to ask about.
    const bool rowPaid = sqlQueryModel->data(
        sqlQueryModel->index(rowClickedCell, INGRESOS_COL_PAGADO)).toString() == QLatin1String("SI");
    const bool alreadySettled = stateEnum == VerifactuEstado::Enviada
                             || stateEnum == VerifactuEstado::Anulada
                             || stateEnum == VerifactuEstado::Rectificada;
    if (verifactuUsable && rowPaid) {
        QPushButton *btnQuery = new QPushButton("Consultar en AEAT", dlg);
        btnQuery->setToolTip(alreadySettled
            ? "Consulta a AEAT los datos registrados de esta factura (solo informativo)."
            : "Comprueba si AEAT ya tiene esta factura y permite recuperar su CSV.");
        connect(btnQuery, &QPushButton::clicked, this,
                [this, dlg, ticketNum, rowSeq, alreadySettled]() {
            dlg->accept();
            queryAeatAndOfferReconcile(ticketNum, rowSeq, alreadySettled);
        });
        layout->addWidget(btnQuery);
    }

    QPushButton *btnClose = new QPushButton("Cerrar", dlg);
    connect(btnClose, &QPushButton::clicked, dlg, &QDialog::accept);
    layout->addWidget(btnClose);

    dlg->setMinimumWidth(420);
    dlg->exec();
}

void RecogPrendas::retryVerifactuSubmit(const QString &ticketNum, int seq)
{
    if (!m_verifactuIntegration || !m_verifactuIntegration->isConfigured()) {
        qWarning() << "retryVerifactuSubmit: Verifactu not configured for ticket" << ticketNum;
        return;
    }

    // Re-submit the ONE payment event, under its own InvoiceID, its own total and
    // its original fecha_pago. The old version sent the bare n_recibo with the
    // whole ticket's importe on the reception date, which for a partial-pay event
    // meant a wrong amount under an ID belonging to a different event.
    const PendingVerifactuEvent ev = verifactuEventFor(db, ticketNum, seq);
    const QString invoiceId = verifactuInvoiceId(ticketNum, seq);
    const QDate invoiceDate = QDate::fromString(ev.fechaPago, "dd-MM-yyyy");
    if (ev.nRecibo.isEmpty() || !invoiceDate.isValid()) {
        qWarning() << "retryVerifactuSubmit: no paid event" << invoiceId
                   << "- nothing to re-submit";
        QMessageBox::warning(this, tr("Verifactu"),
                             tr("No se puede reenviar el ticket %1: no consta como cobrado.")
                                 .arg(invoiceId));
        return;
    }

    double ivaRate = AppSettings::instance()->ivaRate();
    ensureVerifactuConnected();
    const QString reqId = m_verifactuIntegration->submitSimplifiedInvoiceAsync(
        invoiceId,
        invoiceDate,
        ev.importe / (1.0 + ivaRate / 100.0),
        ivaRate,
        "Servicios de lavanderia"
    );
    if (reqId.isEmpty()) {
        qWarning() << "retryVerifactuSubmit: Verifactu rejected request for" << invoiceId;
        return;
    }
    m_pendingSubmits.insert(reqId, { ticketNum, seq });
    statusBar()->showMessage(tr("Enviando ticket %1 a AEAT...").arg(invoiceId));
}

void RecogPrendas::queryAeatAndOfferReconcile(const QString &ticketNum, int seq,
                                              bool localAlreadySettled)
{
    if (!m_verifactuIntegration || !m_verifactuIntegration->isConfigured()) {
        QMessageBox::warning(this, tr("Verifactu no configurado"),
                             tr("Verifactu no está configurado, no se puede consultar a AEAT."));
        return;
    }
    const PendingVerifactuEvent ev = verifactuEventFor(db, ticketNum, seq);
    const QString invoiceId = verifactuInvoiceId(ticketNum, seq);
    if (ev.nRecibo.isEmpty()) {
        QMessageBox::warning(this, tr("Verifactu"),
                             tr("El ticket %1 no consta como cobrado, no hay factura que consultar.")
                                 .arg(invoiceId));
        return;
    }

    const QString reqId = m_verifactuIntegration->queryInvoiceAsync(invoiceId);
    if (reqId.isEmpty()) return;

    statusBar()->showMessage(tr("Consultando %1 en AEAT...").arg(invoiceId));
    // One-shot: disconnect as soon as our own reqId answers.
    auto *conn = new QMetaObject::Connection;
    *conn = connect(m_verifactuIntegration, &VerifactuIntegration::queryFinished, this,
        [this, conn, reqId, ticketNum, seq, ev, invoiceId, localAlreadySettled]
        (const QString &id, const VerifactuRemoteRecord &rec) {
            if (id != reqId) return;
            disconnect(*conn);
            delete conn;
            showAeatReconcileDialog(ticketNum, seq, invoiceId, ev, rec, localAlreadySettled);
        });
}

void RecogPrendas::showAeatReconcileDialog(const QString &ticketNum, int seq,
                                           const QString &invoiceId,
                                           const PendingVerifactuEvent &ev,
                                           const VerifactuRemoteRecord &rec,
                                           bool localAlreadySettled)
{
    statusBar()->clearMessage();
    const bool matches = verifactuRemoteMatches(rec, invoiceId, ev.fechaPago, ev.importe);
    // A row that is already ENVIADA / ANULADA / RECTIFICADA has nothing to adopt -
    // reconcileVerifactuFromAeat would refuse it anyway - so the query is purely
    // informative there and the apply button stays disabled.
    const bool canAdopt = matches && rec.hasUsableCsv() && !localAlreadySettled;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Consulta AEAT - %1").arg(invoiceId));
    auto *layout = new QVBoxLayout(&dlg);

    auto *summary = new QLabel(&dlg);
    summary->setTextFormat(Qt::RichText);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (!rec.parsed) {
        // Could not read the answer: this is NOT evidence the invoice is absent.
        summary->setText(tr("<b>No se ha podido interpretar la respuesta de AEAT.</b><br>"
                            "Esto <i>no</i> significa que la factura no esté registrada. "
                            "Revisa el detalle de abajo y consulta la sede electrónica."));
    } else if (!rec.found) {
        // Deliberately not "se puede reenviar con seguridad": the query reply does
        // not echo the filter we sent, so an empty result is not yet proof that the
        // InvoiceID filter was applied. Claiming a false all-clear here would push
        // the operator straight into a duplicate submission.
        summary->setText(tr("<b>AEAT no ha devuelto ninguna factura con el número %1.</b><br>"
                            "Lo más probable es que no llegara a registrarse. Aun así, "
                            "antes de reenviar conviene confirmarlo en la sede electrónica "
                            "de la AEAT: si ya constara allí, el reenvío se rechazaría por "
                            "duplicado.").arg(invoiceId));
    } else {
        summary->setText(tr("<b>AEAT tiene registrada esta factura.</b><br>"
                            "%1").arg(matches
                                ? tr("Los datos coinciden con los del ticket.")
                                : tr("<span style='color:#b00'>Los datos NO coinciden con los del "
                                     "ticket - no se puede actualizar automáticamente.</span>")));
    }
    layout->addWidget(summary);

    auto *table = new QTableWidget(4, 3, &dlg);
    table->setHorizontalHeaderLabels({ tr("Campo"), tr("AEAT"), tr("Ticket") });
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    const QString localAmount = QString::number(ev.importe, 'f', 2);
    const QString remoteAmount = rec.totalAmount > 0 ? QString::number(rec.totalAmount, 'f', 2)
                                                     : QString("-");
    const QStringList fields = { tr("Nº factura"), tr("Fecha"), tr("Importe"), tr("CSV") };
    const QStringList remote = { rec.invoiceId, rec.invoiceDate, remoteAmount, rec.csv };
    const QStringList local  = { invoiceId, ev.fechaPago, localAmount, QString("-") };
    for (int r = 0; r < 4; ++r) {
        table->setItem(r, 0, new QTableWidgetItem(fields[r]));
        table->setItem(r, 1, new QTableWidgetItem(remote[r].isEmpty() ? "-" : remote[r]));
        table->setItem(r, 2, new QTableWidgetItem(local[r]));
    }
    table->setMinimumHeight(150);
    layout->addWidget(table);

    // The raw payload is always available: the response schema is unpublished, so
    // this is the only way to tell a real mismatch from a parser that guessed wrong.
    auto *rawBox = new QTextEdit(&dlg);
    rawBox->setReadOnly(true);
    rawBox->setPlainText(rec.raw);
    rawBox->setVisible(false);
    auto *btnRaw = new QPushButton(tr("Ver respuesta completa"), &dlg);
    btnRaw->setCheckable(true);
    connect(btnRaw, &QPushButton::toggled, rawBox, &QWidget::setVisible);
    layout->addWidget(btnRaw);
    layout->addWidget(rawBox);

    auto *btnRow = new QHBoxLayout();
    auto *btnApply = new QPushButton(tr("Actualizar con los datos de AEAT"), &dlg);
    btnApply->setEnabled(canAdopt);
    if (!canAdopt && localAlreadySettled)
        btnApply->setToolTip(tr("El ticket ya está registrado localmente; "
                                "esta consulta es solo informativa."));
    else if (!canAdopt && rec.found && matches)
        btnApply->setToolTip(tr("AEAT no ha devuelto el CSV de la factura."));
    auto *btnClose = new QPushButton(tr("Cerrar"), &dlg);
    btnRow->addWidget(btnApply);
    btnRow->addStretch();
    btnRow->addWidget(btnClose);
    layout->addLayout(btnRow);
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnApply, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.setMinimumWidth(560);
    if (dlg.exec() != QDialog::Accepted || !canAdopt)
        return;

    const int rows = reconcileVerifactuFromAeat(db, ticketNum, seq, rec.csv, rec.validationUrl);
    if (rows > 0) {
        QMessageBox::information(this, tr("Verifactu"),
            tr("El ticket %1 se ha actualizado con el CSV de AEAT (%2).")
                .arg(invoiceId, rec.csv));
        on_pb_search_clicked();
    } else {
        QMessageBox::warning(this, tr("Verifactu"),
            tr("No se ha actualizado ninguna fila del ticket %1.").arg(invoiceId));
    }
}

void RecogPrendas::onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result)
{
    auto it = m_pendingSubmits.find(requestId);
    if (it == m_pendingSubmits.end()) return; // not one of ours
    const QString ticketNum = it.value().ticketNum;
    const int     seq       = it.value().seq;
    const bool    adopted   = it.value().adopted;
    m_pendingSubmits.erase(it);

    updateTicketVerifactuFields(db, ticketNum, result, seq);

    // Refresh the table so the new estado is visible (only if user is still on this view)
    on_pb_search_clicked();
    if (rowClickedCell >= 0 && rowClickedCell < sqlQueryModel->rowCount()) {
        updateRowClickedToFields();
        isCellClicked = true;
    }

    if (result.isSuccess()) {
        qDebug() << "Verifactu submit successful for ticket" << ticketNum << "- CSV:" << result.csv;
        // An adopted reply landed after the customer already got a QR-less recibo,
        // so say the factura can now be printed rather than just "enviado".
        statusBar()->showMessage(
            adopted ? tr("AEAT ha confirmado el ticket %1 - ya se puede imprimir la factura con QR")
                          .arg(verifactuInvoiceId(ticketNum, seq))
                    : tr("Ticket %1 enviado a AEAT (CSV: %2)").arg(ticketNum, result.csv),
            adopted ? 30000 : 10000);
    } else {
        qWarning() << "Verifactu submit failed for ticket" << ticketNum << "-" << result.errorDescription;
        statusBar()->showMessage(
            tr("Error al enviar ticket %1: %2").arg(ticketNum, result.errorDescription), 15000);
        // "Already exists" means AEAT HAS the invoice and we lost the reply, so a
        // further retry can only fail the same way. Close the loop here: ask AEAT
        // what it holds and offer to adopt its CSV.
        if (verifactuErrorIsDuplicate(result.errorCode, result.errorDescription)) {
            qDebug() << "Verifactu: duplicate rejection for" << verifactuInvoiceId(ticketNum, seq)
                     << "- querying AEAT to reconcile";
            queryAeatAndOfferReconcile(ticketNum, seq);
        }
    }
}

void RecogPrendas::ensureVerifactuConnected()
{
    if (m_verifactuIntegration)
        connect(m_verifactuIntegration, &VerifactuIntegration::requestFinished,
                this, &RecogPrendas::onVerifactuRequestFinished, Qt::UniqueConnection);
}

bool RecogPrendas::hasPendingSubmit(const QString &ticketNum) const
{
    for (auto it = m_pendingSubmits.constBegin(); it != m_pendingSubmits.constEnd(); ++it) {
        if (it.value().ticketNum == ticketNum) return true;
    }
    return false;
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
        } else {
            qWarning() << "Garment split blocked: quantity is not greater than 1";
            QMessageBox::warning(this, "Separar prendas",
                                     "La cantidad de la prenda seleccionada no es mayor de 1, no se puede separar en varios recibos.",
                                     QMessageBox::Ok, QMessageBox::Ok);
        }
    } else {
        qWarning() << "Garment split blocked: garment has a non-zero size";
        QMessageBox::warning(this, "Separar prendas",
                                 "No se pueden separar prendas que tienen un tamaño distinto de 0.",
                                 QMessageBox::Ok, QMessageBox::Ok);
    }
}
