#include "rectifyinvoicedialog.h"
#include "aeatstore.h"

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
#include "uikit.h"
#include "aeatresponse.h"
#include "aeatdirectbackend.h"

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

RectifyInvoiceDialog::RectifyInvoiceDialog(const QSqlDatabase &database, QWidget *parent)
    : QDialog(parent), db(database)
{
    UiKit::setUpDialog(this, "Factura rectificativa Verifactu");

    QVBoxLayout *layout = new QVBoxLayout(this);

    // Search row -----------------------------------------------------------
    QHBoxLayout *searchRow = new QHBoxLayout();
    searchRow->addWidget(new QLabel("Número de ticket original:"));
    m_leTicketNum = new QLineEdit();
    m_leTicketNum->setObjectName("leTicketNum");   // stable names for the e2e test bench
    m_leTicketNum->setPlaceholderText("Ej: 24417");
    searchRow->addWidget(m_leTicketNum);
    QPushButton *btnSearch = new QPushButton("Buscar");
    m_btnSearch = btnSearch;
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
    m_cbInvoiceType->setObjectName("cbInvoiceType");
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
    m_rbSubstitution->setObjectName("rbSubstitution");
    m_rbDifferences->setObjectName("rbDifferences");
    m_rbDifferences->setChecked(true);
    rowMode->addWidget(m_rbDifferences);
    rowMode->addWidget(m_rbSubstitution);
    rowMode->addStretch();
    formLayout->addLayout(rowMode);

    QHBoxLayout *rowDate = new QHBoxLayout();
    rowDate->addWidget(new QLabel("Fecha rectificativa:"));
    m_deRectifyDate = new QDateEdit(QDate::currentDate());
    m_deRectifyDate->setObjectName("deRectifyDate");
    m_deRectifyDate->setCalendarPopup(true);
    m_deRectifyDate->setDisplayFormat("dd-MM-yyyy");
    rowDate->addWidget(m_deRectifyDate);
    rowDate->addStretch();
    formLayout->addLayout(rowDate);

    QHBoxLayout *rowAmount = new QHBoxLayout();
    m_lblAmount = new QLabel(); // text set by onRectificationModeChanged()
    rowAmount->addWidget(m_lblAmount);
    m_sbAmount = new UiKit::AmountSpinBox();
    m_sbAmount->setObjectName("sbAmount");
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
    m_btnRectify->setObjectName("btnRectify");
    m_btnRectify->setEnabled(false);
    layout->addWidget(m_btnRectify);

    m_lblResult = new QLabel();
    m_lblResult->setObjectName("lblResult");
    m_lblResult->setWordWrap(true);
    layout->addWidget(m_lblResult);

    QPushButton *btnClose = new QPushButton("Cerrar");
    layout->addWidget(btnClose, 0, Qt::AlignRight);

    // Wiring ---------------------------------------------------------------
    connect(btnSearch,        &QPushButton::clicked,     this, &RectifyInvoiceDialog::onSearchClicked);
    UiKit::onEnter(m_leTicketNum, [this]() { onSearchClicked(); });
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
    if (!m_pendingRectifyId.isEmpty())   // the reply belongs to the ticket shown
        return;
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
             displayMoney(importe),
             csv.isEmpty() ? "-" : csv,
             verifactuEstadoToString(verifactuEstadoFromString(estado))));

    const VerifactuEstado estadoEnum = verifactuEstadoFromString(estado);
    const QString pending = pendingRectificationOf(db, ticketNum);
    const bool cancellationPending = m_verifactu && m_verifactu->directBackend()
        && m_verifactu->directBackend()->holdsPendingRecord(AeatStore::Kind::Cancellation, verifactuInvoiceId(ticketNum, 0));
    if (estadoEnum == VerifactuEstado::Enviada && cancellationPending) {
        m_lblResult->setText(UiKit::warnHtml(tr("La anulación de este ticket está pendiente de envío a la AEAT: "
                                                "no se puede rectificar.")));
    } else if (estadoEnum == VerifactuEstado::Enviada && !pending.isEmpty()) {
        m_lblResult->setText(UiKit::warnHtml(tr("La rectificativa %1 de este ticket está pendiente de envío a la AEAT: "
                                                "espere a que se registre antes de rectificarlo de nuevo.")
                                                 .arg(pending.toHtmlEscaped())));
    } else if (estadoEnum == VerifactuEstado::Enviada) {
        m_loadedTicket        = ticketNum;
        m_loadedCliente       = cliente;
        m_loadedImporteTotal  = importe;
        // Sensible default: in S mode, prefill with the original total so the user
        // adjusts down; in I mode, leave at 0.
        m_sbAmount->setValue(m_rbDifferences->isChecked() ? 0.00 : importe);
        setFormEnabled(true);
    } else {
        const QString reason = (estadoEnum == VerifactuEstado::Anulada)     ? "Este ticket está anulado en AEAT - no se puede rectificar."
                             : (estadoEnum == VerifactuEstado::Rectificada) ? "Este ticket ya fue rectificado previamente."
                             // AEAT, 13-10-2026: a refused record still exists in the system; it is resent
                             // corrected (same number), and a rectificativa follows if the amount or date change.
                             : (estadoEnum == VerifactuEstado::Error)       ? "La AEAT rechazó esta factura. Reenvíela primero (Recogida de Prendas, "
                                                                              "botón Verifactu\u2026, Reintentar); cuando la AEAT la acepte, si hay que "
                                                                              "cambiar el importe o la fecha, rectifíquela aquí."
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

    // The rectificativa row is income dated on this day: never before the original
    // payment (its subtraction would land in a period that never counted it), and
    // never into a closed quarter.
    const QDate rectifyDate = m_deRectifyDate->date();
    const QDate lastPayment = ticketLastPaymentDate(db, m_loadedTicket);
    if (lastPayment.isValid() && rectifyDate < lastPayment) {
        m_lblResult->setText(QString("<b style='color:red'>La fecha de la rectificativa no puede ser anterior "
                                     "al cobro del ticket original (%1).</b>").arg(lastPayment.toString("dd-MM-yyyy")));
        return;
    }
    if (quarterIsClosed(db, rectifyDate)) {
        m_lblResult->setText("<b style='color:red'>La fecha de la rectificativa pertenece a un trimestre "
                             "con la contabilidad cerrada. Elija una fecha de un periodo abierto.</b>");
        return;
    }

    const double ivaRate    = AppSettings::instance()->ivaRate();
    const double divisor    = 1.0 + ivaRate / 100.0;
    const double newTaxBase   = amountWithIva / divisor;
    const double newTaxAmount = amountWithIva - newTaxBase;

    // For substitution we send the ORIGINAL values as RectificationTaxBase/Amount.
    const double origTaxBase   = m_loadedImporteTotal / divisor;
    const double origTaxAmount = m_loadedImporteTotal - origTaxBase;

    // Refusals come before the number is claimed and the placeholder row inserted:
    // that row is income and would be offered for resubmission as a plain invoice.
    const auto invoiceType = invoiceTypeFromIndex(m_cbInvoiceType->currentIndex());
    // The shop only issues simplified invoices: the direct client corrects them with R5.
    if (m_verifactu->directBackend() && invoiceType != VerifactuInvoice::RECTIFICATION_R5) {
        m_lblResult->setText(UiKit::errorHtml("Con la conexión directa con la AEAT la rectificativa de un ticket "
                                              "(factura simplificada) es siempre R5."));
        return;
    }
    if (!m_verifactu->isConfigured()) {
        m_lblResult->setText(UiKit::errorHtml("Verifactu no está configurado: no se ha creado la rectificativa."));
        return;
    }
    if (const QString blocked = m_verifactu->invoicingBlockedReason(); !blocked.isEmpty()) {
        m_lblResult->setText(UiKit::errorHtml(blocked));
        return;
    }

    setFormEnabled(false);
    m_lblResult->setText("Enviando rectificativa a AEAT...");

    // Lazy-wire the signal on first use - m_verifactu is set by MainWindow after construction.
    connect(m_verifactu, &VerifactuIntegration::requestFinished,
            this, &RectifyInvoiceDialog::onVerifactuRequestFinished, Qt::UniqueConnection);

    // The number is claimed (placeholder row) before the round-trip; a refused one is
    // resent under its own number (AEAT, 13-10-2026), never beside a second one.
    const QString refused = refusedRectificationOf(db, m_loadedTicket);
    if (!refused.isEmpty() && m_verifactu->directBackend()
        && m_verifactu->directBackend()->holdsPendingRecord(AeatStore::Kind::Registration, refused)) {
        m_lblResult->setText(UiKit::warnHtml(tr("La rectificativa %1 de este ticket está pendiente de envío a la AEAT: "
                                                "se enviará sola.").arg(refused.toHtmlEscaped())));
        setFormEnabled(true);
        return;
    }
    m_newInvoiceNumber = refused.isEmpty() ? QString::number(nextTicketNumber(db, m_verifactu->highestNumberAtAeat()))
                                           : refused;
    m_newInvoiceDate   = m_deRectifyDate->date();
    m_submittedAmount  = amountWithIva;
    m_submittedIsSubstitution = isSubstitution;
    m_submittedTicket = m_loadedTicket;
    if (refused.isEmpty()) {
        if (!insertPlaceholderRow()) {
            m_lblResult->setText(UiKit::errorHtml(tr("No se pudo guardar el nuevo ticket %1 de la rectificativa: no se ha "
                                                     "enviado nada a la AEAT.").arg(m_newInvoiceNumber.toHtmlEscaped())));
            setFormEnabled(true);
            return;
        }
    } else if (!prepareRefusedResend(refused, amountWithIva, isSubstitution)) {
        setFormEnabled(true);
        return;
    }

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

    // The corrected invoice as AEAT holds it: from its stored record, else the event's
    // own InvoiceID and earliest payment date.
    QString rectifiedNumber = verifactuInvoiceId(m_loadedTicket, 0);
    QDate rectifiedDate = QDate::fromString(verifactuEventFor(db, m_loadedTicket, 0).fechaPago, "dd-MM-yyyy");
    const AeatResponse::RecordSummary stored = AeatResponse::summarizeRecord(verifactuEventRecordXml(db, m_loadedTicket, 0));
    if (stored.valid) {
        rectifiedNumber = stored.invoiceNumber;
        rectifiedDate = QDate::fromString(stored.issueDate, "dd-MM-yyyy");
    }
    m_pendingRectifyId = m_verifactu->submitRectificationAsync(
        m_newInvoiceNumber, m_newInvoiceDate, invoiceType, rectType,
        newTaxBase, newTaxAmount, origTaxBase, origTaxAmount,
        ivaRate, desc, rectifiedNumber, rectifiedDate);

    // No other ticket can be searched while this one waits for AEAT.
    m_leTicketNum->setEnabled(m_pendingRectifyId.isEmpty());
    m_btnSearch->setEnabled(m_pendingRectifyId.isEmpty());
    if (m_pendingRectifyId.isEmpty()) {
        // Nothing was sent: its row (claimed or reused) is an error to redo, not PENDIENTE.
        markRectificationUnsent(db, m_newInvoiceNumber, tr("No se envió: Verifactu no configurado."));
        m_lblResult->setText(
            QString("<b style='color:red'>Verifactu no configurado:</b> %1")
            .arg(m_verifactu->getLastError()));
        setFormEnabled(true);
    }
}

bool RectifyInvoiceDialog::prepareRefusedResend(const QString &refused, double amountWithIva, bool isSubstitution)
{
    db.open();
    QSqlQuery old(db);
    old.prepare("SELECT MIN(fecha_pago), SUM(importe), MIN(verifactu_rectification_type) FROM ingresos "
                "WHERE n_recibo = :n AND verifactu_rectifies_n_recibo = :o");
    old.bindValue(":n", refused);
    old.bindValue(":o", m_submittedTicket);
    const bool read = old.exec() && old.next();
    const QDate oldDate = read ? QDate::fromString(old.value(0).toString(), "dd-MM-yyyy") : QDate();
    const double oldAmount = read ? old.value(1).toDouble() : 0.0;
    const bool oldSubstitution = read && old.value(2).toString() == QLatin1String("S");
    old.finish();
    db.close();
    if (!read || !oldDate.isValid()) {                              // never rewrite a row we cannot read
        m_lblResult->setText(UiKit::errorHtml(tr("No se pudo leer la rectificativa rechazada %1: no se ha enviado nada.")
                                                  .arg(refused.toHtmlEscaped())));
        return false;
    }
    if (!quarterIsClosed(db, oldDate)) {
        if (reusePlaceholderRow())
            return true;
        m_lblResult->setText(UiKit::errorHtml(tr("No se pudo preparar el reenvío de la rectificativa %1: no se ha "
                                                 "enviado nada.").arg(refused.toHtmlEscaped())));
        return false;
    }
    // Counted in a quarter closed since: resent only as it was filed, its row untouched.
    if (qAbs(oldAmount - amountWithIva) > 0.005 || oldSubstitution != isSubstitution) {
        m_lblResult->setText(UiKit::errorHtml(tr("La rectificativa rechazada %1 es de un trimestre cerrado: solo "
                                                 "puede reenviarse como se registró (%2, %3).")
                                                  .arg(refused.toHtmlEscaped(), displayMoney(oldAmount),
                                                       oldSubstitution ? tr("por sustitución") : tr("por diferencias"))));
        return false;
    }
    if (!markRectificationResent(db, refused)) {                    // PENDIENTE while it is sent
        m_lblResult->setText(UiKit::errorHtml(tr("No se pudo preparar el reenvío de la rectificativa %1: no se ha "
                                                 "enviado nada.").arg(refused.toHtmlEscaped())));
        return false;
    }
    m_newInvoiceDate = oldDate;
    return true;
}

RectifyInvoiceDialog::~RectifyInvoiceDialog()
{
    // Nobody else would apply the answer (with the gateway, or the direct client before
    // its record exists): the row would stay PENDIENTE where nothing resends a
    // rectificativa. As an error it is counted and redone under its number.
    if (m_pendingRectifyId.isEmpty() || !m_verifactu)
        return;
    if (m_verifactu->directBackend()
        && m_verifactu->directBackend()->holdsPendingRecord(AeatStore::Kind::Registration, m_newInvoiceNumber))
        return;                                                     // it sends itself and settles
    qWarning() << "RectifyInvoiceDialog: closed before AEAT answered rectificativa" << m_newInvoiceNumber;
    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET verifactu_estado = :e, verifactu_error = :err "
              "WHERE n_recibo = :n AND verifactu_rectifies_n_recibo = :o AND verifactu_estado = :pending");
    q.bindValue(":e",       verifactuEstadoToString(VerifactuEstado::Error));
    q.bindValue(":err",     tr("Sin respuesta de la AEAT al cerrar la ventana: rehágala en Rectificar factura."));
    q.bindValue(":n",       m_newInvoiceNumber);
    q.bindValue(":o",       m_submittedTicket);
    q.bindValue(":pending", verifactuEstadoToString(VerifactuEstado::NotSubmitted));
    q.exec();
    db.close();
}

void RectifyInvoiceDialog::done(int result)
{
    if (!m_pendingRectifyId.isEmpty()) {
        m_lblResult->setText(UiKit::warnHtml(tr("Esperando la respuesta de la AEAT: la ventana se cerrará o se podrá cerrar en cuanto responda.")));
        return;
    }
    QDialog::done(result);
}

void RectifyInvoiceDialog::onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result)
{
    if (requestId != m_pendingRectifyId) return; // not ours
    m_pendingRectifyId.clear();
    m_leTicketNum->setEnabled(true);
    m_btnSearch->setEnabled(true);

    const bool localOk = applyRectificationResult(result);

    if (result.isSuccess() && localOk) {
        m_lblResult->setText(
            QString("<b style='color:green'>Rectificativa enviada.</b><br>"
                    "Nuevo ticket: %1<br>CSV: %2")
            .arg(m_newInvoiceNumber,
                 result.csv.isEmpty() ? "(sin CSV)" : result.csv));
        m_loadedTicket.clear();
        m_leTicketNum->clear();
        m_lblInfo->setText("-");
    } else if (result.isSuccess()) {
        // AEAT accepted but the local mark failed: keep the red message set by
        // applyRectificationResult, and do not offer to rectify the same ticket again.
        m_loadedTicket.clear();
        m_leTicketNum->clear();
    } else if (queuedWithoutAnswer(result)) {
        // The direct client stored and chained it: it goes out on its own (and the
        // original is marked when AEAT accepts it). Never rectified a second time.
        m_lblResult->setText(UiKit::warnHtml(tr("Rectificativa %1 guardada, pendiente de envío a la AEAT: %2")
                                                 .arg(m_newInvoiceNumber.toHtmlEscaped(), result.errorDescription.toHtmlEscaped()))
                             + "<br>" + tr("Se enviará sola; no la repita."));
        m_loadedTicket.clear();
        m_leTicketNum->clear();
    } else {
        m_lblResult->setText(
            QString("<b style='color:red'>Error al rectificar:</b> %1")
            .arg(result.errorDescription));
        setFormEnabled(true);
    }
}

bool RectifyInvoiceDialog::queuedWithoutAnswer(const VerifactuResult &result) const
{
    // Only when the record was generated: before the first chain sync none is, and the
    // row is an error to redo (under its number), not something that sends itself.
    return m_verifactu && m_verifactu->directBackend()
           && (result.status == VerifactuResult::NETWORK_ERROR || result.status == VerifactuResult::PENDING)
           && m_verifactu->directBackend()->holdsPendingRecord(AeatStore::Kind::Registration, m_newInvoiceNumber);
}

bool RectifyInvoiceDialog::insertPlaceholderRow()
{
    const QString fechaStr = m_newInvoiceDate.toString("dd-MM-yyyy");
    const QString modeStr  = VerifactuInvoice::rectificationTypeToString(
        m_submittedIsSubstitution ? VerifactuInvoice::BY_SUBSTITUTION
                                  : VerifactuInvoice::BY_DIFFERENCES);
    const QString hash = genHash16();

    qDebug() << "RectifyInvoiceDialog::insertPlaceholderRow:"
             << "newTicket=" << m_newInvoiceNumber
             << "rectifies=" << m_submittedTicket
             << "mode=" << modeStr
             << "amount=" << m_submittedAmount;

    db.open();
    QSqlQuery q(db);
    // Deliberately a custom INSERT, not sql_lite::insertGarmentRow: a rectificativa
    // row writes the full verifactu_* set (csv/timestamp/error/url_qr/xml/hash +
    // rectifies_n_recibo/rectification_type) that IngresoGarmentRow does not carry.
    q.prepare(
        "INSERT INTO ingresos "
        "(n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, "
        "cantidad, prenda, size, servicio, observaciones, edit_lock, hash, "
        "verifactu_csv, verifactu_timestamp, verifactu_estado, verifactu_error, verifactu_url_qr, "
        "verifactu_xml, verifactu_hash, "
        "verifactu_rectifies_n_recibo, verifactu_rectification_type) "
        "VALUES "
        "(:n_recibo, :cliente, :fecha_recepcion, :fecha_pago, :fecha_recogida, :importe, :pagado, :estado, "
        ":cantidad, :prenda, :size, :servicio, :observaciones, '0', :hash, "
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
    q.bindValue(":prenda",          QString("Rectificativa de ticket %1").arg(m_submittedTicket));
    q.bindValue(":size",            "");
    q.bindValue(":servicio",        "");
    q.bindValue(":observaciones",   QString("Rectificativa %1 (%2)").arg(
                                        VerifactuInvoice::isRectificationInvoiceType(
                                            invoiceTypeFromIndex(m_cbInvoiceType->currentIndex()))
                                            ? m_cbInvoiceType->currentText().left(2)
                                            : QString("R1"),
                                        modeStr));
    q.bindValue(":hash",            hash);
    // Placeholder: AEAT metadata patched by applyRectificationResult() on reply.
    q.bindValue(":vcsv",            "");
    q.bindValue(":vts",             "");
    q.bindValue(":vestado",         verifactuEstadoToString(VerifactuEstado::NotSubmitted));
    q.bindValue(":verror",          "");
    q.bindValue(":vurl",            "");
    q.bindValue(":vxml",            "");
    q.bindValue(":vhash",           "");
    q.bindValue(":vrectifies",      m_submittedTicket);
    q.bindValue(":vrecttype",       modeStr);
    const bool ok = q.exec();
    if (!ok)
        qWarning() << "RectifyInvoiceDialog: INSERT placeholder rectificativa row failed for new ticket"
                   << m_newInvoiceNumber << "-" << q.lastError().text();
    db.close();
    return ok;
}

bool RectifyInvoiceDialog::reusePlaceholderRow()
{
    qDebug() << "RectifyInvoiceDialog: resending refused rectificativa" << m_newInvoiceNumber << "of" << m_submittedTicket;
    const QString fechaStr = m_newInvoiceDate.toString("dd-MM-yyyy");
    db.open();
    QSqlQuery q(db);
    q.prepare("UPDATE ingresos SET fecha_recepcion = :f, fecha_pago = :f, importe = :imp, "
              "verifactu_rectification_type = :t, verifactu_estado = :e, verifactu_error = '' "
              "WHERE n_recibo = :n AND verifactu_rectifies_n_recibo = :o");
    q.bindValue(":f",   fechaStr);
    q.bindValue(":imp", QString::number(m_submittedAmount, 'f', 2));
    q.bindValue(":t",   VerifactuInvoice::rectificationTypeToString(
                            m_submittedIsSubstitution ? VerifactuInvoice::BY_SUBSTITUTION : VerifactuInvoice::BY_DIFFERENCES));
    q.bindValue(":e",   verifactuEstadoToString(VerifactuEstado::NotSubmitted));
    q.bindValue(":n",   m_newInvoiceNumber);
    q.bindValue(":o",   m_submittedTicket);
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (!ok)
        qWarning() << "RectifyInvoiceDialog: UPDATE refused rectificativa row failed for" << m_newInvoiceNumber
                   << "-" << q.lastError().text();
    db.close();
    return ok;
}

bool RectifyInvoiceDialog::applyRectificationResult(const VerifactuResult &result)
{
    bool localOk = true;
    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    const VerifactuEstado estado = result.isSuccess()         ? VerifactuEstado::Enviada
                                 : queuedWithoutAnswer(result) ? VerifactuEstado::NotSubmitted
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
        q.bindValue(":estado", verifactuEstadoToString(estado));
        q.bindValue(":error",  result.errorDescription);
        q.bindValue(":url",    "");
        q.bindValue(":xml",    "");
        q.bindValue(":hash",   "");
        q.bindValue(":id",     "");
    }
    q.bindValue(":num",  m_newInvoiceNumber);
    q.bindValue(":orig", m_submittedTicket);
    // A row that is not there (0 rows changed) is a failure too, not a silent success.
    if (!q.exec() || q.numRowsAffected() == 0) {
        qWarning() << "RectifyInvoiceDialog: UPDATE placeholder rectificativa row failed for new ticket"
                   << m_newInvoiceNumber << "-" << q.lastError().text();
        if (result.isSuccess()) {
            localOk = false;
            m_lblResult->setText(QString("<b style='color:red'>La AEAT aceptó la rectificativa, pero no se pudo guardar "
                                         "el nuevo ticket %1 en la base de datos local. Revise el log de depuración "
                                         "antes de hacer la contabilidad.</b>").arg(m_newInvoiceNumber.toHtmlEscaped()));
        }
    }

    // For substitution (S), mark the original rows as RECTIFICADA so they no longer
    // count toward accounting. Differences (I) leaves the original untouched - the
    // delta row alone reconciles the books. Only on AEAT success.
    if (result.isSuccess() && m_submittedIsSubstitution) {
        qDebug() << "RectifyInvoiceDialog: marking RECTIFICADA ticket" << m_submittedTicket;
        // Into an open period (a refused one from a closed quarter is resent with its date).
        const QDate rectifiedOn = openPeriodDate(db, m_newInvoiceDate);
        if (!markTicketRectified(db, m_submittedTicket, rectifiedOn)) {
            localOk = false;
            m_lblResult->setText(QString("<b style='color:red'>La AEAT aceptó la rectificativa, pero no se pudo marcar "
                                         "el ticket %1 como RECTIFICADA en la base de datos local. Revise el log "
                                         "de depuración antes de hacer la contabilidad.</b>").arg(m_submittedTicket.toHtmlEscaped()));
        }
    }
    db.close();
    return localOk;
}
