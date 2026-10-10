#include "recog_prendas.h"
#include "pay_dialog.h"
#include "sql_lite.h"
#include "ingresoscolumns.h"
#include "imprimir.h"
#include "appsettings.h"
#include "textcolordelegate.h"
#include "numberformatdelegate.h"
#include "verifactuintegration.h"
#include "verifacturesponse.h"
#include "uikit.h"
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableView>
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
    ui(new Widgets),
    db(database)
{
    buildUi();
    initialSettings();
}

RecogPrendas::~RecogPrendas()
{
    delete ui;
}

void RecogPrendas::buildUi()
{
    setObjectName("RecogPrendas");
    setWindowTitle(tr("Recogida de prendas"));
    resize(1180, 820);

    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    ui->lbl_title = UiKit::heading(tr("Recogida de prendas"));
    layout->addWidget(ui->lbl_title);
    layout->addWidget(UiKit::introPanel(tr(
        "Busque por Nº de recibo, teléfono, nombre del cliente o fecha. Al seleccionar una prenda "
        "se muestran sus datos debajo; Cobrar, Recoger todo, Imprimir y Verifactu se aplican al "
        "ticket de la prenda seleccionada.")));

    // Search ---------------------------------------------------------------
    QGroupBox *grpSearch = new QGroupBox(tr("Búsqueda"));
    QHBoxLayout *searchRow = new QHBoxLayout(grpSearch);
    ui->le_search = new QLineEdit();
    ui->le_search->setPlaceholderText(tr("Nº de recibo, teléfono, nombre del cliente o fecha (dd-mm-aaaa)"));
    searchRow->addWidget(ui->le_search, 1);
    searchRow->addWidget(new QLabel(tr("Fecha de:")));
    ui->cb_search_date = new QComboBox();
    ui->cb_search_date->addItems({ tr("Recepción"), tr("Pago"), tr("Recogida"), tr("Anulación") });
    ui->cb_search_date->setToolTip(tr("Qué fecha se busca cuando la búsqueda es una fecha."));
    searchRow->addWidget(ui->cb_search_date);
    ui->pb_search = UiKit::secondaryButton(tr("Buscar"), "pb_search");
    searchRow->addWidget(ui->pb_search);
    ui->pb_reset = UiKit::secondaryButton(tr("Limpiar"), "pb_reset");
    searchRow->addWidget(ui->pb_reset);
    layout->addWidget(grpSearch);

    ui->tableView = new QTableView();
    ui->tableView->setAlternatingRowColors(true);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableView->setSortingEnabled(true);
    ui->tableView->verticalHeader()->setVisible(false);
    layout->addWidget(ui->tableView, 1);

    // Selected garment ---------------------------------------------------------
    QHBoxLayout *details = new QHBoxLayout();
    QGroupBox *grpGarment = new QGroupBox(tr("Prenda seleccionada"));
    QGridLayout *g = new QGridLayout(grpGarment);
    const auto field = [](QLineEdit *&edit, bool readOnly, const QString &tip = QString()) {
        edit = new QLineEdit();
        edit->setReadOnly(readOnly);
        edit->setToolTip(tip);
        return edit;
    };
    g->addWidget(new QLabel(tr("Nº recibo:")), 0, 0);
    g->addWidget(field(ui->le_nr_ticket, true), 0, 1);
    g->addWidget(new QLabel(tr("Cliente:")), 0, 2);
    g->addWidget(field(ui->le_client, true), 0, 3);
    g->addWidget(new QLabel(tr("Teléfono:")), 1, 0);
    g->addWidget(field(ui->le_phone, true), 1, 1);
    g->addWidget(new QLabel(tr("Móvil:")), 1, 2);
    g->addWidget(field(ui->le_mobile, true), 1, 3);
    g->addWidget(new QLabel(tr("Prenda:")), 2, 0);
    g->addWidget(field(ui->le_garm, true), 2, 1);
    g->addWidget(new QLabel(tr("Servicio:")), 2, 2);
    ui->cb_servic = new QComboBox();
    ui->cb_servic->addItems({ "Limp.", "Plan." });
    ui->cb_servic->setToolTip(tr("Cambiar el servicio de la prenda si aún no está pagada. "
                                 "El importe se recalcula automáticamente."));
    g->addWidget(ui->cb_servic, 2, 3);
    g->addWidget(new QLabel(tr("Cantidad:")), 3, 0);
    g->addWidget(field(ui->le_qty, false, tr("Editar la cantidad si aún no está pagada. "
                                             "El importe se recalcula automáticamente.")), 3, 1);
    g->addWidget(new QLabel(tr("Tamaño (m2):")), 3, 2);
    g->addWidget(field(ui->le_size, false, tr("Solo en prendas por m2 sin pagar: el importe se calcula "
                                              "con el tamaño.")), 3, 3);
    g->addWidget(new QLabel(tr("Importe:")), 4, 0);
    g->addWidget(field(ui->le_price, false, tr("Editar a mano el importe si aún no está pagada.")), 4, 1);
    g->addWidget(new QLabel(tr("Observaciones:")), 5, 0);
    g->addWidget(field(ui->le_obsv, false, tr("Notas de la prenda; se guardan al salir del campo.")), 5, 1, 1, 3);
    g->setColumnStretch(1, 1);
    g->setColumnStretch(3, 2);
    details->addWidget(grpGarment, 3);

    // State and dates ----------------------------------------------------------
    QGroupBox *grpState = new QGroupBox(tr("Estado y fechas"));
    QGridLayout *s = new QGridLayout(grpState);
    ui->de_date_recep  = UiKit::dateEdit(QDate::currentDate(), "de_date_recep");
    ui->de_date_paym   = UiKit::dateEdit(QDate::currentDate(), "de_date_paym");
    ui->de_date_pickup = UiKit::dateEdit(QDate::currentDate(), "de_date_pickup");
    ui->de_date_pickup->setToolTip(tr("Fecha que se guarda al marcar la prenda (o el ticket) como recogida."));
    ui->pb_payment = new QCheckBox(tr("Pagada"));
    ui->pb_payment->setEnabled(false);   // payment happens through Cobrar only
    ui->pb_state = new QCheckBox(tr("Recogida"));
    ui->pb_state->setToolTip(tr("Marca o desmarca la prenda seleccionada como recogida."));
    ui->lbl_payment_badge = new QLabel();
    ui->lbl_state_badge = new QLabel();
    s->addWidget(new QLabel(tr("Recepción:")), 0, 0);
    s->addWidget(ui->de_date_recep, 0, 1);
    s->addWidget(ui->pb_payment, 1, 0);
    s->addWidget(ui->de_date_paym, 1, 1);
    s->addWidget(ui->lbl_payment_badge, 1, 2);
    s->addWidget(ui->pb_state, 2, 0);
    s->addWidget(ui->de_date_pickup, 2, 1);
    s->addWidget(ui->lbl_state_badge, 2, 2);
    s->setRowStretch(3, 1);
    details->addWidget(grpState, 2);

    // Ticket actions -------------------------------------------------------------
    QGroupBox *grpTicket = new QGroupBox(tr("Ticket"));
    QVBoxLayout *t = new QVBoxLayout(grpTicket);
    ui->lbl_total = new QLabel();
    QFont totalFont = ui->lbl_total->font();
    totalFont.setPointSizeF(totalFont.pointSizeF() + 3);
    totalFont.setBold(true);
    ui->lbl_total->setFont(totalFont);
    ui->lbl_total->setToolTip(tr("Suma de las prendas listadas (sin anuladas ni rectificadas)."));
    t->addWidget(ui->lbl_total);
    ui->pb_pay_all = UiKit::primaryButton(tr("Cobrar…"), "pb_pay_all");
    ui->pb_pay_all->setToolTip(tr("Cobrar las prendas pendientes del ticket seleccionado."));
    t->addWidget(ui->pb_pay_all);
    ui->pb_pku_all = UiKit::secondaryButton(tr("Recoger todo"), "pb_pku_all");
    ui->pb_pku_all->setToolTip(tr("Marcar todas las prendas del ticket como recogidas en la fecha de recogida."));
    t->addWidget(ui->pb_pku_all);
    QHBoxLayout *splitRow = new QHBoxLayout();
    ui->pb_separ_garm = UiKit::secondaryButton(tr("Separar"), "pb_separ_garm");
    ui->pb_separ_garm->setToolTip(tr("Pasa estas prendas a una fila aparte, para pagarlas o entregarlas por separado."));
    ui->sb_separ = new QSpinBox();
    ui->sb_separ->setObjectName("sb_separ");
    ui->sb_separ->setRange(1, 1);
    splitRow->addWidget(ui->pb_separ_garm);
    splitRow->addWidget(ui->sb_separ);
    splitRow->addWidget(new QLabel(tr("prenda(s)")));
    splitRow->addStretch();
    t->addLayout(splitRow);
    ui->pb_print = UiKit::secondaryButton(tr("Imprimir factura"), "pb_print");
    ui->pb_print->setToolTip(tr("Reimprimir la factura del pago de la prenda seleccionada."));
    t->addWidget(ui->pb_print);
    ui->pb_verifactu = UiKit::secondaryButton(tr("Verifactu…"), "pb_verifactu");
    ui->pb_verifactu->setToolTip(tr("Ver información de Verifactu para el ticket seleccionado."));
    t->addWidget(ui->pb_verifactu);
    t->addStretch();
    details->addWidget(grpTicket, 2);
    layout->addLayout(details);

    m_result = new UiKit::ResultPanel();
    m_result->setMinimumHeight(44);
    layout->addWidget(m_result);
    setCentralWidget(central);
    statusBar();   // AEAT replies arrive asynchronously and are announced there

    // Same object names as the former form: the on_<name>_<signal> slots connect by
    // name, and the e2e bench finds the widgets by them.
    ui->le_search->setObjectName("le_search");
    ui->cb_search_date->setObjectName("cb_search_date");
    ui->tableView->setObjectName("tableView");
    ui->le_nr_ticket->setObjectName("le_nr_ticket");
    ui->le_client->setObjectName("le_client");
    ui->le_phone->setObjectName("le_phone");
    ui->le_mobile->setObjectName("le_mobile");
    ui->le_garm->setObjectName("le_garm");
    ui->cb_servic->setObjectName("cb_servic");
    ui->le_qty->setObjectName("le_qty");
    ui->le_size->setObjectName("le_size");
    ui->le_price->setObjectName("le_price");
    ui->le_obsv->setObjectName("le_obsv");
    ui->pb_payment->setObjectName("pb_payment");
    ui->pb_state->setObjectName("pb_state");
    QMetaObject::connectSlotsByName(this);
}

void RecogPrendas::initialSettings()
{
    resetAllContents();
    // Quantity is an integer count: reject any non-integer keystroke in le_qty.
    ui->le_qty->setValidator(new QIntValidator(1, 9999, this));
    ui->le_search->setFocus();
    m_result->showInfo(tr("Introduzca una búsqueda y pulse Intro."));
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
    ui->lbl_total->setText(tr("Importe total: -"));
    ui->pb_payment->setChecked(false);
    ui->pb_state->setChecked(false);
    on_pb_payment_toggled(false);
    showPickupBadge(false);
    ui->pb_state->setEnabled(false);
    ui->pb_pay_all->setEnabled(false);
    ui->pb_pku_all->setEnabled(false);
    ui->pb_separ_garm->setEnabled(false);
    ui->sb_separ->setEnabled(false);
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
    // Reception date is display-only for the same reason: nothing writes it back to
    // the clicked row. Its one remaining reader is the split-off row in
    // SEPARATE_GARM, which must inherit the original reception date anyway - a
    // hand-typed value there would also shift the row in or out of the Verifactu
    // startup-recovery window, which gates on fecha_recepcion.
    ui->de_date_recep->setReadOnly(true);
    ui->de_date_recep->setButtonSymbols(QAbstractSpinBox::NoButtons);
    ui->de_date_recep->setToolTip(tr("La fecha de recepción se fija al crear el "
                                     "ticket y no se puede modificar aquí."));
    // Clear the SQL query model and the view
    sqlQueryModel->clear();
    ui->tableView->setModel(sqlQueryModel);
}

void RecogPrendas::updateDb(UpdateDBop op, int nGarm)
{
    bool editLock = sqlQueryModel->data(sqlQueryModel->index(rowClickedCell, INGRESOS_COL_EDIT_LOCK)).toBool();
    static const char *const opNames[] = {
        "PKU_YES", "PKU_NO", "OBSV", "SIZE_AND_PRICE",
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
    case PKU_YES:
        updateTicketPickup(db, ticketNum, rowHash,
                           ui->de_date_pickup->date().toString("dd-MM-yyyy"), "Recogido");
        break;
    case PKU_NO:
        updateTicketPickup(db, ticketNum, rowHash, "", "En tienda");
        break;
    case OBSV:
        updateTicketObservations(db, ticketNum, rowHash, ui->le_obsv->text());
        break;
    case SIZE_AND_PRICE:
        if (!editLock && !ui->pb_payment->isChecked()) {
            updateTicketSizeAndPrice(db, ticketNum, rowHash,
                                     ui->le_size->text(), ui->le_price->text());
        }
        break;
    case QTY:
        // Editing quantity re-prices the row: importe = qty * unitPrice * size.
        if (!editLock && !ui->pb_payment->isChecked()) {
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
        if (!editLock && !ui->pb_payment->isChecked()) {
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
        if (!editLock && !ui->pb_payment->isChecked()) {
            updateTicketSizeAndPrice(db, ticketNum, rowHash,
                                     ui->le_size->text().replace(",", "."),
                                     ui->le_price->text().replace(",", "."));
        }
        break;
    case SEPARATE_GARM:
        // If editLock payment info cannot be changed
        if (!editLock) {
            // The split-off row keeps the original's invoice (seq, estado, CSV): on a
            // paid row those garments are part of the invoice AEAT registered.
            splitGarmentRow(db, ticketNum, rowHash, nGarm);
        }
        else {
            m_result->setText(UiKit::errorHtml(tr("Ticket bloqueado por la contabilidad."))
                              + "<br>" + tr("No se pueden separar prendas de un trimestre cerrado."));
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
    const int qty = ui->le_qty->text().toInt();
    ui->sb_separ->setRange(1, qMax(1, qty - 1));
    ui->sb_separ->setEnabled(!isAnulado && qty > 1);
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

double RecogPrendas::calculatePrice()
{
    if (ui->le_size->text().contains(",")) {
        QStringList sizeSplitted = ui->le_size->text().split(",");
        ui->le_size->setText(sizeSplitted.first() + "." + sizeSplitted.last());
    }
    return garmentImporte(ui->le_qty->text(), ui->le_size->text(),
                          readGarmentPrice(db, ui->le_garm->text(), ui->cb_servic->currentText()));
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
    bool searchProblem = false;
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
                    m_result->setText(UiKit::warnHtml(tr("No hay ningún cliente con el teléfono %1.")
                                                          .arg(ui->le_search->text().toHtmlEscaped())));
                    searchProblem = true;
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
                               (ui->cb_search_date->currentText() == "Recogida") ? "fecha_recogida" :
                               (ui->cb_search_date->currentText() == "Anulación") ? "fecha_anulacion" : "";
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
            m_result->setText(UiKit::warnHtml(tr("No se reconoce la búsqueda."))
                              + "<br>" + tr("Escriba un Nº de recibo, un teléfono, un nombre o una fecha dd-mm-aaaa."));
            searchProblem = true;
        }
        // Complete model and set to the view
        sqlQueryModel->setHeaderData(INGRESOS_COL_N_RECIBO   , Qt::Horizontal, tr("Nº"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_CLIENTE   , Qt::Horizontal, tr("Cliente"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_FECHA_RECEPCION , Qt::Horizontal, tr("Recepción"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_FECHA_PAGO , Qt::Horizontal, tr("Pago"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_FECHA_RECOGIDA , Qt::Horizontal, tr("Recogida"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_FECHA_ANULACION, Qt::Horizontal, tr("Anulación"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_IMPORTE    , Qt::Horizontal, tr("Importe"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_PAGADO , Qt::Horizontal, tr("Pagado"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_ESTADO    , Qt::Horizontal, tr("Estado"));
        sqlQueryModel->setHeaderData(INGRESOS_COL_CANTIDAD , Qt::Horizontal, tr("Cant."));
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
        placeIngresosDateColumns(ui->tableView->horizontalHeader());
        // Hide internal columns not meant for display
        ui->tableView->setColumnHidden(INGRESOS_COL_EDIT_LOCK,           true);
        ui->tableView->setColumnHidden(INGRESOS_COL_HASH,                true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_CSV,       true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_TIMESTAMP, true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_ESTADO,    true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_ERROR,     true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_URL_QR,    true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_XML,        true);
        ui->tableView->setColumnHidden(INGRESOS_COL_VERIFACTU_CANCEL_XML, true);
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
        // Fill total_price from proxy rows (reflects the filtered set in all search modes).
        // Voided / cancelled / superseded rows (ANULADA, RECTIFICADA) are skipped.
        // Unpaid rows still count, so a ticket searched by number shows the amount owed.
        if (totalPriceActive) {
            float totalPrice = 0.0;
            for (int row = 0; row < proxyModel->rowCount(); row++) {
                const QString vEstado = proxyModel->data(proxyModel->index(row, INGRESOS_COL_VERIFACTU_ESTADO)).toString();
                if (!garmentExcludedFromTotals(vEstado))
                    totalPrice += proxyModel->data(proxyModel->index(row, INGRESOS_COL_IMPORTE)).toFloat();
            }
            ui->lbl_total->setText(tr("Importe total: %1 €").arg(moneyText(totalPrice).replace('.', ',')));
        }
        else
            ui->lbl_total->setText(tr("Importe total: -"));
        if (!searchProblem)
            m_result->showInfo(proxyModel->rowCount() == 0
                                   ? tr("No se ha encontrado ninguna prenda.")
                                   : tr("%1 prenda(s) encontrada(s). Seleccione una para ver sus datos.")
                                         .arg(proxyModel->rowCount()));
    }
    else {
        sqlQueryModel->clear();
        ui->tableView->setModel(sqlQueryModel);
    }
}

void RecogPrendas::on_pb_reset_clicked()
{
    ui->le_search->clear();
    resetAllContents();
    m_result->showInfo(tr("Introduzca una búsqueda y pulse Intro."));
    ui->le_search->setFocus();
}

void RecogPrendas::on_pb_payment_toggled(bool checked)
{
    ui->lbl_payment_badge->setText(checked ? UiKit::okHtml(tr("SÍ")) : UiKit::errorHtml(tr("NO")));
}

void RecogPrendas::showPickupBadge(bool pickedUp)
{
    ui->lbl_state_badge->setText(pickedUp ? UiKit::okHtml(tr("Recogido")) : UiKit::errorHtml(tr("En tienda")));
}

void RecogPrendas::on_pb_state_toggled(bool checked)
{
    showPickupBadge(checked);
    if (isCellClicked)
        updateDb(checked ? PKU_YES : PKU_NO);
}

void RecogPrendas::on_tableView_clicked(const QModelIndex &index)
{
    // index is in proxy coords; rowClickedCell is consumed as a source row.
    const QModelIndex sourceIndex = proxyModel ? proxyModel->mapToSource(index) : index;
    selectSourceRow(sourceIndex.row());
}

void RecogPrendas::selectSourceRow(int sourceRow)
{
    if (sourceRow != rowClickedCell)
        isCellClicked = false;
    rowClickedCell = sourceRow;
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
    // No size yet: nothing to price (garmentImporte reads size 0 as "no size factor").
    if (isCellClicked && ui->le_garm->text().contains("m2")
            && ui->le_size->text().replace(',', '.').toDouble() > 0) {
        const double price = calculatePrice();
        if (price > 0) {
            ui->le_price->setText(moneyText(price));
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
        m_result->setText(UiKit::warnHtml(tr("El ticket %1 no tiene prendas pendientes de cobrar.")
                                              .arg(ticketNum.toHtmlEscaped())));
        return;
    }
    const bool charged = dlg.exec() == QDialog::Accepted;
    on_pb_search_clicked();
    if (charged)
        m_result->setText(UiKit::okHtml(tr("Ticket %1 cobrado.").arg(ticketNum.toHtmlEscaped()))
                          + "<br>" + tr("El envío a AEAT se confirma en la barra de estado."));
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

    const QString date = ui->de_date_pickup->date().toString("dd-MM-yyyy");
    markTicketPickedUp(db, ticketNum, date);

    on_pb_search_clicked();
    m_result->setText(UiKit::okHtml(tr("Ticket %1 recogido el %2.").arg(ticketNum.toHtmlEscaped(), date)));
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
            m_result->setText(UiKit::warnHtml(tr("La prenda seleccionada no está pagada."))
                              + "<br>" + tr("Seleccione una prenda ya pagada del pago que quiera reimprimir."));
            return;
        }
        seq = sqlQueryModel->data(sqlQueryModel->index(
                  rowClickedCell, INGRESOS_COL_VERIFACTU_INVOICE_SEQ)).toInt();
    }
    const QString ticketNum = ui->le_nr_ticket->text();
    const bool printed = printFactura(ticketNum, seq);
    resetAllContents();
    m_result->setText(printed ? UiKit::okHtml(tr("Factura del ticket %1 impresa.").arg(ticketNum.toHtmlEscaped()))
                              : UiKit::warnHtml(tr("La impresión está desactivada en Configuración o la "
                                                   "impresora no respondió: no se ha impreso.")));
}

bool RecogPrendas::printFactura(const QString &ticketNum, int invoiceSeq)
{
    if (ticketNum.isEmpty())
        return false;
    qDebug() << "RecogPrendas::printFactura: ticket=" << ticketNum << "invoiceSeq=" << invoiceSeq;
    Imprimir *ui_impr = new Imprimir(db, this);
    ui_impr->isRecibo = false;
    ui_impr->isCompleteInvoice = false;
    ui_impr->verifactuIntegration = m_verifactuIntegration;
    ui_impr->invoiceSeq = invoiceSeq;
    ui_impr->le_n_ticket->setText(ticketNum);
    ui_impr->getTicketInfo();
    ui_impr->buildTicket(false, false);
    return AppSettings::instance()->enablePrinting() && ui_impr->printTicket();
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
    dlg->setObjectName("verifactuDialog");   // stable names for the e2e test bench
    UiKit::setUpDialog(dlg, tr("Verifactu - Ticket %1").arg(ticketNum), 460);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout *layout = new QVBoxLayout(dlg);
    QGroupBox *grpInfo = new QGroupBox(tr("Registro en AEAT"));
    QFormLayout *info = new QFormLayout(grpInfo);
    auto addRow = [&](const QString &label, const QString &value) {
        QLabel *lbl = new QLabel(value.isEmpty() ? QStringLiteral("-") : value.toHtmlEscaped());
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lbl->setWordWrap(true);
        info->addRow(label, lbl);
    };
    addRow(tr("Estado:"), state);
    addRow(tr("CSV:"), csv);
    addRow(tr("Fecha envío:"), timestamp);
    if (!error.isEmpty())
        addRow(tr("Error:"), error);
    if (!url.isEmpty()) {
        QLabel *urlLabel = new QLabel(QString("<a href='%1'>%2</a>").arg(url, tr("Abrir en la sede de AEAT")));
        urlLabel->setOpenExternalLinks(true);
        urlLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
        info->addRow(tr("Validación:"), urlLabel);
    }
    layout->addWidget(grpInfo);
    QHBoxLayout *actions = new QHBoxLayout();

    const VerifactuEstado stateEnum = verifactuEstadoFromString(state);
    const bool verifactuUsable = m_verifactuIntegration && m_verifactuIntegration->isConfigured();
    if (stateEnum == VerifactuEstado::Error && verifactuUsable) {
        QPushButton *btnRetry = UiKit::secondaryButton(tr("Reintentar envío a AEAT"), "btnRetry");
        connect(btnRetry, &QPushButton::clicked, this, [this, dlg, ticketNum, rowSeq]() {
            dlg->accept();
            retryVerifactuSubmit(ticketNum, rowSeq);
        });
        actions->addWidget(btnRetry);
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
        QPushButton *btnQuery = UiKit::secondaryButton(tr("Consultar en AEAT"), "btnQuery");
        btnQuery->setToolTip(alreadySettled
            ? "Consulta a AEAT los datos registrados de esta factura (solo informativo)."
            : "Comprueba si AEAT ya tiene esta factura y permite recuperar su CSV.");
        connect(btnQuery, &QPushButton::clicked, this,
                [this, dlg, ticketNum, rowSeq, alreadySettled]() {
            dlg->accept();
            queryAeatAndOfferReconcile(ticketNum, rowSeq, alreadySettled);
        });
        actions->addWidget(btnQuery);
    }
    actions->addStretch();
    layout->addLayout(actions);
    layout->addLayout(UiKit::closeRow(dlg));
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
        m_result->setText(UiKit::errorHtml(tr("No se puede reenviar el ticket %1: no consta como cobrado.")
                                               .arg(invoiceId.toHtmlEscaped())));
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
        m_result->setText(UiKit::errorHtml(tr("Verifactu no está configurado: no se puede consultar a AEAT.")));
        return;
    }
    const PendingVerifactuEvent ev = verifactuEventFor(db, ticketNum, seq);
    const QString invoiceId = verifactuInvoiceId(ticketNum, seq);
    if (ev.nRecibo.isEmpty()) {
        m_result->setText(UiKit::warnHtml(tr("El ticket %1 no consta como cobrado: no hay factura que consultar.")
                                              .arg(invoiceId.toHtmlEscaped())));
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
    dlg.setObjectName("aeatReconcileDialog");   // stable names for the e2e test bench
    UiKit::setUpDialog(&dlg, tr("Consulta AEAT - %1").arg(invoiceId), 600);
    auto *layout = new QVBoxLayout(&dlg);

    auto *summary = UiKit::introPanel(QString());
    summary->setObjectName("lblSummary");
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
        // Every submission ATTEMPT is stored, so a retried ticket returns several
        // records; the fields shown come from the accepted one, not the newest.
        const QString attempts = rec.recordCount > 1
            ? tr("<br><i>AEAT ha devuelto %1 registros para este número (los reenvíos "
                 "rechazados quedan guardados). Se muestran los datos del registro "
                 "aceptado.</i>").arg(rec.recordCount)
            : QString();
        QString verdict;
        if (!matches)
            verdict = tr("<span style='color:#b00'>Los datos NO coinciden con los del "
                         "ticket - no se puede actualizar automáticamente.</span>");
        else if (!rec.hasUsableCsv())
            verdict = tr("<span style='color:#b00'>Ninguno de los registros fue aceptado "
                         "por AEAT (no hay CSV que recuperar).</span>");
        else
            verdict = tr("Los datos coinciden con los del ticket.");
        summary->setText(tr("<b>AEAT tiene registrada esta factura.</b><br>%1%2")
                             .arg(verdict, attempts));
    }
    layout->addWidget(summary);

    auto *table = new QTableWidget(4, 3, &dlg);
    table->setObjectName("tableCompare");
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
    auto *btnRaw = UiKit::secondaryButton(tr("Ver respuesta completa"), "btnRaw");
    btnRaw->setCheckable(true);
    connect(btnRaw, &QPushButton::toggled, rawBox, &QWidget::setVisible);
    layout->addWidget(btnRaw);
    layout->addWidget(rawBox);

    auto *btnRow = new QHBoxLayout();
    auto *btnApply = UiKit::primaryButton(tr("Actualizar con los datos de AEAT"), "btnApply");
    btnApply->setEnabled(canAdopt);
    if (!canAdopt && localAlreadySettled)
        btnApply->setToolTip(tr("El ticket ya está registrado localmente; "
                                "esta consulta es solo informativa."));
    else if (!canAdopt && rec.found && matches)
        btnApply->setToolTip(tr("AEAT no ha devuelto el CSV de la factura."));
    auto *btnClose = UiKit::secondaryButton(tr("Cerrar"), "btnClose");
    btnRow->addWidget(btnClose);
    btnRow->addStretch();
    btnRow->addWidget(btnApply);
    layout->addLayout(btnRow);
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnApply, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted || !canAdopt)
        return;

    const int rows = reconcileVerifactuFromAeat(db, ticketNum, seq, rec.csv, rec.validationUrl);
    if (rows > 0) {
        on_pb_search_clicked();
        m_result->setText(UiKit::okHtml(tr("El ticket %1 se ha actualizado con el CSV de AEAT (%2).")
                                            .arg(invoiceId.toHtmlEscaped(), rec.csv.toHtmlEscaped())));
    } else {
        m_result->setText(UiKit::warnHtml(tr("No se ha actualizado ninguna fila del ticket %1.")
                                              .arg(invoiceId.toHtmlEscaped())));
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

    const int changed = updateTicketVerifactuFields(db, ticketNum, result, seq);

    // Refresh the table so the new estado is visible (only if user is still on this view)
    on_pb_search_clicked();
    if (rowClickedCell >= 0 && rowClickedCell < sqlQueryModel->rowCount()) {
        updateRowClickedToFields();
        isCellClicked = true;
    }

    // Nothing to reconcile either: AEAT already holds the invoice we have.
    if (changed <= 0) {
        statusBar()->showMessage(changed == 0
            ? tr("Respuesta de AEAT para el ticket %1 ignorada: la factura ya estaba registrada")
                  .arg(verifactuInvoiceId(ticketNum, seq))
            : tr("No se pudo guardar la respuesta de AEAT del ticket %1").arg(verifactuInvoiceId(ticketNum, seq)),
            15000);
        return;
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

void RecogPrendas::on_pb_separ_garm_clicked()
{
    if (!ui->le_size->text().isEmpty()) {
        qWarning() << "Garment split blocked: garment has a non-zero size";
        m_result->setText(UiKit::warnHtml(tr("Las prendas por m2 no se pueden separar.")));
        return;
    }
    if (ui->le_qty->text().toInt() <= 1) {
        qWarning() << "Garment split blocked: quantity is not greater than 1";
        m_result->setText(UiKit::warnHtml(tr("La prenda seleccionada es una sola unidad: no hay nada que separar.")));
        return;
    }
    const int number = ui->sb_separ->value();
    const QString ticketNum = ui->le_nr_ticket->text();
    updateDb(SEPARATE_GARM, number);
    if (!m_result->text().contains(QLatin1String("bloqueado")))
        m_result->setText(UiKit::okHtml(tr("%1 prenda(s) separadas en una fila aparte del ticket %2.")
                                            .arg(number).arg(ticketNum.toHtmlEscaped())));
}
