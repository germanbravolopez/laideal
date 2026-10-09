#include "cancelinvoicedialog.h"
#include "sql_lite.h"

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
constexpr int COL_DATE       = 1;
constexpr int COL_IMPORTE    = 2;
constexpr int COL_ESTADO     = 3;
constexpr int COL_CSV        = 4;
constexpr int COL_ACTION     = 5;
constexpr int COL_COUNT      = 6;
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
    m_leTicketNum->setObjectName("leTicketNum");   // stable names for the e2e test bench
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
    m_table->setObjectName("table");
    m_table->setColumnCount(COL_COUNT);
    m_table->setHorizontalHeaderLabels(
        { tr("InvoiceID"), tr("Fecha factura"), tr("Importe"), tr("Estado"), tr("CSV"), tr("Acción") });
    m_table->horizontalHeader()->setSectionResizeMode(COL_CSV, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_ACTION, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_table);

    m_lblResult = new QLabel;
    m_lblResult->setObjectName("lblResult");
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
    db.close();
    m_receptionDate = QDate::fromString(date, "dd-MM-yyyy");

    QVector<SubmittedInvoiceEvent> submitted;
    if (!submittedInvoiceEvents(db, ticketNum, submitted)) {
        m_lblHeader->setText(tr("<i>Error al leer el ticket.</i>"));
        return;
    }
    for (const SubmittedInvoiceEvent &s : submitted) {
        Event e;
        e.seq         = s.seq;
        e.invoiceId   = s.invoiceId;
        e.importe     = s.importe;
        e.csv         = s.csv;
        e.estado      = s.estado;
        e.invoiceDate = QDate::fromString(s.fechaPago, "dd-MM-yyyy");
        m_events.append(e);
    }

    m_loadedTicket = ticketNum;

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
        m_table->setItem(row, COL_DATE, new QTableWidgetItem(
            e.invoiceDate.isValid() ? e.invoiceDate.toString("dd-MM-yyyy") : QStringLiteral("-")));
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

    // A cancellation of a closed quarter's invoice is accounted today; refuse it while
    // today's quarter is itself closed, or that filed report would change.
    m_pendingCancelDate = QDate::currentDate();
    if (quarterIsClosed(db, m_pendingCancelDate)) {
        m_lblResult->setText(tr("<b style='color:red'>El trimestre actual tiene la contabilidad cerrada. "
                                "Revierta la contabilidad del trimestre actual para poder anular.</b>"));
        return;
    }

    const Event &e = m_events[row];
    // AEAT keys an invoice on (emisor, InvoiceID, date): cancel it under the date it
    // was issued with - the payment date, not the ticket's reception date.
    if (!e.invoiceDate.isValid()) {
        qWarning() << "CancelInvoiceDialog: no payment date for" << e.invoiceId << "- cancellation not sent";
        m_lblResult->setText(tr("<b style='color:red'>La factura %1 no tiene fecha de pago, "
                                "no se puede anular en AEAT.</b><br>Compruebe qué tiene registrado la AEAT "
                                "con \"Consultar en AEAT\" en Recogida de prendas.").arg(e.invoiceId.toHtmlEscaped()));
        return;
    }
    setActionsEnabled(false);
    m_lblResult->setText(tr("Enviando anulación de %1 a AEAT...").arg(e.invoiceId));

    connect(m_verifactu, &VerifactuIntegration::requestFinished,
            this, &CancelInvoiceDialog::onVerifactuRequestFinished, Qt::UniqueConnection);

    m_pendingCancelRow = row;
    m_firstRejection.clear();
    m_pendingFallbackDate = (e.seq == 0 && m_receptionDate.isValid() && m_receptionDate != e.invoiceDate)
                            ? m_receptionDate : QDate();
    m_pendingCancelId  = m_verifactu->cancelInvoiceAsync(e.invoiceId, e.invoiceDate);
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

    // Before 10.9 a failed submission was re-sent with the reception date, so some old
    // seq-0 invoices are registered under it. When AEAT rejects the cancellation, try
    // that date once: only one invoice has this InvoiceID, so it cannot hit another.
    const QDate fallback = m_pendingFallbackDate;
    m_pendingFallbackDate = QDate();
    if (result.status == VerifactuResult::ERROR && fallback.isValid()) {
        qWarning() << "CancelInvoiceDialog: AEAT rejected the cancellation of" << e.invoiceId
                   << "on" << e.invoiceDate.toString("dd-MM-yyyy") << "-" << result.errorDescription
                   << "- retrying with the reception date" << fallback.toString("dd-MM-yyyy");
        m_lblResult->setText(tr("Reintentando la anulación de %1 con la fecha de recepción (%2)...")
                                 .arg(e.invoiceId.toHtmlEscaped(), fallback.toString("dd-MM-yyyy")));
        m_firstRejection   = result.errorDescription;
        m_pendingCancelRow = row;
        m_pendingCancelId  = m_verifactu->cancelInvoiceAsync(e.invoiceId, fallback);
        if (!m_pendingCancelId.isEmpty())
            return;
        m_pendingCancelRow = -1;
    }

    const QString firstRejection = m_firstRejection;
    m_firstRejection.clear();
    if (!result.isSuccess()) {
        QString text = QString("<b style='color:red'>Error al anular %1:</b> %2")
                           .arg(e.invoiceId.toHtmlEscaped(), result.errorDescription.toHtmlEscaped());
        if (!firstRejection.isEmpty())
            text = tr("<b style='color:red'>Error al anular %1.</b><br>Con la fecha de pago: %2<br>"
                      "Con la fecha de recepción: %3")
                       .arg(e.invoiceId.toHtmlEscaped(), firstRejection.toHtmlEscaped(),
                            result.errorDescription.toHtmlEscaped());
        m_lblResult->setText(text);
        setActionsEnabled(true);
        return;
    }

    // Scope the local UPDATE to the seq we just cancelled so the other payment
    // events of the same n_recibo stay ENVIADA. This is the fix the 8.5 blocker
    // pointed at: the legacy single-event flow updated WHERE n_recibo=X alone
    // and would have marked every event ANULADA in one shot.
    qDebug() << "CancelInvoiceDialog: marking ANULADA ticket" << m_loadedTicket << "seq" << e.seq;
    // Stamp the date the closed-quarter guard checked, not the (later) reply time -
    // unless that quarter was closed while the request was in flight.
    QDate cancelDate = m_pendingCancelDate;
    if (quarterIsClosed(db, cancelDate) && !quarterIsClosed(db, QDate::currentDate()))
        cancelDate = QDate::currentDate();
    if (!markInvoiceSeqCancelled(db, m_loadedTicket, e.seq, cancelDate, result.rawXml)) {
        // AEAT did cancel it: show it as such so its button cannot send a second
        // cancellation, and flag the missing local write.
        e.estado = verifactuEstadoToString(VerifactuEstado::Anulada);
        rebuildTable();
        m_lblResult->setText(tr("<b style='color:red'>La AEAT aceptó la anulación de %1, pero no se pudo guardar "
                                "en la base de datos local. Revise el log de depuración antes de hacer la "
                                "contabilidad.</b>").arg(e.invoiceId.toHtmlEscaped()));
        setActionsEnabled(true);
        return;
    }

    e.estado = verifactuEstadoToString(VerifactuEstado::Anulada);
    rebuildTable();

    m_lblResult->setText(QString("<b style='color:green'>Anulación confirmada para %1.</b><br>CSV: %2")
                             .arg(e.invoiceId.toHtmlEscaped(),
                                  result.csv.isEmpty() ? e.csv.toHtmlEscaped() : result.csv.toHtmlEscaped()));
    setActionsEnabled(true);
}
