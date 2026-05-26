#include "cancelinvoicedialog.h"
#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QSqlError>
#include <QSqlQuery>

CancelInvoiceDialog::CancelInvoiceDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Anular Factura Verifactu");
    setMinimumWidth(440);

    QVBoxLayout *layout = new QVBoxLayout(this);

    // Search row
    QHBoxLayout *searchRow = new QHBoxLayout();
    searchRow->addWidget(new QLabel("Número de ticket:"));
    m_leTicketNum = new QLineEdit();
    m_leTicketNum->setPlaceholderText("Ej: 24417");
    searchRow->addWidget(m_leTicketNum);
    QPushButton *btnSearch = new QPushButton("Buscar");
    searchRow->addWidget(btnSearch);
    layout->addLayout(searchRow);

    // Ticket info panel
    m_lblInfo = new QLabel("-");
    m_lblInfo->setWordWrap(true);
    m_lblInfo->setFrameShape(QFrame::StyledPanel);
    m_lblInfo->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(m_lblInfo);

    // Cancel button
    m_btnCancel = new QPushButton("Anular en AEAT");
    m_btnCancel->setEnabled(false);
    layout->addWidget(m_btnCancel);

    // Result label
    m_lblResult = new QLabel();
    m_lblResult->setWordWrap(true);
    layout->addWidget(m_lblResult);

    // Close button
    QPushButton *btnClose = new QPushButton("Cerrar");
    layout->addWidget(btnClose, 0, Qt::AlignRight);

    connect(btnSearch,    &QPushButton::clicked,   this, &CancelInvoiceDialog::onSearchClicked);
    connect(m_leTicketNum, &QLineEdit::returnPressed, this, &CancelInvoiceDialog::onSearchClicked);
    connect(m_btnCancel,  &QPushButton::clicked,   this, &CancelInvoiceDialog::onCancelClicked);
    connect(btnClose,     &QPushButton::clicked,   this, &QDialog::accept);
}

void CancelInvoiceDialog::onSearchClicked()
{
    m_btnCancel->setEnabled(false);
    m_lblResult->clear();
    m_loadedTicket.clear();

    QString ticketNum = m_leTicketNum->text().trimmed();
    if (ticketNum.isEmpty()) return;

    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT cliente, fecha_recepcion, SUM(importe), verifactu_csv, verifactu_estado "
              "FROM ingresos WHERE n_recibo = :num "
              "GROUP BY cliente, fecha_recepcion, verifactu_csv, verifactu_estado "
              "LIMIT 1");
    q.bindValue(":num", ticketNum);
    q.exec();

    if (!q.first()) {
        m_lblInfo->setText("<i>Ticket no encontrado.</i>");
        db.close();
        return;
    }

    QString cliente = q.value(0).toString();
    QString fecha   = q.value(1).toString();
    double  importe = q.value(2).toDouble();
    QString csv     = q.value(3).toString();
    QString estado  = q.value(4).toString();
    db.close();

    m_lblInfo->setText(
        QString("<b>Cliente:</b> %1<br>"
                "<b>Fecha:</b> %2<br>"
                "<b>Importe total:</b> %3 €<br>"
                "<b>CSV Verifactu:</b> %4<br>"
                "<b>Estado:</b> %5")
        .arg(cliente, fecha,
             QString::number(importe, 'f', 2),
             csv.isEmpty() ? "-" : csv,
             verifactuEstadoToString(verifactuEstadoFromString(estado))));

    const VerifactuEstado estadoEnum = verifactuEstadoFromString(estado);
    if (estadoEnum == VerifactuEstado::Enviada) {
        m_loadedTicket = ticketNum;
        m_loadedDate   = QDate::fromString(fecha, "dd-MM-yyyy");
        m_loadedCSV    = csv;
        m_btnCancel->setEnabled(true);
    } else {
        QString reason = (estadoEnum == VerifactuEstado::Anulada) ? "Este ticket ya está anulado en AEAT." :
                         (estadoEnum == VerifactuEstado::Error)    ? "Este ticket tuvo un error al enviarse - no hay nada que anular." :
                                                                     "Este ticket no fue enviado a AEAT - no hay nada que anular.";
        m_lblResult->setText(reason);
    }
}

void CancelInvoiceDialog::onCancelClicked()
{
    if (m_loadedTicket.isEmpty() || !m_verifactu) return;
    if (!m_pendingCancelId.isEmpty()) return; // already in flight

    m_btnCancel->setEnabled(false);
    m_lblResult->setText("Enviando anulación a AEAT...");

    // Lazy-wire the signal on first use - m_verifactu is set by MainWindow after construction.
    connect(m_verifactu, &VerifactuIntegration::requestFinished,
            this, &CancelInvoiceDialog::onVerifactuRequestFinished, Qt::UniqueConnection);

    m_pendingCancelId = m_verifactu->cancelInvoiceAsync(m_loadedTicket, m_loadedDate);
    if (m_pendingCancelId.isEmpty()) {
        m_lblResult->setText(
            QString("<b style='color:red'>Verifactu no configurado:</b> %1")
            .arg(m_verifactu->getLastError()));
        m_btnCancel->setEnabled(true);
    }
}

void CancelInvoiceDialog::onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result)
{
    if (requestId != m_pendingCancelId) return; // not ours
    m_pendingCancelId.clear();

    if (result.isSuccess()) {
        qDebug() << "CancelInvoiceDialog: UPDATE ingresos SET verifactu_estado = ANULADA WHERE n_recibo ="
                 << m_loadedTicket << "(AEAT cancel confirmed)";
        db.open();
        QSqlQuery q(db);
        q.prepare("UPDATE ingresos SET verifactu_estado = :estado WHERE n_recibo = :num");
        q.bindValue(":estado", verifactuEstadoToString(VerifactuEstado::Anulada));
        q.bindValue(":num", m_loadedTicket);
        if (!q.exec())
            qWarning() << "CancelInvoiceDialog: UPDATE estado=ANULADA failed for ticket"
                       << m_loadedTicket << "-" << q.lastError().text();
        db.close();

        m_lblResult->setText(
            QString("<b style='color:green'>Anulación confirmada.</b><br>CSV anulación: %1")
            .arg(result.csv.isEmpty() ? m_loadedCSV : result.csv));
        m_loadedTicket.clear();
    } else {
        m_lblResult->setText(
            QString("<b style='color:red'>Error al anular:</b> %1")
            .arg(result.errorDescription));
        m_btnCancel->setEnabled(true);
    }
}
