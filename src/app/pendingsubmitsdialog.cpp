#include "pendingsubmitsdialog.h"

#include "appsettings.h"
#include "sql_lite.h"
#include "uikit.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int COL_N_RECIBO = 0;
constexpr int COL_FECHA    = 1;
constexpr int COL_CLIENT   = 2;
constexpr int COL_IMPORTE  = 3;
constexpr int COL_ACTIONS  = 4;
constexpr int COL_COUNT    = 5;
} // namespace

PendingSubmitsDialog::PendingSubmitsDialog(const QSqlDatabase &database, QWidget *parent)
    : QDialog(parent), m_db(database)
{
    UiKit::setUpDialog(this, tr("Envíos Verifactu pendientes"), 720);
    setMinimumHeight(400);
    buildUi();
}

void PendingSubmitsDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    layout->addWidget(UiKit::introPanel(tr(
        "Estos tickets se enviaron a AEAT en una sesión anterior y no llegó la respuesta "
        "(estado PENDIENTE). Decida para cada uno:<br>"
        "<b>Reintentar</b>: vuelve a enviarlo a AEAT (si AEAT ya lo tenía, se consulta y se "
        "recupera su CSV).<br>"
        "<b>Error</b>: lo deja en ERROR para revisarlo en la sede electrónica de AEAT.<br>"
        "<b>Posponer</b>: no hace nada; volverá a aparecer en el próximo arranque.")));

    m_table = new QTableWidget(this);
    m_table->setObjectName("table");   // stable names for the e2e test bench
    m_table->setColumnCount(COL_COUNT);
    m_table->setHorizontalHeaderLabels(
        { tr("Nº recibo"), tr("Fecha"), tr("Cliente"), tr("Importe"), tr("Acciones") });
    m_table->horizontalHeader()->setSectionResizeMode(COL_CLIENT, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_ACTIONS, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_table, 1);

    m_lblResult = new UiKit::ResultPanel();
    m_lblResult->setMinimumHeight(40);
    layout->addWidget(m_lblResult);
    auto *closeRow = new QHBoxLayout();
    closeRow->addStretch();
    auto *btnClose = UiKit::secondaryButton(tr("Cerrar"), "btnClose");
    closeRow->addWidget(btnClose);
    layout->addLayout(closeRow);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
}

bool PendingSubmitsDialog::loadPending()
{
    m_entries.clear();

    // Two gates protect against surfacing pre-Verifactu legacy tickets:
    //  - operator toggle (verifactu.pending_recovery_enabled, default true)
    //  - floor date (verifactu.pending_recovery_floor_date, ISO yyyy-MM-dd,
    //    default = the planned PROD cutover). Rows with fecha_recepcion
    //    strictly before this date never count as pending.
    AppSettings *s = AppSettings::instance();
    if (!s->verifactuPendingRecoveryEnabled())
        return false;
    const QString floorIso = verifactuRecoveryFloorIso(m_db, s->verifactuPendingRecoveryFloorDate(),
                                                       s->verifactuDirectAeat() && s->aeatDirectProduction());

    // One entry per (n_recibo, verifactu_invoice_seq) whose any row is still
    // PENDIENTE on/after the recovery floor. Grouping by seq surfaces partial-
    // pay events (seq>0, InvoiceID "<n_recibo>-<seq>") as well as save-time ones
    // (seq=0), each with its own SUM(importe) - so a retry re-submits the right
    // amount under the right InvoiceID. The duplicate-InvoiceID warning still
    // covers a slow-but-already-registered AEAT submission.
    const QVector<PendingVerifactuEvent> events = pendingVerifactuEvents(m_db, floorIso);
    for (const PendingVerifactuEvent &ev : events) {
        if (inFlight && inFlight(verifactuInvoiceId(ev.nRecibo, ev.seq)))
            continue;                                                   // being sent right now
        Entry e;
        e.ticketNum  = ev.nRecibo;
        e.seq        = ev.seq;
        e.fechaPago  = parseDbDate(ev.fechaPago);
        e.client     = ev.cliente;
        e.importe    = ev.importe;
        m_entries.append(e);
    }

    if (m_entries.isEmpty())
        return false;

    m_table->setRowCount(m_entries.size());
    for (int r = 0; r < m_entries.size(); ++r)
        populateRow(r, m_entries[r]);
    return true;
}

void PendingSubmitsDialog::populateRow(int row, const Entry &e)
{
    // Show the AEAT InvoiceID (bare n_recibo for seq 0, "<n>-<seq>" for a
    // partial-pay event) so the operator can tell apart multiple events of the
    // same ticket.
    m_table->setItem(row, COL_N_RECIBO,
        new QTableWidgetItem(verifactuInvoiceId(e.ticketNum, e.seq)));
    m_table->setItem(row, COL_FECHA,
        new QTableWidgetItem(dbDateText(e.fechaPago)));
    m_table->setItem(row, COL_CLIENT, new QTableWidgetItem(e.client));
    m_table->setItem(row, COL_IMPORTE,
        new QTableWidgetItem(displayEuro(e.importe)));

    m_table->setCellWidget(row, COL_ACTIONS, actionCell(e));
}

QWidget *PendingSubmitsDialog::actionCell(const Entry &e)
{
    auto *cell = new QWidget(m_table);
    auto *h = new QHBoxLayout(cell);
    h->setContentsMargins(2, 0, 2, 0);
    h->setSpacing(4);

    auto *btnRetry = new QPushButton(tr("Reintentar"), cell);
    auto *btnError = new QPushButton(tr("Error"), cell);
    auto *btnPost  = new QPushButton(tr("Posponer"), cell);
    h->addWidget(btnRetry);
    h->addWidget(btnError);
    h->addWidget(btnPost);

    // Rows move up when an earlier one is removed: a button finds its row at click time.
    const QString ticketNum = e.ticketNum;
    const int seq = e.seq;
    connect(btnRetry, &QPushButton::clicked, this, [this, ticketNum, seq]() { onRetryClicked(rowOf(ticketNum, seq)); });
    connect(btnError, &QPushButton::clicked, this, [this, ticketNum, seq]() { onMarkErrorClicked(rowOf(ticketNum, seq)); });
    connect(btnPost,  &QPushButton::clicked, this, [this, ticketNum, seq]() { onPostponeClicked(rowOf(ticketNum, seq)); });
    return cell;
}

int PendingSubmitsDialog::rowOf(const QString &ticketNum, int seq) const
{
    for (int r = 0; r < m_entries.size(); ++r)
        if (m_entries[r].ticketNum == ticketNum && m_entries[r].seq == seq)
            return r;
    return -1;
}

void PendingSubmitsDialog::onRetryClicked(int row)
{
    if (row < 0 || row >= m_entries.size()) return;
    const Entry e = m_entries[row];
    if (!e.fechaPago.isValid()) {
        m_lblResult->setText(UiKit::errorHtml(tr("El ticket %1 no tiene fecha de pago.").arg(e.ticketNum.toHtmlEscaped()))
                             + "<br>" + tr("No se puede reenviar a AEAT: un ticket sin cobrar no tiene "
                                           "factura que recuperar."));
        return;
    }
    qDebug() << "PendingSubmitsDialog: retry requested for ticket" << e.ticketNum
             << "seq=" << e.seq
             << "date=" << e.fechaPago.toString(Qt::ISODate)
             << "total=" << e.importe;
    const QString notSent = retry ? retry(e.ticketNum, e.seq, e.fechaPago, e.importe)
                                  : tr("No se puede reenviar desde aquí.");
    if (!notSent.isEmpty()) {
        m_lblResult->setText(UiKit::errorHtml(tr("El ticket %1 no se ha reenviado.").arg(verifactuInvoiceId(e.ticketNum, e.seq).toHtmlEscaped()))
                             + "<br>" + notSent.toHtmlEscaped());
        return;                                                            // stays listed
    }
    m_lblResult->setText(UiKit::okHtml(tr("Ticket %1 reenviado a AEAT.").arg(verifactuInvoiceId(e.ticketNum, e.seq).toHtmlEscaped()))
                         + "<br>" + tr("La respuesta aparece en la barra de estado de la ventana principal."));
    // Drop the row from the table; the async reply will patch the DB. If it
    // fails the row reverts to ERROR and the operator can revisit via the
    // normal RecogPrendas retry button.
    removeRowAt(row);
}

void PendingSubmitsDialog::onMarkErrorClicked(int row)
{
    if (row < 0 || row >= m_entries.size()) return;
    const Entry e = m_entries[row];

    if (!markPendingEventUnreconciled(m_db, e.ticketNum, e.seq)) {
        m_lblResult->setText(UiKit::errorHtml(tr("No se pudo actualizar el ticket %1.").arg(e.ticketNum.toHtmlEscaped())));
        return;
    }
    m_lblResult->setText(UiKit::warnHtml(tr("Ticket %1 marcado como ERROR.").arg(verifactuInvoiceId(e.ticketNum, e.seq).toHtmlEscaped()))
                         + "<br>" + tr("Revíselo en la sede electrónica de AEAT o con Consultar en AEAT en Recogida."));
    removeRowAt(row);
}

void PendingSubmitsDialog::onPostponeClicked(int row)
{
    if (row < 0 || row >= m_entries.size()) return;
    m_lblResult->showInfo(tr("Ticket %1 pospuesto al próximo arranque.")
                              .arg(verifactuInvoiceId(m_entries[row].ticketNum, m_entries[row].seq)));
    removeRowAt(row);
}

void PendingSubmitsDialog::removeRowAt(int row)
{
    if (row < 0 || row >= m_table->rowCount()) return;
    m_table->removeRow(row);
    m_entries.remove(row);
    if (m_entries.isEmpty())
        accept();
}
