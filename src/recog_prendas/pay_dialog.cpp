#include "pay_dialog.h"

#include "appsettings.h"
#include "imprimir.h"
#include "sql_lite.h"
#include "verifactuintegration.h"

#include <QCheckBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
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
    setWindowTitle(tr("Cobrar"));
    setModal(true);
    resize(720, 480);

    auto *root = new QVBoxLayout(this);

    m_lblHeader = new QLabel(this);
    m_lblHeader->setStyleSheet("font-size: 14px; font-weight: bold;");
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
    m_lblTotal->setStyleSheet("font-size: 14px; font-weight: bold;");
    root->addWidget(m_lblTotal);

    auto *form = new QFormLayout;
    m_dePago = new QDateEdit(QDate::currentDate(), this);
    m_dePago->setCalendarPopup(true);
    m_dePago->setDisplayFormat("dd-MM-yyyy");
    form->addRow(tr("Fecha de pago:"), m_dePago);
    root->addLayout(form);

    m_lblStatus = new QLabel(this);
    m_lblStatus->setStyleSheet("color: #555;");
    root->addWidget(m_lblStatus);

    auto *btns = new QHBoxLayout;
    btns->addStretch(1);
    m_btnCobrar = new QPushButton(tr("Cobrar"), this);
    m_btnCobrar->setDefault(true);
    m_btnCancel = new QPushButton(tr("Cancelar"), this);
    btns->addWidget(m_btnCobrar);
    btns->addWidget(m_btnCancel);
    root->addLayout(btns);

    connect(m_btnCobrar, &QPushButton::clicked, this, &PayDialog::onCobrarClicked);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

bool PayDialog::loadTicket(const QString &ticketNum)
{
    m_ticketNum = ticketNum;
    m_table->setRowCount(0);

    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT cliente, fecha_recepcion, cantidad, prenda, size, servicio, "
              "importe, observaciones, pagado, hash, edit_lock "
              "FROM ingresos WHERE n_recibo = :n ORDER BY rowid");
    q.bindValue(":n", ticketNum);
    if (!q.exec()) {
        qWarning() << "PayDialog::loadTicket: SELECT failed for ticket" << ticketNum
                   << "-" << q.lastError().text();
        db.close();
        return false;
    }

    int unpaidCount  = 0;
    int lockedCount  = 0;
    while (q.next()) {
        if (m_cliente.isEmpty()) {
            m_cliente        = q.value(0).toString();
            m_fechaRecepcion = QDate::fromString(q.value(1).toString(), "dd-MM-yyyy");
        }
        const QString pagado   = q.value(8).toString();
        const bool    editLock = q.value(10).toBool();
        if (pagado == "SI") continue;
        // edit_lock=1 means the row's quarter has been closed by contabilidad
        // (`updateLockForMonth`). RecogPrendas::updateDb blocks PAY_YES under
        // the same condition; skip here so the locked row never enters the
        // selectable set. Unpaid rows usually have edit_lock=0 (the lock keys
        // on fecha_pago LIKE), so this is mostly a defensive filter.
        if (editLock) { ++lockedCount; continue; }

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
        auto *imp = new QTableWidgetItem(QString::number(q.value(6).toDouble(), 'f', 2));
        imp->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(r, COL_AMOUNT,  imp);
        m_table->setItem(r, COL_OBS,     new QTableWidgetItem(q.value(7).toString()));
        m_table->setItem(r, COL_HASH,    new QTableWidgetItem(q.value(9).toString()));
        ++unpaidCount;
    }
    db.close();

    if (unpaidCount == 0) {
        qDebug() << "PayDialog::loadTicket: ticket" << ticketNum << "has no unpaid rows";
        return false;
    }

    QString headerText = tr("Ticket %1 - %2 - Recepción %3")
                         .arg(ticketNum, m_cliente,
                              m_fechaRecepcion.toString("dd-MM-yyyy"));
    if (lockedCount > 0)
        headerText += tr("  (%1 prenda(s) omitida(s) por bloqueo de contabilidad)").arg(lockedCount);
    m_lblHeader->setText(headerText);
    connect(m_table, &QTableWidget::itemChanged,
            this, [this](QTableWidgetItem *it) {
        if (it && it->column() == COL_CHECK) recomputeTotal();
    });
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
            total += m_table->item(r, COL_AMOUNT)->text().toDouble();
            ++selected;
        }
    }
    m_lblTotal->setText(tr("Total seleccionado (%1 prendas): %2 €")
                        .arg(selected).arg(total, 0, 'f', 2));
    m_btnCobrar->setEnabled(selected > 0);
}

void PayDialog::onSelectionChanged()
{
    recomputeTotal();
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
    QStringList hashes;
    double total = 0.0;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto *chk = m_table->item(r, COL_CHECK);
        if (chk && chk->checkState() == Qt::Checked) {
            hashes << m_table->item(r, COL_HASH)->text();
            total += m_table->item(r, COL_AMOUNT)->text().toDouble();
        }
    }
    if (hashes.isEmpty()) {
        QMessageBox::information(this, tr("Sin selección"),
                                 tr("Selecciona al menos una prenda para cobrar."));
        return;
    }

    const QDate fechaPago = m_dePago->date();
    if (readLockForMonthAndYear(db, "ingresos", fechaPago.month(), fechaPago.year()) == 1) {
        QMessageBox::warning(this, tr("Trimestre bloqueado"),
                             tr("La fecha de pago pertenece a un trimestre que se encuentra bloqueado por la contabilidad."));
        return;
    }

    // Race guard: edit_lock could have flipped 0->1 between loadTicket() and
    // now (accountant closed the quarter in another window). Submitting to
    // AEAT for an amount we can't actually mark paid locally would
    // desynchronize the books from AEAT - abort before any AEAT call.
    {
        db.open();
        QSqlQuery lockQ(db);
        QString placeholders;
        for (int i = 0; i < hashes.size(); ++i) {
            if (i) placeholders += ',';
            placeholders += ':' + QString("h%1").arg(i);
        }
        lockQ.prepare("SELECT COUNT(*) FROM ingresos WHERE n_recibo = :n "
                      "AND edit_lock = 1 AND hash IN (" + placeholders + ")");
        lockQ.bindValue(":n", m_ticketNum);
        for (int i = 0; i < hashes.size(); ++i)
            lockQ.bindValue(QString(":h%1").arg(i), hashes[i]);
        int locked = 0;
        if (lockQ.exec() && lockQ.first())
            locked = lockQ.value(0).toInt();
        db.close();
        if (locked > 0) {
            QMessageBox::warning(this, tr("Trimestre bloqueado"),
                                 tr("Una o más prendas seleccionadas pertenecen a un trimestre "
                                    "que se ha bloqueado durante la operación. Cierra y vuelve "
                                    "a abrir el cobro para refrescar el estado del ticket."));
            return;
        }
    }

    const int seq = nextVerifactuInvoiceSeq(db, m_ticketNum);
    const QString invoiceId = QString("%1-%2").arg(m_ticketNum).arg(seq);

    qDebug() << "PayDialog::onCobrarClicked: ticket=" << m_ticketNum
             << "seq=" << seq << "invoiceId=" << invoiceId
             << "rows=" << hashes.size() << "total=" << total;

    m_pendingSeq        = seq;
    m_pendingHashes     = hashes;
    m_pendingFechaPago  = fechaPago;

    if (!m_verifactu || !m_verifactu->isConfigured()) {
        // Verifactu disabled: mark rows paid without AEAT metadata, no print QR.
        qDebug() << "PayDialog: Verifactu not configured, persisting payment without AEAT";
        VerifactuResult res;
        res.status = VerifactuResult::INVALID_CONFIG;
        persistPayment(seq, res);
        printPartialFactura(seq, QPixmap());
        accept();
        return;
    }

    const double ivaRate = AppSettings::instance()->ivaRate();
    const double taxBase = total / (1.0 + ivaRate / 100.0);

    connect(m_verifactu, &VerifactuIntegration::requestFinished,
            this, &PayDialog::onVerifactuRequestFinished, Qt::UniqueConnection);
    m_pendingReqId = m_verifactu->submitSimplifiedInvoiceAsync(
        invoiceId, fechaPago, taxBase, ivaRate, tr("Servicios de lavandería"));

    if (m_pendingReqId.isEmpty()) {
        QMessageBox::warning(this, tr("Verifactu"),
                             tr("No se pudo enviar a AEAT:\n%1\n\nSe registrará el pago localmente sin envío.")
                                 .arg(m_verifactu->getLastError()));
        VerifactuResult res;
        res.status = VerifactuResult::INVALID_CONFIG;
        persistPayment(seq, res);
        printPartialFactura(seq, QPixmap());
        accept();
        return;
    }

    setFormEnabled(false);
    m_lblStatus->setText(tr("Enviando %1 a AEAT...").arg(invoiceId));

    // Bounded 5s wait for the AEAT reply. onVerifactuRequestFinished will
    // accept() us once it lands; if it doesn't, fall back to ERROR locally.
    QTimer::singleShot(5000, this, [this]() {
        if (m_pendingReqId.isEmpty()) return; // already handled
        qWarning() << "PayDialog: AEAT timeout (5s) for" << m_pendingReqId;
        m_pendingReqId.clear();
        VerifactuResult res;
        res.status = VerifactuResult::NETWORK_ERROR;
        res.errorDescription = tr("Tiempo de espera agotado (5 s)");
        persistPayment(m_pendingSeq, res);
        printPartialFactura(m_pendingSeq, QPixmap());
        accept();
    });
}

void PayDialog::onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result)
{
    if (requestId != m_pendingReqId) return; // not ours
    m_pendingReqId.clear();
    persistPayment(m_pendingSeq, result);
    printPartialFactura(m_pendingSeq, result.qrCode);
    accept();
}

void PayDialog::persistPayment(int seq, const VerifactuResult &result)
{
    db.open();
    QSqlQuery q(db);
    // AND edit_lock = 0: defensive against a row whose quarter was closed
    // between loadTicket and persistPayment (very narrow race window, but
    // free to guard - keeps accounting lock authoritative).
    q.prepare("UPDATE ingresos SET pagado = 'SI', fecha_pago = :fp, "
              "verifactu_invoice_seq = :seq "
              "WHERE n_recibo = :n AND hash = :h AND edit_lock = 0");
    for (const QString &h : m_pendingHashes) {
        q.bindValue(":fp",  m_pendingFechaPago.toString("dd-MM-yyyy"));
        q.bindValue(":seq", seq);
        q.bindValue(":n",   m_ticketNum);
        q.bindValue(":h",   h);
        if (!q.exec())
            qWarning() << "PayDialog::persistPayment: UPDATE failed for hash" << h
                       << "-" << q.lastError().text();
    }
    db.close();

    if (result.status == VerifactuResult::INVALID_CONFIG) {
        qDebug() << "PayDialog::persistPayment: Verifactu disabled, leaving verifactu_* empty";
        return;
    }
    // updateTicketVerifactuFields scopes WHERE n_recibo=? AND verifactu_invoice_seq=?
    // which is exactly the rows we just stamped above.
    updateTicketVerifactuFields(db, m_ticketNum, result, seq);
}

void PayDialog::printPartialFactura(int seq, const QPixmap &qrCode)
{
    auto *ui_impr = new Imprimir(db, this);
    ui_impr->isRecibo            = qrCode.isNull(); // recibo (no QR) when AEAT didn't reply
    ui_impr->isCompleteInvoice   = false;
    ui_impr->verifactuIntegration = nullptr;
    ui_impr->qrCode              = qrCode;
    ui_impr->invoiceSeq          = seq;
    ui_impr->le_n_ticket->setText(m_ticketNum);
    ui_impr->getTicketInfo();
    ui_impr->createTicketExcel(/*copyForClient=*/true, /*addPayedInfo=*/false);
    if (AppSettings::instance()->enablePrinting()) {
        ui_impr->printTicket();
        ui_impr->createTicketExcel(/*copyForClient=*/false, /*addPayedInfo=*/false);
        ui_impr->printTicket();
    }
}
