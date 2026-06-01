#include "cancelinvoicedialog.h"

#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int COL_INVOICE_ID = 0;
constexpr int COL_IMPORTE    = 1;
constexpr int COL_ESTADO     = 2;
constexpr int COL_CSV        = 3;
constexpr int COL_ACTION     = 4;
constexpr int COL_COUNT      = 5;
} // namespace

CancelInvoiceDialog::CancelInvoiceDialog(const QSqlDatabase &database, QWidget *parent)
    : QDialog(parent), db(database)
{
    setWindowTitle(tr("Anular Factura Verifactu"));
    setMinimumSize(620, 360);
    buildUi();
}

void CancelInvoiceDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    auto *searchRow = new QHBoxLayout;
    searchRow->addWidget(new QLabel(tr("Número de ticket:")));
    m_leTicketNum = new QLineEdit;
    m_leTicketNum->setPlaceholderText(tr("Ej: 24417"));
    searchRow->addWidget(m_leTicketNum);
    auto *btnSearch = new QPushButton(tr("Buscar"));
    searchRow->addWidget(btnSearch);
    layout->addLayout(searchRow);

    m_lblHeader = new QLabel("-");
    m_lblHeader->setWordWrap(true);
    m_lblHeader->setFrameShape(QFrame::StyledPanel);
    m_lblHeader->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(m_lblHeader);

    m_table = new QTableWidget;
    m_table->setColumnCount(COL_COUNT);
    m_table->setHorizontalHeaderLabels(
        { tr("InvoiceID"), tr("Importe"), tr("Estado"), tr("CSV"), tr("Acción") });
    m_table->horizontalHeader()->setSectionResizeMode(COL_CSV, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_ACTION, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_table);

    m_lblResult = new QLabel;
    m_lblResult->setWordWrap(true);
    layout->addWidget(m_lblResult);

    auto *btnClose = new QPushButton(tr("Cerrar"));
    layout->addWidget(btnClose, 0, Qt::AlignRight);

    connect(btnSearch,     &QPushButton::clicked,    this, &CancelInvoiceDialog::onSearchClicked);
    connect(m_leTicketNum, &QLineEdit::returnPressed, this, &CancelInvoiceDialog::onSearchClicked);
    connect(btnClose,      &QPushButton::clicked,    this, &QDialog::accept);
}

void CancelInvoiceDialog::onSearchClicked()
{
    m_lblResult->clear();
    m_loadedTicket.clear();
    m_events.clear();
    m_table->setRowCount(0);
    m_lblHeader->setText("-");

    const QString ticketNum = m_leTicketNum->text().trimmed();
    if (ticketNum.isEmpty()) return;

    db.open();

    // Header: cliente + fecha_recepcion. Constants across rows of the same
    // ticket, so MIN is fine.
    QString client, date;
    {
        QSqlQuery q(db);
        q.prepare("SELECT MIN(cliente), MIN(fecha_recepcion) "
                  "FROM ingresos WHERE n_recibo = :num");
        q.bindValue(":num", ticketNum);
        if (!q.exec() || !q.first() || q.value(0).isNull()) {
            db.close();
            m_lblHeader->setText(tr("<i>Ticket no encontrado.</i>"));
            return;
        }
        client = q.value(0).toString();
        date   = q.value(1).toString();
    }

    // One entry per (n_recibo, verifactu_invoice_seq) pair that was actually
    // submitted to AEAT. The estado filter excludes never-submitted rows; row-
    // by-row partial-pay events GROUP BY seq, and a single-event legacy ticket
    // collapses to one group at seq=0. MAX over CSV/estado/invoice_id is safe
    // since rows of the same event share those values.
    {
        QSqlQuery q(db);
        q.prepare("SELECT verifactu_invoice_seq, "
                  "       COALESCE(MAX(verifactu_invoice_id), ''), "
                  "       SUM(importe), "
                  "       COALESCE(MAX(verifactu_csv), ''), "
                  "       COALESCE(MAX(verifactu_estado), '') "
                  "FROM ingresos "
                  "WHERE n_recibo = :num "
                  "  AND verifactu_estado IS NOT NULL "
                  "  AND verifactu_estado != '' "
                  "GROUP BY verifactu_invoice_seq "
                  "ORDER BY verifactu_invoice_seq");
        q.bindValue(":num", ticketNum);
        if (!q.exec()) {
            qWarning() << "CancelInvoiceDialog: events SELECT failed for ticket"
                       << ticketNum << "-" << q.lastError().text();
            db.close();
            m_lblHeader->setText(tr("<i>Error al leer el ticket.</i>"));
            return;
        }
        while (q.next()) {
            Event e;
            e.seq       = q.value(0).toInt();
            e.invoiceId = q.value(1).toString();
            e.importe   = q.value(2).toDouble();
            e.csv       = q.value(3).toString();
            e.estado    = q.value(4).toString();
            // Reconstruct the literal AEAT InvoiceID when the column is empty:
            // legacy 8.0-8.4 rows submitted as bare n_recibo (seq=0), and
            // pre-Phase-G PayDialog rows that did not populate the column.
            if (e.invoiceId.isEmpty()) {
                e.invoiceId = (e.seq == 0)
                    ? ticketNum
                    : QString("%1-%2").arg(ticketNum).arg(e.seq);
            }
            m_events.append(e);
        }
    }
    db.close();

    m_loadedTicket = ticketNum;
    m_loadedDate   = QDate::fromString(date, "dd-MM-yyyy");

    m_lblHeader->setText(QString("<b>Cliente:</b> %1<br><b>Fecha:</b> %2")
                             .arg(client.toHtmlEscaped(), date.toHtmlEscaped()));

    if (m_events.isEmpty()) {
        m_lblResult->setText(tr("Este ticket no tiene envíos en AEAT."));
        return;
    }
    rebuildTable();
}

void CancelInvoiceDialog::rebuildTable()
{
    m_table->setRowCount(m_events.size());
    for (int row = 0; row < m_events.size(); ++row) {
        const Event &e = m_events[row];
        m_table->setItem(row, COL_INVOICE_ID, new QTableWidgetItem(e.invoiceId));
        m_table->setItem(row, COL_IMPORTE,
            new QTableWidgetItem(QString::number(e.importe, 'f', 2) + " €"));
        m_table->setItem(row, COL_ESTADO,
            new QTableWidgetItem(verifactuEstadoToString(verifactuEstadoFromString(e.estado))));
        m_table->setItem(row, COL_CSV,
            new QTableWidgetItem(e.csv.isEmpty() ? QStringLiteral("-") : e.csv));

        const VerifactuEstado st = verifactuEstadoFromString(e.estado);
        auto *btn = new QPushButton(tr("Anular"));
        // Only ENVIADA events are AEAT-cancellable. ANULADA / RECTIFICADA /
        // ERROR / PENDIENTE all have no AEAT-side cancel to issue.
        btn->setEnabled(st == VerifactuEstado::Enviada);
        connect(btn, &QPushButton::clicked, this, [this, row]() { onCancelClicked(row); });
        m_table->setCellWidget(row, COL_ACTION, btn);
    }
}

void CancelInvoiceDialog::setActionsEnabled(bool enabled)
{
    for (int row = 0; row < m_events.size(); ++row) {
        if (auto *btn = qobject_cast<QPushButton *>(m_table->cellWidget(row, COL_ACTION))) {
            const VerifactuEstado st = verifactuEstadoFromString(m_events[row].estado);
            btn->setEnabled(enabled && st == VerifactuEstado::Enviada);
        }
    }
}

void CancelInvoiceDialog::onCancelClicked(int row)
{
    if (row < 0 || row >= m_events.size()) return;
    if (!m_verifactu) return;
    if (!m_pendingCancelId.isEmpty()) return; // already in flight

    const Event &e = m_events[row];
    setActionsEnabled(false);
    m_lblResult->setText(tr("Enviando anulación de %1 a AEAT...").arg(e.invoiceId));

    connect(m_verifactu, &VerifactuIntegration::requestFinished,
            this, &CancelInvoiceDialog::onVerifactuRequestFinished, Qt::UniqueConnection);

    m_pendingCancelRow = row;
    m_pendingCancelId  = m_verifactu->cancelInvoiceAsync(e.invoiceId, m_loadedDate);
    if (m_pendingCancelId.isEmpty()) {
        m_lblResult->setText(QString("<b style='color:red'>Verifactu no configurado:</b> %1")
                                 .arg(m_verifactu->getLastError()));
        m_pendingCancelRow = -1;
        setActionsEnabled(true);
    }
}

void CancelInvoiceDialog::onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result)
{
    if (requestId != m_pendingCancelId) return; // not ours
    m_pendingCancelId.clear();
    const int row = m_pendingCancelRow;
    m_pendingCancelRow = -1;

    if (row < 0 || row >= m_events.size()) {
        setActionsEnabled(true);
        return;
    }
    Event &e = m_events[row];

    if (!result.isSuccess()) {
        m_lblResult->setText(QString("<b style='color:red'>Error al anular %1:</b> %2")
                                 .arg(e.invoiceId.toHtmlEscaped(), result.errorDescription.toHtmlEscaped()));
        setActionsEnabled(true);
        return;
    }

    // Scope the local UPDATE to the seq we just cancelled so the other payment
    // events of the same n_recibo stay ENVIADA. This is the fix the 8.5 blocker
    // pointed at: the legacy single-event flow updated WHERE n_recibo=X alone
    // and would have marked every event ANULADA in one shot.
    qDebug() << "CancelInvoiceDialog: UPDATE ingresos SET verifactu_estado=ANULADA"
             << "WHERE n_recibo =" << m_loadedTicket << "AND verifactu_invoice_seq =" << e.seq;
    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET verifactu_estado = :estado "
              "WHERE n_recibo = :num AND verifactu_invoice_seq = :seq");
    q.bindValue(":estado", verifactuEstadoToString(VerifactuEstado::Anulada));
    q.bindValue(":num",    m_loadedTicket);
    q.bindValue(":seq",    e.seq);
    if (!q.exec())
        qWarning() << "CancelInvoiceDialog: UPDATE estado=ANULADA failed for ticket"
                   << m_loadedTicket << "seq" << e.seq << "-" << q.lastError().text();
    db.close();

    e.estado = verifactuEstadoToString(VerifactuEstado::Anulada);
    rebuildTable();

    m_lblResult->setText(QString("<b style='color:green'>Anulación confirmada para %1.</b><br>CSV: %2")
                             .arg(e.invoiceId.toHtmlEscaped(),
                                  result.csv.isEmpty() ? e.csv.toHtmlEscaped() : result.csv.toHtmlEscaped()));
    setActionsEnabled(true);
}
