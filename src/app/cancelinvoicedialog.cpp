#include "cancelinvoicedialog.h"
#include "sql_lite.h"
#include "uikit.h"

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
    UiKit::setUpDialog(this, tr("Anular Factura Verifactu"));
    setMinimumSize(620, 360);
    buildUi();
}

void CancelInvoiceDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(UiKit::introPanel(tr(
        "Anula en la AEAT una factura ya registrada. Busque el ticket y pulse <b>Anular</b> en la "
        "factura; la ventana indica qué se va a anular y se confirma con <b>Confirmar anulación</b>. "
        "La anulación no se puede deshacer.")));

    auto *searchRow = new QHBoxLayout;
    searchRow->addWidget(new QLabel(tr("Número de ticket:")));
    m_leTicketNum = new QLineEdit;
    m_leTicketNum->setObjectName("leTicketNum");   // stable names for the e2e test bench
    m_leTicketNum->setPlaceholderText(tr("Ej: 24417"));
    searchRow->addWidget(m_leTicketNum);
    m_btnSearch = UiKit::secondaryButton(tr("Buscar"), "btnSearch");
    searchRow->addWidget(m_btnSearch);
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

    m_lblResult = new UiKit::ResultPanel;
    layout->addWidget(m_lblResult);
    layout->addLayout(UiKit::closeRow(this, tr("Cerrar")));

    connect(m_btnSearch, &QPushButton::clicked, this, &CancelInvoiceDialog::onSearchClicked);
    UiKit::onEnter(m_leTicketNum, [this]() { onSearchClicked(); });
}

void CancelInvoiceDialog::onSearchClicked()
{
    if (!m_pendingCancelId.isEmpty())   // the reply belongs to the ticket shown
        return;
    m_lblResult->clear();
    m_armedRow = -1;
    m_loadedTicket.clear();
    m_events.clear();
    m_table->setRowCount(0);
    m_lblHeader->setText("-");

    const QString ticketNum = m_leTicketNum->text().trimmed();
    if (ticketNum.isEmpty()) return;

    // Header: cliente + fecha_recepcion. Constants across rows of the same
    // ticket, so MIN is fine.
    QString client, date;
    {
        ConnectionScope scope(db);
        QSqlQuery q(db);
        q.prepare("SELECT MIN(cliente), MIN(fecha_recepcion) "
                  "FROM ingresos WHERE n_recibo = :num");
        q.bindValue(":num", ticketNum);
        if (!q.exec() || !q.first() || q.value(0).isNull()) {
            m_lblHeader->setText(tr("<i>Ticket no encontrado.</i>"));
            return;
        }
        client = q.value(0).toString();
        date   = q.value(1).toString();
    }
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
        m_lblResult->setText(UiKit::warnHtml(tr("Este ticket no tiene envíos en AEAT.")));
        return;
    }
    rebuildTable();
    const QString pending = pendingRectificationOf(db, ticketNum);
    if (!pending.isEmpty()) {
        setActionsEnabled(false);
        m_lblResult->setText(UiKit::warnHtml(tr("La rectificativa %1 de este ticket está pendiente de envío a la AEAT: "
                                                "espere a que se registre antes de anularlo.").arg(pending.toHtmlEscaped())));
    }
}

void CancelInvoiceDialog::rebuildTable()
{
    m_armedRow = -1;
    m_table->setRowCount(m_events.size());
    for (int row = 0; row < m_events.size(); ++row) {
        const Event &e = m_events[row];
        m_table->setItem(row, COL_INVOICE_ID, new QTableWidgetItem(e.invoiceId));
        m_table->setItem(row, COL_DATE, new QTableWidgetItem(
            e.invoiceDate.isValid() ? e.invoiceDate.toString("dd-MM-yyyy") : QStringLiteral("-")));
        m_table->setItem(row, COL_IMPORTE,
            new QTableWidgetItem(displayMoney(e.importe) + " €"));
        m_table->setItem(row, COL_ESTADO,
            new QTableWidgetItem(verifactuEstadoDisplay(e.estado)));
        m_table->setItem(row, COL_CSV,
            new QTableWidgetItem(e.csv.isEmpty() ? QStringLiteral("-") : e.csv));

        const VerifactuEstado st = verifactuEstadoFromString(e.estado);
        auto *btn = UiKit::secondaryButton(tr("Anular"), QStringLiteral("btnCancel%1").arg(row));
        // Wide enough for the text it shows when armed.
        btn->setMinimumWidth(btn->fontMetrics().horizontalAdvance(tr("Confirmar anulación")) + 32);
        // Only ENVIADA events are AEAT-cancellable. ANULADA / RECTIFICADA /
        // ERROR / PENDIENTE all have no AEAT-side cancel to issue.
        btn->setEnabled(st == VerifactuEstado::Enviada);
        connect(btn, &QPushButton::clicked, this, [this, row]() { onCancelClicked(row); });
        m_table->setCellWidget(row, COL_ACTION, btn);
    }
}

void CancelInvoiceDialog::setActionsEnabled(bool enabled)
{
    // No other ticket can be searched while a cancellation waits for AEAT.
    m_leTicketNum->setEnabled(enabled);
    if (m_btnSearch)
        m_btnSearch->setEnabled(enabled);
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
        disarmCancellation();
        m_lblResult->setText(UiKit::errorHtml(tr("El trimestre actual tiene la contabilidad cerrada. "
                                                 "Revierta la contabilidad del trimestre actual para poder anular.")));
        return;
    }

    if (const QString blocked = m_verifactu->numberingBlockedReason(); !blocked.isEmpty()) {
        disarmCancellation();
        m_lblResult->setText(UiKit::errorHtml(blocked.toHtmlEscaped()));
        return;
    }
    const Event &e = m_events[row];
    // AEAT keys an invoice on (emisor, InvoiceID, date): cancel it under the date it
    // was issued with - the payment date, not the ticket's reception date.
    if (!e.invoiceDate.isValid()) {
        disarmCancellation();
        qWarning() << "CancelInvoiceDialog: no payment date for" << e.invoiceId << "- cancellation not sent";
        m_lblResult->setText(UiKit::errorHtml(tr("La factura %1 no tiene fecha de pago, no se puede anular en AEAT.")
                                                  .arg(e.invoiceId.toHtmlEscaped()))
                             + "<br>" + tr("Compruebe qué tiene registrado la AEAT con \"Consultar en AEAT\" en "
                                           "Recogida de prendas."));
        return;
    }
    QString rectificativa;
    if (!liveRectificationOf(db, m_loadedTicket, &rectificativa)) {
        disarmCancellation();
        m_lblResult->setText(UiKit::errorHtml(tr("No se pudo comprobar si el ticket tiene rectificativas: no se ha "
                                                 "enviado nada a la AEAT.")));
        return;
    }
    if (!rectificativa.isEmpty()) {
        disarmCancellation();
        m_lblResult->setText(UiKit::errorHtml(tr("La rectificativa %1 de este ticket está registrada en la AEAT: "
                                                 "anule antes la rectificativa (busque el ticket %1) y después esta factura.")
                                                  .arg(rectificativa.toHtmlEscaped())));
        return;
    }
    if (m_armedRow != row) {
        armCancellation(row);
        return;
    }
    disarmCancellation();
    setActionsEnabled(false);
    m_lblResult->setText(tr("Enviando anulación de %1 a AEAT...").arg(e.invoiceId.toHtmlEscaped()));

    connect(m_verifactu, &VerifactuIntegration::requestFinished,
            this, &CancelInvoiceDialog::onVerifactuRequestFinished, Qt::UniqueConnection);

    m_pendingCancelRow = row;
    m_pendingTicket = m_loadedTicket;
    m_pendingEvent = e;
    m_firstRejection.clear();
    m_pendingFallbackDate = (e.seq == 0 && m_receptionDate.isValid() && m_receptionDate != e.invoiceDate)
                            ? m_receptionDate : QDate();
    m_pendingCancelId  = m_verifactu->cancelInvoiceAsync(e.invoiceId, e.invoiceDate);
    if (m_pendingCancelId.isEmpty()) {
        m_lblResult->setText(UiKit::errorHtml(tr("Verifactu no configurado:")) + " "
                             + m_verifactu->getLastError().toHtmlEscaped());
        m_pendingCancelRow = -1;
        setActionsEnabled(true);
    }
}

void CancelInvoiceDialog::armCancellation(int row)
{
    disarmCancellation();
    auto *btn = qobject_cast<QPushButton *>(m_table->cellWidget(row, COL_ACTION));
    if (!btn)
        return;
    m_armedRow = row;
    btn->setText(tr("Confirmar anulación"));
    const Event &e = m_events[row];
    m_lblResult->setText(UiKit::warnHtml(tr("Se va a anular en la AEAT la factura %1 del %2 por %3 €.")
                                             .arg(e.invoiceId.toHtmlEscaped(), e.invoiceDate.toString("dd-MM-yyyy"),
                                                  displayMoney(e.importe)))
                         + "<br>" + tr("La anulación no se puede deshacer. Pulse <b>Confirmar anulación</b> para "
                                       "enviarla, o busque otro ticket para no hacer nada."));
}

void CancelInvoiceDialog::disarmCancellation()
{
    if (m_armedRow >= 0)
        if (auto *btn = qobject_cast<QPushButton *>(m_table->cellWidget(m_armedRow, COL_ACTION)))
            btn->setText(tr("Anular"));
    m_armedRow = -1;
}

void CancelInvoiceDialog::done(int result)
{
    if (!m_pendingCancelId.isEmpty()) {
        m_lblResult->setText(UiKit::warnHtml(tr("Esperando la respuesta de la AEAT: la ventana se cerrará o se podrá cerrar en cuanto responda.")));
        return;
    }
    QDialog::done(result);
}

void CancelInvoiceDialog::onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result)
{
    if (requestId != m_pendingCancelId) return; // not ours
    m_pendingCancelId.clear();
    const int row = m_pendingCancelRow;
    m_pendingCancelRow = -1;
    if (row < 0) {
        setActionsEnabled(true);
        return;
    }
    // The invoice the request was sent for, not whatever row `row` holds now.
    Event &e = m_pendingEvent;
    // Shows the new estado on the table row of that invoice, when the window still lists it.
    const auto showEstado = [this](const Event &sent, VerifactuEstado estado) {
        if (m_loadedTicket != m_pendingTicket)
            return;
        for (Event &shown : m_events)
            if (shown.seq == sent.seq)
                shown.estado = verifactuEstadoToString(estado);
        rebuildTable();
    };

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
    // The direct client stored the cancellation and sends it on its own: pending, not failed.
    if ((result.status == VerifactuResult::NETWORK_ERROR || result.status == VerifactuResult::PENDING)
        && m_verifactu->holdsPendingCancellation(e.invoiceId)) {
        m_lblResult->setText(UiKit::warnHtml(tr("Anulación de %1 guardada, pendiente de envío a la AEAT: %2")
                                                 .arg(e.invoiceId.toHtmlEscaped(), result.errorDescription.toHtmlEscaped()))
                             + "<br>" + tr("Se enviará sola y la factura quedará anulada; no la repita."));
        m_leTicketNum->setEnabled(true);   // another ticket may be searched; this one is settled by itself
        m_btnSearch->setEnabled(true);
        return;
    }
    if (!result.isSuccess()) {
        QString text = UiKit::errorHtml(tr("Error al anular %1:").arg(e.invoiceId.toHtmlEscaped()))
                       + " " + result.errorDescription.toHtmlEscaped();
        if (!firstRejection.isEmpty())
            text = UiKit::errorHtml(tr("Error al anular %1.").arg(e.invoiceId.toHtmlEscaped()))
                   + "<br>" + tr("Con la fecha de pago: %1<br>Con la fecha de recepción: %2")
                                  .arg(firstRejection.toHtmlEscaped(), result.errorDescription.toHtmlEscaped());
        m_lblResult->setText(text);
        setActionsEnabled(true);
        return;
    }

    // Scope the local UPDATE to the seq we just cancelled so the other payment
    // events of the same n_recibo stay ENVIADA. This is the fix the 8.5 blocker
    // pointed at: the legacy single-event flow updated WHERE n_recibo=X alone
    // and would have marked every event ANULADA in one shot.
    qDebug() << "CancelInvoiceDialog: marking ANULADA ticket" << m_pendingTicket << "seq" << e.seq;
    // Stamp the date the closed-quarter guard checked, not the (later) reply time -
    // unless that quarter was closed while the request was in flight.
    QDate cancelDate = m_pendingCancelDate;
    if (quarterIsClosed(db, cancelDate) && !quarterIsClosed(db, QDate::currentDate()))
        cancelDate = QDate::currentDate();
    if (!markInvoiceSeqCancelled(db, m_pendingTicket, e.seq, cancelDate, result.rawXml)) {
        // AEAT did cancel it: show it as such so its button cannot send a second
        // cancellation, and flag the missing local write.
        showEstado(e, VerifactuEstado::Anulada);
        m_lblResult->setText(UiKit::errorHtml(tr("La AEAT aceptó la anulación de %1, pero no se pudo guardar "
                                                 "en la base de datos local. Revise el log de depuración antes de "
                                                 "hacer la contabilidad.").arg(e.invoiceId.toHtmlEscaped())));
        setActionsEnabled(true);
        return;
    }

    showEstado(e, VerifactuEstado::Anulada);

    m_lblResult->setText(UiKit::okHtml(tr("Anulación confirmada para %1.").arg(e.invoiceId.toHtmlEscaped()))
                         + "<br>" + tr("CSV: %1").arg(result.csv.isEmpty() ? e.csv.toHtmlEscaped()
                                                                            : result.csv.toHtmlEscaped()));
    setActionsEnabled(true);
}
