#include "refusedinvoicesdialog.h"

#include "uikit.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

RefusedInvoicesDialog::RefusedInvoicesDialog(const QSqlDatabase &database, const QString &floorIso, QWidget *parent)
    : QDialog(parent)
    , db(database)
    , m_floorIso(floorIso)
{
    UiKit::setUpDialog(this, tr("Facturas rechazadas por la AEAT"), 760);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(UiKit::introPanel(tr(
        "Facturas cobradas que la AEAT ha rechazado y siguen sin reenviar. Seleccione una y pulse \"Abrir en "
        "Recogida\": allí, Verifactu2026 → Reintentar después de corregir la causa, o Consultar en AEAT si el "
        "motivo dice que ya existe. Una rectificativa se rehace en Facturas → Rectificar factura.")));

    m_table = new QTableWidget(0, 4);
    m_table->setObjectName("tableRefused");
    m_table->setHorizontalHeaderLabels({ tr("Ticket"), tr("Fecha de pago"), tr("Importe"), tr("Motivo de la AEAT") });
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    for (int c = 0; c < 3; ++c)
        m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table);

    QHBoxLayout *actions = new QHBoxLayout();
    actions->addStretch();
    m_btnOpen = UiKit::primaryButton(tr("Abrir en Recogida"), "btnOpenRecogida");
    actions->addWidget(m_btnOpen);
    layout->addLayout(actions);
    m_result = new UiKit::ResultPanel();
    layout->addWidget(m_result);
    layout->addLayout(UiKit::closeRow(this));

    connect(m_btnOpen, &QPushButton::clicked, this, &RefusedInvoicesDialog::openSelected);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &RefusedInvoicesDialog::openSelected);
    reload();
}

void RefusedInvoicesDialog::reload()
{
    m_invoices = refusedInvoices(db, m_floorIso);
    m_table->setRowCount(0);
    for (const RefusedInvoice &r : m_invoices) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        const QString ticket = r.seq > 0 ? QStringLiteral("%1-%2").arg(r.nRecibo).arg(r.seq) : r.nRecibo;
        m_table->setItem(row, 0, new QTableWidgetItem(r.rectificativa ? tr("%1 (rectificativa)").arg(ticket) : ticket));
        m_table->setItem(row, 1, new QTableWidgetItem(r.fechaPago));
        auto *amount = new QTableWidgetItem(displayMoney(r.importe) + " €");
        amount->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 2, amount);
        auto *reason = new QTableWidgetItem(r.error);
        reason->setToolTip(r.error);
        m_table->setItem(row, 3, reason);
    }
    if (!m_invoices.isEmpty())
        m_table->selectRow(0);
    m_btnOpen->setEnabled(!m_invoices.isEmpty());
    m_result->setText(m_invoices.isEmpty() ? UiKit::okHtml(tr("No queda ninguna factura rechazada."))
                                           : tr("%n factura(s) rechazada(s).", "", m_invoices.size()));
}

void RefusedInvoicesDialog::openSelected()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_invoices.size())
        return;
    emit openTicket(m_invoices[row].nRecibo, m_invoices[row].seq);
}
