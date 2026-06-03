#include "pendingsubmitsdialog.h"

#include "appsettings.h"
#include "sql_lite.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
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

PendingSubmitsDialog::PendingSubmitsDialog(QSqlDatabase &db, QWidget *parent)
    : QDialog(parent), db(db)
{
    setWindowTitle(tr("Envíos Verifactu pendientes"));
    setMinimumSize(720, 360);
    buildUi();
}

void PendingSubmitsDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    auto *header = new QLabel(this);
    header->setWordWrap(true);
    header->setText(tr(
        "Estos tickets quedaron sin respuesta de AEAT en una sesión anterior "
        "(verifactu_estado = PENDIENTE). Decide para cada uno:"));
    layout->addWidget(header);

    auto *legend = new QLabel(this);
    legend->setWordWrap(true);
    legend->setText(tr(
        "<b>Reintentar</b>: vuelve a enviar a AEAT (puede fallar con "
        "InvoiceID duplicado si la AEAT ya lo registró antes del cierre).<br>"
        "<b>Marcar como error</b>: deja la fila como ERROR para que la revises "
        "manualmente en la sede electrónica AEAT.<br>"
        "<b>Posponer</b>: no hace nada; el ticket volverá a aparecer en el "
        "próximo arranque."));
    layout->addWidget(legend);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(COL_COUNT);
    m_table->setHorizontalHeaderLabels(
        { tr("Nº recibo"), tr("Fecha"), tr("Cliente"), tr("Importe"), tr("Acciones") });
    m_table->horizontalHeader()->setSectionResizeMode(COL_CLIENT, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_ACTIONS, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_table);

    auto *btnClose = new QPushButton(tr("Cerrar"), this);
    layout->addWidget(btnClose, 0, Qt::AlignRight);
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
    const QString floorIso = s->verifactuPendingRecoveryFloorDate();

    // One entry per (n_recibo, verifactu_invoice_seq) whose any row is still
    // PENDIENTE on/after the recovery floor. Grouping by seq surfaces partial-
    // pay events (seq>0, InvoiceID "<n_recibo>-<seq>") as well as save-time ones
    // (seq=0), each with its own SUM(importe) - so a retry re-submits the right
    // amount under the right InvoiceID. The duplicate-InvoiceID warning still
    // covers a slow-but-already-registered AEAT submission.
    const QVector<PendingVerifactuEvent> events = pendingVerifactuEvents(db, floorIso);
    for (const PendingVerifactuEvent &ev : events) {
        Entry e;
        e.ticketNum      = ev.nRecibo;
        e.seq            = ev.seq;
        e.fechaRecepcion = QDate::fromString(ev.fecha, "dd-MM-yyyy");
        e.client         = ev.cliente;
        e.importe        = ev.importe;
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
        new QTableWidgetItem(e.fechaRecepcion.toString("dd-MM-yyyy")));
    m_table->setItem(row, COL_CLIENT, new QTableWidgetItem(e.client));
    m_table->setItem(row, COL_IMPORTE,
        new QTableWidgetItem(QString::number(e.importe, 'f', 2) + " €"));

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
    m_table->setCellWidget(row, COL_ACTIONS, cell);

    connect(btnRetry, &QPushButton::clicked, this, [this, row]() { onRetryClicked(row); });
    connect(btnError, &QPushButton::clicked, this, [this, row]() { onMarkErrorClicked(row); });
    connect(btnPost,  &QPushButton::clicked, this, [this, row]() { onPostponeClicked(row); });
}

void PendingSubmitsDialog::onRetryClicked(int row)
{
    if (row < 0 || row >= m_entries.size()) return;
    const Entry e = m_entries[row];
    if (!e.fechaRecepcion.isValid()) {
        QMessageBox::warning(this, tr("Fecha inválida"),
            tr("No se pudo leer la fecha de recepción del ticket %1.").arg(e.ticketNum));
        return;
    }
    qDebug() << "PendingSubmitsDialog: retry requested for ticket" << e.ticketNum
             << "seq=" << e.seq
             << "date=" << e.fechaRecepcion.toString(Qt::ISODate)
             << "total=" << e.importe;
    emit retryRequested(e.ticketNum, e.seq, e.fechaRecepcion, e.importe);
    // Drop the row from the table; the async reply will patch the DB. If it
    // fails the row reverts to ERROR and the operator can revisit via the
    // normal RecogPrendas retry button.
    removeRowAt(row);
}

void PendingSubmitsDialog::onMarkErrorClicked(int row)
{
    if (row < 0 || row >= m_entries.size()) return;
    const Entry e = m_entries[row];

    db.open();
    QSqlQuery q(db);
    // Scope by seq so marking one event as Error never clobbers a sibling event
    // (a different seq) of the same ticket that is still PENDIENTE.
    q.prepare(
        "UPDATE ingresos SET verifactu_estado = 'Error', "
        "verifactu_error = 'Pendiente sin reconciliar tras cierre - revisar en sede AEAT' "
        "WHERE n_recibo = :n AND verifactu_invoice_seq = :seq "
        "  AND (verifactu_estado IS NULL OR verifactu_estado = '' "
        "       OR verifactu_estado = 'PENDIENTE')");
    q.bindValue(":n", e.ticketNum);
    q.bindValue(":seq", e.seq);
    if (!q.exec()) {
        qWarning() << "PendingSubmitsDialog: UPDATE estado=Error failed for ticket"
                   << e.ticketNum << "-" << q.lastError().text();
        QMessageBox::warning(this, tr("Error"),
            tr("No se pudo actualizar el ticket %1.").arg(e.ticketNum));
        db.close();
        return;
    }
    db.close();
    removeRowAt(row);
}

void PendingSubmitsDialog::onPostponeClicked(int row)
{
    if (row < 0 || row >= m_entries.size()) return;
    removeRowAt(row);
}

void PendingSubmitsDialog::removeRowAt(int row)
{
    if (row < 0 || row >= m_table->rowCount()) return;
    m_table->removeRow(row);
    m_entries.remove(row);
    // Re-wire the lambda captures: every surviving row now has a different
    // visual position, so its buttons must point at the new index. The
    // simplest reliable way is to rebuild the action widgets in place.
    for (int r = row; r < m_entries.size(); ++r) {
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
        m_table->setCellWidget(r, COL_ACTIONS, cell);
        connect(btnRetry, &QPushButton::clicked, this, [this, r]() { onRetryClicked(r); });
        connect(btnError, &QPushButton::clicked, this, [this, r]() { onMarkErrorClicked(r); });
        connect(btnPost,  &QPushButton::clicked, this, [this, r]() { onPostponeClicked(r); });
    }
    if (m_entries.isEmpty())
        accept();
}
