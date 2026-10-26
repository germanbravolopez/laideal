#include "pay_dialog.h"

#include "appsettings.h"
#include "imprimir.h"
#include <QScopeGuard>
#include "sql_lite.h"
#include "uikit.h"
#include "verifactuintegration.h"

#include <QDateEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// Columns of the dialog's QTableWidget (different from ingresos' columns).
constexpr int COL_CHECK    = 0;
constexpr int COL_QTY      = 1;
constexpr int COL_GARMENT  = 2;
constexpr int COL_SIZE     = 3;
constexpr int COL_SERVICE  = 4;
constexpr int COL_AMOUNT   = 5;
constexpr int COL_OBS      = 6;
// Hidden column carrying the row's ingresos.hash so we can address the DB row.
constexpr int COL_HASH     = 7;
} // namespace

PayDialog::PayDialog(const QSqlDatabase &database, QWidget *parent)
    : QDialog(parent), db(database)
{
    buildUi();
}

void PayDialog::buildUi()
{
    UiKit::setUpDialog(this, tr("Cobrar"), 720);
    setModal(true);
    resize(720, 520);

    auto *root = new QVBoxLayout(this);

    m_lblHeader = UiKit::introPanel(QString());   // ticket, client, reception (loadTicket)
    root->addWidget(m_lblHeader);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels(
        { "", tr("Cant."), tr("Prenda"), tr("Talla"), tr("Serv."), tr("Importe"), tr("Obs."), "" });
    m_table->setColumnHidden(COL_HASH, true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(COL_CHECK, 32);
    m_table->setColumnWidth(COL_QTY,   50);
    m_table->setColumnWidth(COL_GARMENT, 220);
    m_table->setColumnWidth(COL_SIZE,  60);
    m_table->setColumnWidth(COL_SERVICE, 60);
    m_table->setColumnWidth(COL_AMOUNT, 80);
    root->addWidget(m_table);

    m_lblTotal = new QLabel(this);
    m_lblTotal->setAlignment(Qt::AlignRight);
    QFont totalFont = m_lblTotal->font();
    totalFont.setPointSizeF(totalFont.pointSizeF() + 2);
    totalFont.setBold(true);
    m_lblTotal->setFont(totalFont);
    root->addWidget(m_lblTotal);

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    m_dePago = UiKit::dateEdit(QDate::currentDate(), "dePago");
    form->addRow(tr("Fecha de pago:"), m_dePago);
    root->addLayout(form);

    auto *btns = new QHBoxLayout;
    m_btnCancel = UiKit::secondaryButton(tr("Cancelar"), "btnCancel");
    btns->addWidget(m_btnCancel);
    btns->addStretch(1);
    m_btnCobrar = UiKit::primaryButton(tr("Cobrar"), "btnCobrar");
    btns->addWidget(m_btnCobrar);
    root->addLayout(btns);

    m_lblStatus = new UiKit::ResultPanel(tr("Marque las prendas que se cobran y pulse \"Cobrar\"."), this);
    m_lblStatus->setMinimumHeight(40);
    root->addWidget(m_lblStatus);

    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (it && it->column() == COL_CHECK) recomputeTotal();
    });
    connect(m_btnCobrar, &QPushButton::clicked, this, &PayDialog::onCobrarClicked);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

bool PayDialog::loadTicket(const QString &ticketNum)
{
    m_ticketNum = ticketNum;
    m_cliente.clear();
    const QSignalBlocker loading(m_table);   // recomputeTotal() runs once, after the rows are in
    m_table->setRowCount(0);

    int unpaidCount = 0;
    {
        ConnectionScope scope(db);
        QSqlQuery q(db);
        q.prepare("SELECT cliente, fecha_recepcion, cantidad, prenda, size, servicio, "
                  "importe, observaciones, pagado, hash, estado "
                  "FROM ingresos WHERE n_recibo = :n ORDER BY rowid");
        q.bindValue(":n", ticketNum);
        if (!q.exec()) {
            qWarning() << "PayDialog::loadTicket: SELECT failed for ticket" << ticketNum
                       << "-" << q.lastError().text();
            return false;
        }

        while (q.next()) {
            if (m_cliente.isEmpty()) {
                m_cliente        = q.value(0).toString();
                m_fechaRecepcion = QDate::fromString(q.value(1).toString(), "dd-MM-yyyy");
            }
            const QString pagado   = q.value(8).toString();
            const QString estado   = q.value(10).toString();
            if (pagado == "SI") continue;
            // A locally voided garment (estado "Anulado") is never chargeable - it
            // must not enter the payable set or it could be submitted to AEAT.
            if (estado == QLatin1String(INGRESOS_ESTADO_ANULADO)) continue;

            const int r = m_table->rowCount();
            m_table->insertRow(r);

            auto *cell = new QTableWidgetItem;
            cell->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            cell->setCheckState(Qt::Checked);
            m_table->setItem(r, COL_CHECK, cell);

            m_table->setItem(r, COL_QTY,     new QTableWidgetItem(q.value(2).toString()));
            m_table->setItem(r, COL_GARMENT, new QTableWidgetItem(q.value(3).toString()));
            m_table->setItem(r, COL_SIZE,    new QTableWidgetItem(q.value(4).toString()));
            m_table->setItem(r, COL_SERVICE, new QTableWidgetItem(q.value(5).toString()));
            auto *imp = new QTableWidgetItem(displayMoney(q.value(6).toString()));
            imp->setData(Qt::UserRole, moneyText(q.value(6).toString()).toDouble());   // the shown text has a comma
            imp->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(r, COL_AMOUNT,  imp);
            m_table->setItem(r, COL_OBS,     new QTableWidgetItem(q.value(7).toString()));
            m_table->setItem(r, COL_HASH,    new QTableWidgetItem(q.value(9).toString()));
            ++unpaidCount;
        }
    }

    if (unpaidCount == 0) {
        qDebug() << "PayDialog::loadTicket: ticket" << ticketNum << "has no unpaid rows";
        return false;
    }

    QString headerText = tr("Ticket %1 - %2 - Recepción %3")
                         .arg(ticketNum, m_cliente,
                              m_fechaRecepcion.toString("dd-MM-yyyy"));
    m_lblHeader->setText(headerText);
    // The payment date is the invoice date AEAT gets: not in the future, not before reception.
    m_dePago->setMaximumDate(QDate::currentDate());
    if (m_fechaRecepcion.isValid() && m_fechaRecepcion <= QDate::currentDate())
        m_dePago->setMinimumDate(m_fechaRecepcion);
    recomputeTotal();
    return true;
}

void PayDialog::recomputeTotal()
{
    double total = 0.0;
    int selected = 0;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto *chk = m_table->item(r, COL_CHECK);
        if (chk && chk->checkState() == Qt::Checked) {
            total += m_table->item(r, COL_AMOUNT)->data(Qt::UserRole).toDouble();
            ++selected;
        }
    }
    m_lblTotal->setText(tr("Total seleccionado (%1 prendas): %2 €")
                        .arg(selected).arg(displayMoney(total)));
    m_btnCobrar->setEnabled(selected > 0);
}

void PayDialog::setFormEnabled(bool enabled)
{
    m_table->setEnabled(enabled);
    m_dePago->setEnabled(enabled);
    m_btnCobrar->setEnabled(enabled);
    m_btnCancel->setEnabled(enabled);
}

void PayDialog::onCobrarClicked()
{
    if (!m_pendingReqId.isEmpty())   // waiting for AEAT: a second press would charge another seq
        return;
    QStringList hashes;
    QHash<QString, QString> amounts;   // hash -> the amount shown, in cents
    double total = 0.0;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto *chk = m_table->item(r, COL_CHECK);
        if (chk && chk->checkState() == Qt::Checked) {
            // Once charged its amount is frozen: measure it first.
            if (garmentUnmeasured(m_table->item(r, COL_GARMENT)->text(), m_table->item(r, COL_SIZE)->text())) {
                m_lblStatus->setText(UiKit::errorHtml(tr("La prenda \"%1\" se cobra por m2 y no tiene tamaño.")
                                                          .arg(m_table->item(r, COL_GARMENT)->text().toHtmlEscaped()))
                                     + "<br>" + tr("Introduzca su tamaño en Recogida o desmárquela para cobrar el resto."));
                return;
            }
            hashes << m_table->item(r, COL_HASH)->text();   // the charged set: m_pendingAmounts
            amounts.insert(m_table->item(r, COL_HASH)->text(), m_table->item(r, COL_AMOUNT)->text());
            total += m_table->item(r, COL_AMOUNT)->data(Qt::UserRole).toDouble();
        }
    }
    if (hashes.isEmpty()) {
        m_lblStatus->setText(UiKit::warnHtml(tr("Selecciona al menos una prenda para cobrar.")));
        return;
    }
    if (total <= 0.0) {
        m_lblStatus->setText(UiKit::warnHtml(tr("El total a cobrar es 0,00: no hay nada que facturar."))
                             + "<br>" + tr("Revise el importe de las prendas en Recogida."));
        return;
    }
    // Every payment is an invoice: never booked without its Verifactu record.
    if (!m_verifactu) {
        m_lblStatus->setText(UiKit::errorHtml(tr("Verifactu no está disponible: no se puede cobrar.")));
        return;
    }
    if (const QString blocked = m_verifactu->invoicingBlockedReason(); !blocked.isEmpty()) {
        m_lblStatus->setText(UiKit::errorHtml(blocked));
        return;
    }

    const QDate fechaPago = m_dePago->date();
    // The whole quarter: a month without rows inside a closed quarter is closed too.
    if (quarterIsClosed(db, fechaPago)) {
        m_lblStatus->setText(UiKit::errorHtml(tr("Trimestre bloqueado."))
                             + "<br>" + tr("La fecha de pago pertenece a un trimestre cerrado por la contabilidad."));
        return;
    }

    const int seq = nextVerifactuInvoiceSeq(db, m_ticketNum, m_verifactu->environmentName());
    const QString invoiceId = verifactuInvoiceId(m_ticketNum, seq);

    qDebug() << "PayDialog::onCobrarClicked: ticket=" << m_ticketNum
             << "seq=" << seq << "invoiceId=" << invoiceId
             << "rows=" << hashes.size() << "total=" << total;

    m_pendingSeq        = seq;
    m_pendingHashes     = hashes;
    m_pendingAmounts    = amounts;
    m_pendingFechaPago  = fechaPago;

    // Stored as paid + PENDIENTE before anything is sent, like MainWindow::saveTicket: a
    // crash during the AEAT round trip leaves a payment the start-up recovery resends,
    // never an invoice at AEAT for a ticket the database shows unpaid.
    if (!storePayment(seq)) {
        if (quarterIsClosed(db, fechaPago)) {
            m_lblStatus->setText(UiKit::errorHtml(tr("Trimestre bloqueado."))
                                 + "<br>" + tr("La contabilidad ha cerrado el trimestre de la fecha de pago: no se ha "
                                               "guardado ni enviado nada."));
            return;
        }
        m_lblStatus->setText(UiKit::errorHtml(tr("No se ha podido guardar el cobro: no se ha enviado nada a AEAT."))
                             + "<br>" + tr("Alguna prenda se ha cobrado desde otra ventana o la base de datos no "
                                           "responde. Cierre y vuelva a abrir Cobrar."));
        return;
    }

    const double ivaRate = AppSettings::instance()->ivaRate();
    const double taxBase = total / (1.0 + ivaRate / 100.0);

    connect(m_verifactu, &VerifactuIntegration::requestFinished,
            this, &PayDialog::onVerifactuRequestFinished, Qt::UniqueConnection);
    m_pendingReqId = m_verifactu->submitSimplifiedInvoiceAsync(
        invoiceId, fechaPago, taxBase, ivaRate, tr("Servicios de lavandería"));

    if (m_pendingReqId.isEmpty()) {
        // Nothing left: the payment stays PENDIENTE and the start-up recovery resends it.
        m_notSentReason = m_verifactu->getLastError();
        qWarning() << "PayDialog: invoice" << invoiceId << "stored PENDIENTE, not sent -" << m_notSentReason;
        printPartialFactura(seq, QPixmap());
        accept();
        return;
    }

    // The direct client's QR needs no answer from AEAT (which may make the next
    // submission wait up to TiempoEsperaEnvio): book the payment as pending, print
    // the factura now and leave the answer to RecogPrendas, as after the timeout.
    const QPixmap qrNow = m_verifactu->qrBeforeReply(invoiceId, fechaPago, taxBase, ivaRate);
    if (!qrNow.isNull()) {
        qDebug() << "PayDialog: factura printed before AEAT answers" << m_pendingReqId;
        emit submitAdopted(m_pendingReqId, m_ticketNum, m_pendingSeq, true);
        m_pendingReqId.clear();
        printPartialFactura(m_pendingSeq, qrNow, true);
        accept();
        return;
    }

    setFormEnabled(false);
    m_lblStatus->showInfo(tr("Enviando %1 a AEAT...").arg(invoiceId));

    // Bounded 5s wait for the AEAT reply. onVerifactuRequestFinished will
    // accept() us once it lands; if it doesn't, the rows stay PENDIENTE (the
    // submission may still have reached AEAT).
    QTimer::singleShot(5000, this, [this]() {
        if (m_pendingReqId.isEmpty()) return; // already handled
        qWarning() << "PayDialog: AEAT timeout (5s) for" << m_pendingReqId;
        // Hand the still-in-flight request to a longer-lived owner BEFORE closing.
        // This dialog is stack-allocated by RecogPrendas and dies with exec(), so
        // without the handoff a reply arriving at, say, 7 s (the transport timeout
        // is 10 s) was dropped on the floor - including a SUCCESS carrying the CSV,
        // which left the row falsely PENDIENTE and its later retry answering
        // "already exists".
        emit submitAdopted(m_pendingReqId, m_ticketNum, m_pendingSeq, false);
        m_pendingReqId.clear();
        printPartialFactura(m_pendingSeq, QPixmap());
        accept();
    });
}

void PayDialog::done(int result)
{
    if (!m_pendingReqId.isEmpty()) {
        m_lblStatus->setText(UiKit::warnHtml(tr("Esperando la respuesta de la AEAT: la ventana se cerrará o se podrá cerrar en cuanto responda.")));
        return;
    }
    QDialog::done(result);
}

void PayDialog::onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result)
{
    if (requestId != m_pendingReqId) return; // not ours
    m_pendingReqId.clear();
    applyReply(m_pendingSeq, result);
    printPartialFactura(m_pendingSeq, result.qrCode, result.isSuccess() && !result.qrCode.isNull());
    accept();
}

bool PayDialog::storePayment(int seq)
{
    return storeChargedGarments(db, m_ticketNum, m_pendingAmounts, m_pendingFechaPago, seq,
                                verifactuInvoiceId(m_ticketNum, seq));
}

void PayDialog::applyReply(int seq, const VerifactuResult &result)
{
    // A timeout, a transport failure or a reply-time configuration error is an unknown
    // outcome: the rows stay PENDIENTE for the recovery. A definitive answer (accepted
    // or refused) is written by updateTicketVerifactuFields, scoped to this seq.
    if (result.status == VerifactuResult::NETWORK_ERROR || result.status == VerifactuResult::PENDING
        || result.status == VerifactuResult::INVALID_CONFIG) {
        qWarning() << "PayDialog::applyReply:" << verifactuInvoiceId(m_ticketNum, seq)
                   << "left PENDIENTE - status" << int(result.status) << result.errorDescription;
        return;
    }
    updateTicketVerifactuFields(db, m_ticketNum, result, seq);
}

void PayDialog::printPartialFactura(int seq, const QPixmap &qrCode, bool verifactuIssued)
{
    auto *ui_impr = new Imprimir(db, this);
    const auto release = qScopeGuard([ui_impr] { ui_impr->deleteLater(); });
    ui_impr->isRecibo            = qrCode.isNull(); // recibo (no QR) when AEAT didn't reply
    ui_impr->isCompleteInvoice   = false;
    ui_impr->verifactuIntegration = nullptr;
    ui_impr->qrCode              = qrCode;
    ui_impr->verifactuIssued     = verifactuIssued;
    ui_impr->invoiceSeq          = seq;
    ui_impr->le_n_ticket->setText(m_ticketNum);
    ui_impr->getTicketInfo();
    // Auto-print a single factura (the customer copy) after Cobrar - no second
    // (business) copy. Reprint from RecogPrendas if another copy is needed.
    // The rows are already pagado=SI here, so the recibo fallback (AEAT did not
    // reply) must show IMPORTE PAGADO - otherwise the customer walks out with a
    // ticket that shows neither the payment nor a QR. A factura implies payment
    // and omits the line, as every other print path does.
    ui_impr->buildTicket(/*copyForClient=*/true, /*addPayedInfo=*/ui_impr->isRecibo);
    if (AppSettings::instance()->enablePrinting())
        ui_impr->printTicket();
}
