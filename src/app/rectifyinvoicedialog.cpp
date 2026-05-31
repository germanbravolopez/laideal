#include "rectifyinvoicedialog.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QVBoxLayout>

#include "appsettings.h"
#include "sql_lite.h"

namespace {

// Maps the combobox index to the R1-R5 invoice type enum.
VerifactuInvoice::InvoiceType invoiceTypeFromIndex(int idx)
{
    switch (idx) {
        case 0 : return VerifactuInvoice::RECTIFICATION_R1;
        case 1 : return VerifactuInvoice::RECTIFICATION_R2;
        case 2 : return VerifactuInvoice::RECTIFICATION_R3;
        case 3 : return VerifactuInvoice::RECTIFICATION_R4;
        case 4 : return VerifactuInvoice::RECTIFICATION_R5;
        default: return VerifactuInvoice::RECTIFICATION_R1;
    }
}

} // namespace

RectifyInvoiceDialog::RectifyInvoiceDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Factura rectificativa Verifactu");
    setMinimumWidth(520);

    QVBoxLayout *layout = new QVBoxLayout(this);

    // Search row -----------------------------------------------------------
    QHBoxLayout *searchRow = new QHBoxLayout();
    searchRow->addWidget(new QLabel("Número de ticket original:"));
    m_leTicketNum = new QLineEdit();
    m_leTicketNum->setPlaceholderText("Ej: 24417");
    searchRow->addWidget(m_leTicketNum);
    QPushButton *btnSearch = new QPushButton("Buscar");
    searchRow->addWidget(btnSearch);
    layout->addLayout(searchRow);

    // Ticket info panel ----------------------------------------------------
    m_lblInfo = new QLabel("-");
    m_lblInfo->setWordWrap(true);
    m_lblInfo->setFrameShape(QFrame::StyledPanel);
    m_lblInfo->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(m_lblInfo);

    // Rectification form ---------------------------------------------------
    m_grpForm = new QGroupBox("Datos de la rectificativa");
    QVBoxLayout *formLayout = new QVBoxLayout(m_grpForm);

    QHBoxLayout *rowType = new QHBoxLayout();
    rowType->addWidget(new QLabel("Tipo:"));
    m_cbInvoiceType = new QComboBox();
    m_cbInvoiceType->addItem("R1 - Error fundado en derecho (art. 80.1, 80.2 y 80.6 LIVA)");
    m_cbInvoiceType->addItem("R2 - Concurso de acreedores (art. 80.3 LIVA)");
    m_cbInvoiceType->addItem("R3 - Créditos incobrables (art. 80.4 LIVA)");
    m_cbInvoiceType->addItem("R4 - Resto de causas");
    m_cbInvoiceType->addItem("R5 - Rectificación de factura simplificada");
    m_cbInvoiceType->setCurrentIndex(0); // R1 most common
    rowType->addWidget(m_cbInvoiceType, /*stretch*/ 1);
    formLayout->addLayout(rowType);

    QHBoxLayout *rowMode = new QHBoxLayout();
    rowMode->addWidget(new QLabel("Modo:"));
    m_rbDifferences  = new QRadioButton("Por diferencias (I)");
    m_rbSubstitution = new QRadioButton("Por sustitución (S)");
    m_rbDifferences->setChecked(true);
    rowMode->addWidget(m_rbDifferences);
    rowMode->addWidget(m_rbSubstitution);
    rowMode->addStretch();
    formLayout->addLayout(rowMode);

    QHBoxLayout *rowDate = new QHBoxLayout();
    rowDate->addWidget(new QLabel("Fecha rectificativa:"));
    m_deRectifyDate = new QDateEdit(QDate::currentDate());
    m_deRectifyDate->setCalendarPopup(true);
    m_deRectifyDate->setDisplayFormat("dd-MM-yyyy");
    rowDate->addWidget(m_deRectifyDate);
    rowDate->addStretch();
    formLayout->addLayout(rowDate);

    QHBoxLayout *rowAmount = new QHBoxLayout();
    m_lblAmount = new QLabel(); // text set by onRectificationModeChanged()
    rowAmount->addWidget(m_lblAmount);
    m_sbAmount = new QDoubleSpinBox();
    m_sbAmount->setDecimals(2);
    m_sbAmount->setRange(-100000.00, 100000.00); // negative deltas allowed in I mode
    m_sbAmount->setSingleStep(0.10);
    m_sbAmount->setSuffix(" €");
    rowAmount->addWidget(m_sbAmount);
    rowAmount->addStretch();
    formLayout->addLayout(rowAmount);

    QHBoxLayout *rowDesc = new QHBoxLayout();
    rowDesc->addWidget(new QLabel("Motivo:"));
    m_leDescription = new QLineEdit();
    m_leDescription->setPlaceholderText("Opcional - descripción visible en la rectificativa");
    rowDesc->addWidget(m_leDescription);
    formLayout->addLayout(rowDesc);

    layout->addWidget(m_grpForm);
    m_grpForm->setEnabled(false);

    // Submit + result ------------------------------------------------------
    m_btnRectify = new QPushButton("Enviar rectificativa a AEAT");
    m_btnRectify->setEnabled(false);
    layout->addWidget(m_btnRectify);

    m_lblResult = new QLabel();
    m_lblResult->setWordWrap(true);
    layout->addWidget(m_lblResult);

    QPushButton *btnClose = new QPushButton("Cerrar");
    layout->addWidget(btnClose, 0, Qt::AlignRight);

    // Wiring ---------------------------------------------------------------
    connect(btnSearch,        &QPushButton::clicked,     this, &RectifyInvoiceDialog::onSearchClicked);
    connect(m_leTicketNum,    &QLineEdit::returnPressed, this, &RectifyInvoiceDialog::onSearchClicked);
    connect(m_btnRectify,     &QPushButton::clicked,     this, &RectifyInvoiceDialog::onRectifyClicked);
    connect(m_rbDifferences,  &QRadioButton::toggled,    this, &RectifyInvoiceDialog::onRectificationModeChanged);
    connect(m_rbSubstitution, &QRadioButton::toggled,    this, &RectifyInvoiceDialog::onRectificationModeChanged);
    connect(btnClose,         &QPushButton::clicked,     this, &QDialog::accept);

    onRectificationModeChanged(); // set initial label
}

void RectifyInvoiceDialog::onRectificationModeChanged()
{
    if (m_rbSubstitution->isChecked())
        m_lblAmount->setText("Importe TOTAL corregido (con IVA):");
    else
        m_lblAmount->setText("Diferencia de importe (delta, con IVA):");
}

void RectifyInvoiceDialog::setFormEnabled(bool enabled)
{
    m_grpForm->setEnabled(enabled);
    m_btnRectify->setEnabled(enabled);
}

void RectifyInvoiceDialog::onSearchClicked()
{
    setFormEnabled(false);
    m_lblResult->clear();
    m_loadedTicket.clear();

    const QString ticketNum = m_leTicketNum->text().trimmed();
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

    const QString cliente = q.value(0).toString();
    const QString fecha   = q.value(1).toString();
    const double  importe = q.value(2).toDouble();
    const QString csv     = q.value(3).toString();
    const QString estado  = q.value(4).toString();

    // 8.5+ guard: a ticket with multiple partial-pay events (verifactu_invoice_seq
    // > 0 on any row) was submitted to AEAT under several distinct InvoiceIDs.
    // applyRectificationResult() below issues `UPDATE ... WHERE n_recibo = X`
    // which would mark every payment event's rows RECTIFICADA in one shot - that
    // both loses per-seq AEAT alignment AND silently underreports quarterly
    // income in `contabilidad` (RECTIFICADA is excluded from totals). Block
    // until per-seq rectification is implemented.
    QSqlQuery seqQ(db);
    seqQ.prepare("SELECT COUNT(*) FROM ingresos "
                 "WHERE n_recibo = :num AND verifactu_invoice_seq > 0");
    seqQ.bindValue(":num", ticketNum);
    const bool hasPartialPays = seqQ.exec() && seqQ.first() && seqQ.value(0).toInt() > 0;
    db.close();

    if (hasPartialPays) {
        m_lblInfo->setText(
            QString("<b>Cliente:</b> %1<br><b>Fecha:</b> %2<br>"
                    "<i>Este ticket tiene varios pagos parciales. La rectificación "
                    "individual de cada pago no está disponible en esta versión; "
                    "rectificar desde la sede electrónica de la AEAT.</i>")
            .arg(cliente, fecha));
        return;
    }

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
        m_loadedTicket        = ticketNum;
        m_loadedCliente       = cliente;
        m_loadedImporteTotal  = importe;
        m_loadedCSV           = csv;
        // Sensible default: in S mode, prefill with the original total so the user
        // adjusts down; in I mode, leave at 0.
        m_sbAmount->setValue(m_rbDifferences->isChecked() ? 0.00 : importe);
        setFormEnabled(true);
    } else {
        const QString reason = (estadoEnum == VerifactuEstado::Anulada)     ? "Este ticket está anulado en AEAT - no se puede rectificar."
                             : (estadoEnum == VerifactuEstado::Rectificada) ? "Este ticket ya fue rectificado previamente."
                             : (estadoEnum == VerifactuEstado::Error)       ? "Este ticket tuvo un error al enviarse - no hay nada que rectificar."
                                                                            : "Este ticket no fue enviado a AEAT - no hay nada que rectificar.";
        m_lblResult->setText(reason);
    }
}

void RectifyInvoiceDialog::onRectifyClicked()
{
    if (m_loadedTicket.isEmpty() || !m_verifactu) return;
    if (!m_pendingRectifyId.isEmpty()) return; // already in flight

    const double amountWithIva = m_sbAmount->value();
    const bool   isSubstitution = m_rbSubstitution->isChecked();

    if (isSubstitution && amountWithIva <= 0.0) {
        m_lblResult->setText("<b style='color:red'>El importe corregido total debe ser mayor que 0.</b>");
        return;
    }
    if (!isSubstitution && qFuzzyIsNull(amountWithIva)) {
        m_lblResult->setText("<b style='color:red'>La diferencia no puede ser 0.</b>");
        return;
    }

    const double ivaRate    = AppSettings::instance()->ivaRate();
    const double divisor    = 1.0 + ivaRate / 100.0;
    const double newTaxBase   = amountWithIva / divisor;
    const double newTaxAmount = amountWithIva - newTaxBase;

    // For substitution we send the ORIGINAL values as RectificationTaxBase/Amount.
    const double origTaxBase   = m_loadedImporteTotal / divisor;
    const double origTaxAmount = m_loadedImporteTotal - origTaxBase;

    setFormEnabled(false);
    m_lblResult->setText("Enviando rectificativa a AEAT...");

    // Lazy-wire the signal on first use - m_verifactu is set by MainWindow after construction.
    connect(m_verifactu, &VerifactuIntegration::requestFinished,
            this, &RectifyInvoiceDialog::onVerifactuRequestFinished, Qt::UniqueConnection);

    // Allocate the next n_recibo AND eagerly insert a PENDIENTE placeholder row so the
    // number is claimed locally before the AEAT round-trip. Otherwise a regular
    // MainWindow::saveTicket happening during the seconds-long AEAT call could pick the
    // same number. Same pattern as MainWindow::saveTicket -> updateTicketVerifactuFields.
    m_newInvoiceNumber = QString::number(readMaxValueInColumnFromTable(db, "n_recibo", "ingresos") + 1);
    m_newInvoiceDate   = m_deRectifyDate->date();
    m_submittedAmount  = amountWithIva;
    m_submittedIsSubstitution = isSubstitution;
    insertPlaceholderRow();

    const auto invoiceType = invoiceTypeFromIndex(m_cbInvoiceType->currentIndex());
    const auto rectType    = isSubstitution ? VerifactuInvoice::BY_SUBSTITUTION
                                            : VerifactuInvoice::BY_DIFFERENCES;
    const QString desc = m_leDescription->text().trimmed().isEmpty()
        ? QString("Rectificativa de ticket %1").arg(m_loadedTicket)
        : m_leDescription->text().trimmed();

    qDebug() << "RectifyInvoiceDialog: submit rectificativa for ticket" << m_loadedTicket
             << "newInvoiceID=" << m_newInvoiceNumber
             << "type=" << m_cbInvoiceType->currentIndex() + 1
             << "mode=" << (isSubstitution ? "S" : "I")
             << "amount=" << amountWithIva;

    m_pendingRectifyId = m_verifactu->submitRectificationAsync(
        m_newInvoiceNumber, m_newInvoiceDate, invoiceType, rectType,
        newTaxBase, newTaxAmount, origTaxBase, origTaxAmount,
        ivaRate, desc);

    if (m_pendingRectifyId.isEmpty()) {
        m_lblResult->setText(
            QString("<b style='color:red'>Verifactu no configurado:</b> %1")
            .arg(m_verifactu->getLastError()));
        setFormEnabled(true);
    }
}

void RectifyInvoiceDialog::onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result)
{
    if (requestId != m_pendingRectifyId) return; // not ours
    m_pendingRectifyId.clear();

    applyRectificationResult(result);

    if (result.isSuccess()) {
        m_lblResult->setText(
            QString("<b style='color:green'>Rectificativa enviada.</b><br>"
                    "Nuevo ticket: %1<br>CSV: %2")
            .arg(m_newInvoiceNumber,
                 result.csv.isEmpty() ? "(sin CSV)" : result.csv));
        m_loadedTicket.clear();
        m_leTicketNum->clear();
        m_lblInfo->setText("-");
    } else {
        m_lblResult->setText(
            QString("<b style='color:red'>Error al rectificar:</b> %1")
            .arg(result.errorDescription));
        setFormEnabled(true);
    }
}

void RectifyInvoiceDialog::insertPlaceholderRow()
{
    const QString fechaStr = m_newInvoiceDate.toString("dd-MM-yyyy");
    const QString modeStr  = VerifactuInvoice::rectificationTypeToString(
        m_submittedIsSubstitution ? VerifactuInvoice::BY_SUBSTITUTION
                                  : VerifactuInvoice::BY_DIFFERENCES);
    const QString hash = genHash16();

    qDebug() << "RectifyInvoiceDialog::insertPlaceholderRow:"
             << "newTicket=" << m_newInvoiceNumber
             << "rectifies=" << m_loadedTicket
             << "mode=" << modeStr
             << "amount=" << m_submittedAmount;

    db.open();
    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO ingresos "
        "(n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, "
        "cantidad, prenda, size, servicio, observaciones, edit_lock, hash, "
        "verifactu_csv, verifactu_timestamp, verifactu_estado, verifactu_error, verifactu_url_qr, "
        "verifactu_xml, verifactu_hash, "
        "verifactu_rectifies_n_recibo, verifactu_rectification_type) "
        "VALUES "
        "(:n_recibo, :cliente, :fecha_recepcion, :fecha_pago, :fecha_recogida, :importe, :pagado, :estado, "
        ":cantidad, :prenda, :size, :servicio, :observaciones, :edit_lock, :hash, "
        ":vcsv, :vts, :vestado, :verror, :vurl, "
        ":vxml, :vhash, "
        ":vrectifies, :vrecttype);");
    q.bindValue(":n_recibo",        m_newInvoiceNumber);
    q.bindValue(":cliente",         m_loadedCliente);
    q.bindValue(":fecha_recepcion", fechaStr);
    q.bindValue(":fecha_pago",      fechaStr); // rectificativa always reflects a paid operation
    q.bindValue(":fecha_recogida",  "");
    q.bindValue(":importe",         QString::number(m_submittedAmount, 'f', 2));
    q.bindValue(":pagado",          "SI");
    q.bindValue(":estado",          "Rectificativa");
    q.bindValue(":cantidad",        "1");
    q.bindValue(":prenda",          QString("Rectificativa de ticket %1").arg(m_loadedTicket));
    q.bindValue(":size",            "");
    q.bindValue(":servicio",        "");
    q.bindValue(":observaciones",   QString("Rectificativa %1 (%2)").arg(
                                        VerifactuInvoice::isRectificationInvoiceType(
                                            invoiceTypeFromIndex(m_cbInvoiceType->currentIndex()))
                                            ? m_cbInvoiceType->currentText().left(2)
                                            : QString("R1"),
                                        modeStr));
    q.bindValue(":edit_lock",       "0");
    q.bindValue(":hash",            hash);
    // Placeholder: AEAT metadata patched by applyRectificationResult() on reply.
    q.bindValue(":vcsv",            "");
    q.bindValue(":vts",             "");
    q.bindValue(":vestado",         verifactuEstadoToString(VerifactuEstado::NotSubmitted));
    q.bindValue(":verror",          "");
    q.bindValue(":vurl",            "");
    q.bindValue(":vxml",            "");
    q.bindValue(":vhash",           "");
    q.bindValue(":vrectifies",      m_loadedTicket);
    q.bindValue(":vrecttype",       modeStr);
    if (!q.exec())
        qWarning() << "RectifyInvoiceDialog: INSERT placeholder rectificativa row failed for new ticket"
                   << m_newInvoiceNumber << "-" << q.lastError().text();
    db.close();
}

void RectifyInvoiceDialog::applyRectificationResult(const VerifactuResult &result)
{
    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    const VerifactuEstado estado = result.isSuccess() ? VerifactuEstado::Enviada
                                                      : VerifactuEstado::Error;

    qDebug() << "RectifyInvoiceDialog::applyRectificationResult:"
             << "newTicket=" << m_newInvoiceNumber
             << "estado="    << verifactuEstadoToString(estado)
             << "csv="       << (result.isSuccess() ? result.csv : QString())
             << "error="     << (result.isSuccess() ? QString() : result.errorDescription);

    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET "
              "verifactu_csv = :csv, verifactu_timestamp = :ts, "
              "verifactu_estado = :estado, verifactu_error = :error, verifactu_url_qr = :url, "
              "verifactu_xml = :xml, verifactu_hash = :hash, verifactu_invoice_id = :id "
              "WHERE n_recibo = :num AND verifactu_rectifies_n_recibo = :orig");
    if (result.isSuccess()) {
        q.bindValue(":csv",    result.csv);
        q.bindValue(":ts",     timestamp);
        q.bindValue(":estado", verifactuEstadoToString(VerifactuEstado::Enviada));
        q.bindValue(":error",  "");
        q.bindValue(":url",    result.validationUrl);
        q.bindValue(":xml",    result.rawXml);
        q.bindValue(":hash",   result.rawHash);
        // Rectificativa submitted to AEAT under the new n_recibo as its literal
        // InvoiceID; persist so Imprimir's QR/header reads it authoritatively.
        q.bindValue(":id",     m_newInvoiceNumber);
    } else {
        q.bindValue(":csv",    "");
        q.bindValue(":ts",     timestamp);
        q.bindValue(":estado", verifactuEstadoToString(VerifactuEstado::Error));
        q.bindValue(":error",  result.errorDescription);
        q.bindValue(":url",    "");
        q.bindValue(":xml",    "");
        q.bindValue(":hash",   "");
        q.bindValue(":id",     "");
    }
    q.bindValue(":num",  m_newInvoiceNumber);
    q.bindValue(":orig", m_loadedTicket);
    if (!q.exec())
        qWarning() << "RectifyInvoiceDialog: UPDATE placeholder rectificativa row failed for new ticket"
                   << m_newInvoiceNumber << "-" << q.lastError().text();

    // For substitution (S), mark the original rows as RECTIFICADA so they no longer
    // count toward accounting. Differences (I) leaves the original untouched - the
    // delta row alone reconciles the books. Only on AEAT success.
    if (result.isSuccess() && m_submittedIsSubstitution) {
        qDebug() << "RectifyInvoiceDialog: UPDATE ingresos SET verifactu_estado = RECTIFICADA WHERE n_recibo ="
                 << m_loadedTicket;
        QSqlQuery up(db);
        up.prepare("UPDATE ingresos SET verifactu_estado = :estado WHERE n_recibo = :num");
        up.bindValue(":estado", verifactuEstadoToString(VerifactuEstado::Rectificada));
        up.bindValue(":num",    m_loadedTicket);
        if (!up.exec())
            qWarning() << "RectifyInvoiceDialog: UPDATE estado=RECTIFICADA failed for ticket"
                       << m_loadedTicket << "-" << up.lastError().text();
    }
    db.close();
}
