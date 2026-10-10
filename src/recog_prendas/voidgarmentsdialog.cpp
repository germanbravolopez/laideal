#include "voidgarmentsdialog.h"
#include "sql_lite.h"
#include "uikit.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int COL_SELECT  = 0;
constexpr int COL_PRENDA  = 1;
constexpr int COL_CANT    = 2;
constexpr int COL_IMPORTE = 3;
constexpr int COL_ESTADO  = 4;
constexpr int COL_COUNT   = 5;
} // namespace

VoidGarmentsDialog::VoidGarmentsDialog(const QSqlDatabase &database, QWidget *parent)
    : QDialog(parent), db(database)
{
    UiKit::setUpDialog(this, tr("Anular prendas (recibo erróneo)"), 620);
    setMinimumHeight(420);
    buildUi();
}

void VoidGarmentsDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(UiKit::introPanel(tr(
        "Anula prendas que no se han cobrado ni entregado (recibo erróneo o cambio de opinión). "
        "No se envía nada a AEAT. Las prendas pagadas se anulan con Anular factura Verifactu.")));

    auto *searchRow = new QHBoxLayout;
    searchRow->addWidget(new QLabel(tr("Nº de recibo:")));
    m_leTicketNum = new QLineEdit;
    m_leTicketNum->setObjectName("leTicketNum");   // stable names for the e2e test bench
    m_leTicketNum->setPlaceholderText(tr("Ej: 30877"));
    searchRow->addWidget(m_leTicketNum, 1);
    auto *btnSearch = UiKit::secondaryButton(tr("Buscar"), "btnSearch");
    searchRow->addWidget(btnSearch);
    layout->addLayout(searchRow);

    m_lblHeader = new QLabel("-");
    m_lblHeader->setWordWrap(true);
    layout->addWidget(m_lblHeader);

    m_table = new QTableWidget;
    m_table->setObjectName("table");
    m_table->setColumnCount(COL_COUNT);
    m_table->setHorizontalHeaderLabels(
        { tr("Anular"), tr("Prenda"), tr("Cant."), tr("Importe"), tr("Estado") });
    m_table->horizontalHeader()->setSectionResizeMode(COL_PRENDA, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_table, 1);

    auto *actions = new QHBoxLayout;
    actions->addStretch();
    m_btnVoid = UiKit::primaryButton(tr("Anular seleccionadas"), "btnVoid");
    m_btnVoid->setEnabled(false);
    actions->addWidget(m_btnVoid);
    layout->addLayout(actions);

    m_lblResult = new UiKit::ResultPanel(tr("Marque las prendas que se anulan y pulse \"Anular seleccionadas\"."));
    layout->addWidget(m_lblResult);
    layout->addLayout(UiKit::closeRow(this));

    connect(btnSearch,     &QPushButton::clicked,     this, &VoidGarmentsDialog::onSearchClicked);
    connect(m_leTicketNum, &QLineEdit::returnPressed, this, &VoidGarmentsDialog::onSearchClicked);
    connect(m_btnVoid,     &QPushButton::clicked,     this, &VoidGarmentsDialog::onVoidSelectedClicked);
}

void VoidGarmentsDialog::loadTicket(const QString &ticketNum)
{
    m_leTicketNum->setText(ticketNum);
    onSearchClicked();
}

void VoidGarmentsDialog::onSearchClicked()
{
    m_loadedTicket.clear();
    m_garments.clear();
    m_table->setRowCount(0);
    m_btnVoid->setEnabled(false);
    m_lblHeader->setText("-");

    const QString ticketNum = m_leTicketNum->text().trimmed();
    if (ticketNum.isEmpty()) return;

    db.open();
    QString client, date;
    QSqlQuery q(db);
    q.prepare("SELECT hash, prenda, cantidad, importe, pagado, verifactu_estado, "
              "estado, cliente, fecha_recepcion "
              "FROM ingresos WHERE n_recibo = :num ORDER BY rowid");
    q.bindValue(":num", ticketNum);
    if (!q.exec()) {
        qWarning() << "VoidGarmentsDialog: SELECT failed for ticket"
                   << ticketNum << "-" << q.lastError().text();
        db.close();
        m_lblResult->setText(UiKit::errorHtml(tr("Error al leer el ticket.")));
        return;
    }
    while (q.next()) {
        Garment g;
        g.hash            = q.value(0).toString();
        g.prenda          = q.value(1).toString();
        g.cantidad        = q.value(2).toString();
        g.importe         = q.value(3).toString();
        g.pagado          = q.value(4).toString();
        g.verifactuEstado = q.value(5).toString();
        g.estado          = q.value(6).toString();
        g.voidable        = garmentIsLocallyVoidable(g.pagado, g.verifactuEstado)
                            && g.estado != QLatin1String(INGRESOS_ESTADO_ANULADO);
        if (client.isEmpty()) client = q.value(7).toString();
        if (date.isEmpty())   date   = q.value(8).toString();
        m_garments.append(g);
    }
    db.close();

    if (m_garments.isEmpty()) {
        m_lblResult->setText(UiKit::warnHtml(tr("No se ha encontrado el recibo Nº %1.").arg(ticketNum.toHtmlEscaped())));
        return;
    }

    m_loadedTicket = ticketNum;
    m_lblHeader->setText(QString("<b>Cliente:</b> %1<br><b>Fecha:</b> %2")
                             .arg(client.toHtmlEscaped(), date.toHtmlEscaped()));
    rebuildTable();
}

void VoidGarmentsDialog::rebuildTable()
{
    m_table->setRowCount(m_garments.size());
    bool anyVoidable = false;
    for (int row = 0; row < m_garments.size(); ++row) {
        const Garment &g = m_garments[row];

        auto *check = new QTableWidgetItem;
        check->setFlags(g.voidable ? (Qt::ItemIsUserCheckable | Qt::ItemIsEnabled)
                                   : Qt::NoItemFlags);
        check->setCheckState(Qt::Unchecked);
        m_table->setItem(row, COL_SELECT, check);

        m_table->setItem(row, COL_PRENDA,  new QTableWidgetItem(g.prenda));
        m_table->setItem(row, COL_CANT,    new QTableWidgetItem(g.cantidad));
        m_table->setItem(row, COL_IMPORTE, new QTableWidgetItem(g.importe + " €"));
        // Reason a row is not selectable: already voided, or paid/sent to AEAT
        // (must go through "Anular Factura Verifactu").
        QString estadoText = g.estado;
        if (g.estado == QLatin1String(INGRESOS_ESTADO_ANULADO))
            estadoText = tr("Anulado");
        else if (!g.voidable)
            estadoText = tr("Pagado/enviado - usar Anular Factura");
        m_table->setItem(row, COL_ESTADO, new QTableWidgetItem(estadoText));

        anyVoidable = anyVoidable || g.voidable;
    }
    m_btnVoid->setEnabled(anyVoidable);
    if (!anyVoidable)
        m_lblResult->setText(UiKit::warnHtml(tr("Este recibo no tiene prendas que se puedan anular aquí."))
                             + "<br>" + tr("Las pagadas se anulan con Anular factura Verifactu."));
}

void VoidGarmentsDialog::onVoidSelectedClicked()
{
    if (m_loadedTicket.isEmpty()) return;

    QVector<int> rowsToVoid;
    for (int row = 0; row < m_garments.size(); ++row) {
        QTableWidgetItem *check = m_table->item(row, COL_SELECT);
        if (m_garments[row].voidable && check && check->checkState() == Qt::Checked)
            rowsToVoid.append(row);
    }
    if (rowsToVoid.isEmpty()) {
        m_lblResult->setText(UiKit::warnHtml(tr("Marque al menos una prenda para anular.")));
        return;
    }

    const auto resp = QMessageBox::question(
        this, tr("Anular prendas"),
        tr("¿Anular %1 prenda(s) del ticket %2? Esta acción no se puede deshacer.")
            .arg(rowsToVoid.size()).arg(m_loadedTicket),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (resp != QMessageBox::Yes) return;

    int voided = 0;
    for (int row : rowsToVoid) {
        if (voidGarmentRow(db, m_loadedTicket, m_garments[row].hash))
            ++voided;
    }
    qDebug() << "VoidGarmentsDialog: voided" << voided << "of" << rowsToVoid.size()
             << "garment(s) for ticket" << m_loadedTicket;

    m_voidedTotal += voided;
    const QString done = UiKit::okHtml(tr("%1 prenda(s) del recibo %2 anuladas.").arg(voided).arg(m_loadedTicket.toHtmlEscaped()));
    onSearchClicked(); // reload so the voided rows show as Anulado / non-selectable
    m_lblResult->setText(done);
}
