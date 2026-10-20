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
#include "uikit.h"
#include "aeatresponse.h"

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
    UiKit::setUpDialog(this, tr("Factura rectificativa Verifactu"));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(UiKit::introPanel(tr(
        "Corrige en la AEAT una factura ya registrada con una factura rectificativa, que lleva su propio "
        "número de ticket. Busque el ticket original, rellene los datos y envíela.")));

    // Search row -----------------------------------------------------------
    QHBoxLayout *searchRow = new QHBoxLayout();
    searchRow->addWidget(new QLabel(tr("Número de ticket original:")));
    m_leTicketNum = new QLineEdit();
    m_leTicketNum->setObjectName("leTicketNum");   // stable names for the e2e test bench
    m_leTicketNum->setPlaceholderText(tr("Ej: 24417"));
    searchRow->addWidget(m_leTicketNum);
    m_btnSearch = UiKit::secondaryButton(tr("Buscar"), "btnSearch");
    searchRow->addWidget(m_btnSearch);
    layout->addLayout(searchRow);

    // Ticket info panel ----------------------------------------------------
    m_lblInfo = new QLabel("-");
    m_lblInfo->setWordWrap(true);
    m_lblInfo->setFrameShape(QFrame::StyledPanel);
    m_lblInfo->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(m_lblInfo);

    // Rectification form ---------------------------------------------------
    m_grpForm = new QGroupBox(tr("Datos de la rectificativa"));
    QVBoxLayout *formLayout = new QVBoxLayout(m_grpForm);

    QHBoxLayout *rowType = new QHBoxLayout();
    rowType->addWidget(new QLabel(tr("Tipo:")));
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
    rowMode->addWidget(new QLabel(tr("Modo:")));
    m_rbDifferences  = new QRadioButton(tr("Por diferencias (I)"));
    m_rbSubstitution = new QRadioButton(tr("Por sustitución (S)"));
    m_rbSubstitution->setObjectName("rbSubstitution");
    m_rbDifferences->setObjectName("rbDifferences");
    m_rbDifferences->setChecked(true);
    rowMode->addWidget(m_rbDifferences);
    rowMode->addWidget(m_rbSubstitution);
    rowMode->addStretch();
    formLayout->addLayout(rowMode);

    QHBoxLayout *rowDate = new QHBoxLayout();
    rowDate->addWidget(new QLabel(tr("Fecha rectificativa:")));
    m_deRectifyDate = UiKit::dateEdit(QDate::currentDate(), "deRectifyDate");
    m_deRectifyDate->setMaximumDate(QDate::currentDate());   // AEAT refuses a future invoice date
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
    rowDesc->addWidget(new QLabel(tr("Motivo:")));
    m_leDescription = new QLineEdit();
    m_leDescription->setPlaceholderText(tr("Opcional - descripción visible en la rectificativa"));
    rowDesc->addWidget(m_leDescription);
    formLayout->addLayout(rowDesc);

    layout->addWidget(m_grpForm);
    m_grpForm->setEnabled(false);

    // Submit + result ------------------------------------------------------
    // Not a primaryButton: Enter in a field never sends a rectificativa (11.1).
    m_btnRectify = UiKit::secondaryButton(tr("Enviar rectificativa a AEAT"), "btnRectify");
    m_btnRectify->setEnabled(false);
    auto *actionRow = new QHBoxLayout();
    actionRow->addStretch();
    actionRow->addWidget(m_btnRectify);
    layout->addLayout(actionRow);

    m_lblResult = new UiKit::ResultPanel();
    layout->addWidget(m_lblResult);
    layout->addLayout(UiKit::closeRow(this, tr("Cerrar")));

    // Wiring ---------------------------------------------------------------
    connect(m_btnSearch,      &QPushButton::clicked,     this, &RectifyInvoiceDialog::onSearchClicked);
    UiKit::onEnter(m_leTicketNum, [this]() { onSearchClicked(); });
    connect(m_btnRectify,     &QPushButton::clicked,     this, &RectifyInvoiceDialog::onRectifyClicked);
    connect(m_rbDifferences,  &QRadioButton::toggled,    this, &RectifyInvoiceDialog::onRectificationModeChanged);
    connect(m_rbSubstitution, &QRadioButton::toggled,    this, &RectifyInvoiceDialog::onRectificationModeChanged);

    onRectificationModeChanged(); // set initial label
}

void RectifyInvoiceDialog::onRectificationModeChanged()
{
    if (m_rbSubstitution->isChecked())
        m_lblAmount->setText(tr("Importe TOTAL corregido (con IVA):"));
    else
        m_lblAmount->setText(tr("Diferencia de importe (delta, con IVA):"));
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

    QString cliente, fecha;
    bool hasPartialPays = false;
    {
        ConnectionScope scope(db);
        QSqlQuery q(db);
        q.prepare("SELECT MIN(cliente), MIN(fecha_recepcion) FROM ingresos WHERE n_recibo = :num");
        q.bindValue(":num", ticketNum);
        if (!q.exec() || !q.first() || q.value(0).isNull()) {
            m_lblInfo->setText(tr("<i>Ticket no encontrado.</i>"));
            return;
        }
        cliente = q.value(0).toString();
        fecha   = q.value(1).toString();
        q.finish();

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
        hasPartialPays = seqQ.exec() && seqQ.first() && seqQ.value(0).toInt() > 0;
        seqQ.finish();
    }

    if (hasPartialPays) {
        m_lblInfo->setText(
            tr("<b>Cliente:</b> %1<br><b>Fecha:</b> %2<br>"
               "<i>Este ticket tiene varios pagos parciales. La rectificación "
               "individual de cada pago no está disponible en esta versión; "
               "rectificar desde la sede electrónica de la AEAT.</i>")
            .arg(cliente.toHtmlEscaped(), fecha.toHtmlEscaped()));
        return;
    }

    // The invoice is the seq-0 payment event (its paid rows only): unpaid garments of the
    // ticket share n_recibo but were never part of it.
    QVector<SubmittedInvoiceEvent> events;
    if (!submittedInvoiceEvents(db, ticketNum, events)) {
        m_lblInfo->setText(tr("<i>Error al leer el ticket.</i>"));
        return;
    }
    SubmittedInvoiceEvent invoice;
    for (const SubmittedInvoiceEvent &e : events)
        if (e.seq == 0)
            invoice = e;
    const double  importe = invoice.importe;
    const QString csv     = invoice.csv;
    const QString estado  = invoice.estado;

    m_lblInfo->setText(
        tr("<b>Cliente:</b> %1<br>"
           "<b>Fecha:</b> %2<br>"
           "<b>Importe total:</b> %3 €<br>"
           "<b>CSV Verifactu:</b> %4<br>"
           "<b>Estado:</b> %5")
        .arg(cliente.toHtmlEscaped(), fecha.toHtmlEscaped(),
             displayMoney(importe),
             csv.isEmpty() ? QStringLiteral("-") : csv.toHtmlEscaped(),
             verifactuEstadoDisplay(estado).toHtmlEscaped()));

    const VerifactuEstado estadoEnum = verifactuEstadoFromString(estado);
    const QString pending = pendingRectificationOf(db, ticketNum);
    const bool cancellationPending = m_verifactu && m_verifactu->holdsPendingCancellation(verifactuInvoiceId(ticketNum, 0));
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
        const QString reason = (estadoEnum == VerifactuEstado::Anulada)     ? tr("Este ticket está anulado en AEAT - no se puede rectificar.")
                             : (estadoEnum == VerifactuEstado::Rectificada) ? tr("Este ticket ya fue rectificado previamente.")
                             : (estadoEnum == VerifactuEstado::Unknown)     ? tr("El estado Verifactu de este ticket no es válido: "
                                                                                 "revíselo antes de rectificarlo.")
                             // AEAT, 13-10-2026: a refused record still exists in the system; it is resent
                             // corrected (same number), and a rectificativa follows if the amount or date change.
                             : (estadoEnum == VerifactuEstado::Error)       ? tr("La AEAT rechazó esta factura. Reenvíela primero (Recogida de Prendas, "
                                                                                 "botón Verifactu\u2026, Reintentar); cuando la AEAT la acepte, si hay que "
                                                                                 "cambiar el importe o la fecha, rectifíquela aquí.")
                                                                            : tr("Este ticket no fue enviado a AEAT - no hay nada que rectificar.");
        m_lblResult->setText(UiKit::warnHtml(reason));
    }
}

void RectifyInvoiceDialog::onRectifyClicked()
{
    if (m_loadedTicket.isEmpty() || !m_verifactu) return;
    if (!m_pendingRectifyId.isEmpty()) return; // already in flight

    const double amountWithIva = m_sbAmount->value();
    const bool   isSubstitution = m_rbSubstitution->isChecked();

    if (isSubstitution && amountWithIva <= 0.0) {
        m_lblResult->setText(UiKit::errorHtml(tr("El importe corregido total debe ser mayor que 0.")));
        return;
    }
    if (!isSubstitution && qFuzzyIsNull(amountWithIva)) {
        m_lblResult->setText(UiKit::errorHtml(tr("La diferencia no puede ser 0.")));
        return;
    }
    // A difference never takes the invoice to 0 or below: that is a cancellation (Anular).
    if (!isSubstitution) {
        bool read = false;
        const double earlier = rectifiedDifferences(db, m_loadedTicket, &read);
        const double corrected = m_loadedImporteTotal + earlier + amountWithIva;
        if (!read || corrected <= 0.005) {
            m_lblResult->setText(UiKit::errorHtml(read
                ? tr("Con esta diferencia la factura quedaría en %1 €: debe seguir siendo mayor que 0.")
                      .arg(displayMoney(corrected))
                : tr("No se pudieron leer las rectificativas anteriores del ticket."))
                + "<br>" + tr("Para dejarla sin efecto, use Anular factura."));
            return;
        }
    }

    // The rectificativa row is income dated on this day: never before the original
    // payment (its subtraction would land in a period that never counted it), and
    // never into a closed quarter.
    const QDate rectifyDate = m_deRectifyDate->date();
    const QDate lastPayment = ticketLastPaymentDate(db, m_loadedTicket);
    if (lastPayment.isValid() && rectifyDate < lastPayment) {
        m_lblResult->setText(UiKit::errorHtml(tr("La fecha de la rectificativa no puede ser anterior "
                                                 "al cobro del ticket original (%1).").arg(lastPayment.toString("dd-MM-yyyy"))));
        return;
    }
    if (quarterIsClosed(db, rectifyDate)) {
        m_lblResult->setText(UiKit::errorHtml(tr("La fecha de la rectificativa pertenece a un trimestre "
                                                 "con la contabilidad cerrada. Elija una fecha de un periodo abierto.")));
        return;
    }

    const double ivaRate    = AppSettings::instance()->ivaRate();
    const GrossSplit newSplit = splitGross(amountWithIva, ivaRate);
    const double newTaxBase   = newSplit.base;
    const double newTaxAmount = newSplit.tax;

    // For substitution we send the ORIGINAL values as RectificationTaxBase/Amount.
    const GrossSplit origSplit = splitGross(m_loadedImporteTotal, ivaRate);
    const double origTaxBase   = origSplit.base;
    const double origTaxAmount = origSplit.tax;

    // Refusals come before the number is claimed and the placeholder row inserted:
    // that row is income and would be offered for resubmission as a plain invoice.
    const auto invoiceType = invoiceTypeFromIndex(m_cbInvoiceType->currentIndex());
    // The shop only issues simplified invoices: the direct client corrects them with R5.
    if (m_verifactu->isDirect() && invoiceType != VerifactuInvoice::RECTIFICATION_R5) {
        m_lblResult->setText(UiKit::errorHtml(tr("Con la conexión directa con la AEAT la rectificativa de un ticket "
                                                 "(factura simplificada) es siempre R5.")));
        return;
    }
    if (!m_verifactu->isConfigured()) {
        m_lblResult->setText(UiKit::errorHtml(tr("Verifactu no está configurado: no se ha creado la rectificativa.")));
        return;
    }
    if (const QString blocked = m_verifactu->invoicingBlockedReason(); !blocked.isEmpty()) {
        m_lblResult->setText(UiKit::errorHtml(blocked.toHtmlEscaped()));
        return;
    }

    setFormEnabled(false);
    m_lblResult->setText(tr("Enviando rectificativa a AEAT..."));

    // Lazy-wire the signal on first use - m_verifactu is set by MainWindow after construction.
    connect(m_verifactu, &VerifactuIntegration::requestFinished,
            this, &RectifyInvoiceDialog::onVerifactuRequestFinished, Qt::UniqueConnection);

    // The number is claimed (placeholder row) before the round-trip; a refused one is
    // resent under its own number (AEAT, 13-10-2026), never beside a second one.
    const QString refused = refusedRectificationOf(db, m_loadedTicket);
    if (!refused.isEmpty() && m_verifactu->holdsPendingRegistration(refused)) {
        m_lblResult->setText(UiKit::warnHtml(tr("La rectificativa %1 de este ticket está pendiente de envío a la AEAT: "
                                                "se enviará sola.").arg(refused.toHtmlEscaped())));
        setFormEnabled(true);
        return;
    }
    bool numbered = true;
    m_newInvoiceNumber = refused.isEmpty() ? QString::number(nextTicketNumber(db, m_verifactu->highestNumberAtAeat(), &numbered))
                                           : refused;
    if (!numbered) {
        m_lblResult->setText(UiKit::errorHtml(tr("No se pudo leer el último número de ticket de la base de datos: no se "
                                                 "ha enviado nada a la AEAT.")));
        setFormEnabled(true);
        return;
    }
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
        m_lblResult->setText(UiKit::errorHtml(tr("Verifactu no configurado:")) + " "
                             + m_verifactu->getLastError().toHtmlEscaped());
        setFormEnabled(true);
    }
}

bool RectifyInvoiceDialog::prepareRefusedResend(const QString &refused, double amountWithIva, bool isSubstitution)
{
    bool read = false;
    QDate oldDate;
    double oldAmount = 0.0;
    bool oldSubstitution = false;
    {
        ConnectionScope scope(db);
        QSqlQuery old(db);
        old.prepare("SELECT MIN(fecha_pago), SUM(importe), MIN(verifactu_rectification_type) FROM ingresos "
                    "WHERE n_recibo = :n AND verifactu_rectifies_n_recibo = :o");
        old.bindValue(":n", refused);
        old.bindValue(":o", m_submittedTicket);
        read = old.exec() && old.next();
        if (read) {
            oldDate = QDate::fromString(old.value(0).toString(), "dd-MM-yyyy");
            oldAmount = old.value(1).toDouble();
            oldSubstitution = old.value(2).toString() == QLatin1String("S");
        }
        old.finish();
    }
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
    if (m_verifactu->holdsPendingRegistration(m_newInvoiceNumber))
        return;                                                     // it sends itself and settles
    qWarning() << "RectifyInvoiceDialog: closed before AEAT answered rectificativa" << m_newInvoiceNumber;
    markRectificativaUnanswered(db, m_newInvoiceNumber, m_submittedTicket,
                                tr("Sin respuesta de la AEAT al cerrar la ventana: rehágala en Rectificar factura."));
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
        m_lblResult->setText(UiKit::okHtml(tr("Rectificativa enviada."))
                             + "<br>" + tr("Nuevo ticket: %1<br>CSV: %2")
                                            .arg(m_newInvoiceNumber.toHtmlEscaped(),
                                                 result.csv.isEmpty() ? tr("(sin CSV)") : result.csv.toHtmlEscaped()));
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
    } else if (result.status == VerifactuResult::PENDING) {
        // A gateway "success" without AEAT's CSV: AEAT may hold it. Check before redoing it.
        m_lblResult->setText(UiKit::warnHtml(tr("La rectificativa %1 no se ha confirmado: %2")
                                                 .arg(m_newInvoiceNumber.toHtmlEscaped(), result.errorDescription.toHtmlEscaped()))
                             + "<br>" + tr("La AEAT puede tenerla: compruébelo con \"Consultar en AEAT\" en Recogida de "
                                           "Prendas antes de rehacerla."));
        setFormEnabled(true);
    } else {
        m_lblResult->setText(UiKit::errorHtml(tr("Error al rectificar:")) + " "
                             + result.errorDescription.toHtmlEscaped());
        setFormEnabled(true);
    }
}

bool RectifyInvoiceDialog::queuedWithoutAnswer(const VerifactuResult &result) const
{
    // Only when the record was generated: before the first chain sync none is, and the
    // row is an error to redo (under its number), not something that sends itself.
    return m_verifactu
           && (result.status == VerifactuResult::NETWORK_ERROR || result.status == VerifactuResult::PENDING)
           && m_verifactu->holdsPendingRegistration(m_newInvoiceNumber);
}

RectificativaRow RectifyInvoiceDialog::rectificativaRow() const
{
    RectificativaRow row;
    row.number   = m_newInvoiceNumber;
    row.original = m_submittedTicket;
    row.client   = m_loadedCliente;
    row.type     = VerifactuInvoice::rectificationTypeToString(
        m_submittedIsSubstitution ? VerifactuInvoice::BY_SUBSTITUTION : VerifactuInvoice::BY_DIFFERENCES);
    row.label    = VerifactuInvoice::isRectificationInvoiceType(invoiceTypeFromIndex(m_cbInvoiceType->currentIndex()))
                       ? m_cbInvoiceType->currentText().left(2) : QStringLiteral("R1");
    row.date     = m_newInvoiceDate;
    row.amount   = m_submittedAmount;
    return row;
}

bool RectifyInvoiceDialog::insertPlaceholderRow()
{
    qDebug() << "RectifyInvoiceDialog::insertPlaceholderRow: newTicket=" << m_newInvoiceNumber
             << "rectifies=" << m_submittedTicket << "amount=" << m_submittedAmount;
    return insertRectificativaRow(db, rectificativaRow());
}

bool RectifyInvoiceDialog::reusePlaceholderRow()
{
    qDebug() << "RectifyInvoiceDialog: resending refused rectificativa" << m_newInvoiceNumber << "of" << m_submittedTicket;
    return resetRefusedRectificativa(db, rectificativaRow());
}

bool RectifyInvoiceDialog::applyRectificationResult(const VerifactuResult &result)
{
    bool localOk = true;
    // A gateway "success" without AEAT's CSV (PENDING) may be held by AEAT: PENDIENTE with
    // its reason, to check with Consultar en AEAT, never an error to redo under a new record.
    const VerifactuEstado estado = result.isSuccess()         ? VerifactuEstado::Enviada
                                 : queuedWithoutAnswer(result) || result.status == VerifactuResult::PENDING
                                                               ? VerifactuEstado::NotSubmitted
                                                               : VerifactuEstado::Error;

    qDebug() << "RectifyInvoiceDialog::applyRectificationResult:"
             << "newTicket=" << m_newInvoiceNumber
             << "estado="    << verifactuEstadoToString(estado)
             << "csv="       << (result.isSuccess() ? result.csv : QString())
             << "error="     << (result.isSuccess() ? QString() : result.errorDescription);

    // A row that is not there (0 rows changed) is a failure too when AEAT accepted it.
    const int changed = applyRectificativaResult(db, m_newInvoiceNumber, m_submittedTicket, result, estado);
    if (result.isSuccess() && changed <= 0) {
        localOk = false;
        m_lblResult->setText(UiKit::errorHtml(tr("La AEAT aceptó la rectificativa, pero no se pudo guardar "
                                                 "el nuevo ticket %1 en la base de datos local. Revise el log de "
                                                 "depuración antes de hacer la contabilidad.")
                                                  .arg(m_newInvoiceNumber.toHtmlEscaped())));
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
            m_lblResult->setText(UiKit::errorHtml(tr("La AEAT aceptó la rectificativa, pero no se pudo marcar "
                                                     "el ticket %1 como RECTIFICADA en la base de datos local. Revise "
                                                     "el log de depuración antes de hacer la contabilidad.")
                                                      .arg(m_submittedTicket.toHtmlEscaped())));
        }
    }
    return localOk;
}
