#include "add_garment.h"
#include "sql_lite.h"
#include "uikit.h"
#include "verifactutypes.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QDebug>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

// For the helpers outside the class: the same translation context as tr() inside it.
static QString trText(const char *text)
{
    return QCoreApplication::translate("AddGarment", text);
}

AddGarment::AddGarment(const QSqlDatabase &database, QWidget *parent) :
    QDialog(parent),
    db(database)
{
    setAttribute(Qt::WA_DeleteOnClose);
    UiKit::setUpDialog(this, tr("Añadir nuevas prendas"));
    buildUi();
    resetAllContents();
}

void AddGarment::buildUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(UiKit::introPanel(
        tr("Añade una prenda a un recibo que todavía no tiene prendas pagadas. Busque primero el "
           "recibo; si la prenda se añade como pagada, se envía a AEAT como la factura del recibo.")));

    // Search ---------------------------------------------------------------
    QHBoxLayout *searchRow = new QHBoxLayout();
    searchRow->addWidget(new QLabel(tr("Nº recibo:")));
    m_leNRecibo = new QLineEdit();
    m_leNRecibo->setObjectName("leNRecibo");   // stable names for the e2e test bench
    m_leNRecibo->setPlaceholderText(tr("Ej: 24417"));
    searchRow->addWidget(m_leNRecibo, 1);
    QPushButton *btnSearch = UiKit::secondaryButton(tr("Buscar"), "btnSearch");
    searchRow->addWidget(btnSearch);
    layout->addLayout(searchRow);

    // Receipt found ----------------------------------------------------------
    m_grpTicket = new QGroupBox(tr("Recibo"));
    QFormLayout *ticketForm = new QFormLayout(m_grpTicket);
    m_leCliente = new QLineEdit();
    m_leCliente->setObjectName("leCliente");
    m_leCliente->setReadOnly(true);
    ticketForm->addRow(tr("Cliente:"), m_leCliente);
    m_deFechaRecepcion = UiKit::dateEdit(QDate::currentDate(), "deFechaRecepcion");
    UiKit::lockDate(m_deFechaRecepcion);
    ticketForm->addRow(tr("Fecha recepción:"), m_deFechaRecepcion);
    layout->addWidget(m_grpTicket);

    // Garment ----------------------------------------------------------------
    m_grpGarment = new QGroupBox(tr("Prenda"));
    QFormLayout *garmentForm = new QFormLayout(m_grpGarment);
    m_cbPrenda = new QComboBox();
    m_cbPrenda->setObjectName("cbPrenda");
    m_cbPrenda->setEditable(true);
    garmentForm->addRow(tr("Prenda:"), m_cbPrenda);
    m_cbServicio = new QComboBox();
    m_cbServicio->setObjectName("cbServicio");
    m_cbServicio->addItems({ "Limp.", "Plan." });
    garmentForm->addRow(tr("Servicio:"), m_cbServicio);
    m_leCantidad = new QLineEdit();
    m_leCantidad->setObjectName("leCantidad");
    m_leCantidad->setValidator(new QIntValidator(1, 9999, m_leCantidad));
    garmentForm->addRow(tr("Cantidad:"), m_leCantidad);
    m_leSize = new QLineEdit();
    m_leSize->setObjectName("leSize");
    m_leSize->setPlaceholderText(tr("Solo prendas por m2"));
    garmentForm->addRow(tr("Tamaño (m2):"), m_leSize);
    m_leImporte = new QLineEdit();
    m_leImporte->setObjectName("leImporte");
    m_leImporte->setReadOnly(true);
    garmentForm->addRow(tr("Importe:"), m_leImporte);
    m_leObservaciones = new QLineEdit();
    m_leObservaciones->setObjectName("leObservaciones");
    m_leObservaciones->setPlaceholderText(tr("Opcional"));
    garmentForm->addRow(tr("Observaciones:"), m_leObservaciones);
    layout->addWidget(m_grpGarment);

    // Payment and pickup -----------------------------------------------------
    m_grpState = new QGroupBox(tr("Estado"));
    QFormLayout *stateForm = new QFormLayout(m_grpState);
    m_chkPagado = new QCheckBox(tr("Pagada"));
    m_chkPagado->setObjectName("chkPagado");
    m_deFechaPago = UiKit::dateEdit(QDate::currentDate(), "deFechaPago");
    QHBoxLayout *payRow = new QHBoxLayout();
    payRow->addWidget(m_chkPagado);
    payRow->addWidget(new QLabel(tr("el")));
    payRow->addWidget(m_deFechaPago);
    payRow->addStretch();
    stateForm->addRow(payRow);
    m_chkRecogido = new QCheckBox(tr("Recogida"));
    m_chkRecogido->setObjectName("chkRecogido");
    m_deFechaRecogida = UiKit::dateEdit(QDate::currentDate(), "deFechaRecogida");
    QHBoxLayout *pickupRow = new QHBoxLayout();
    pickupRow->addWidget(m_chkRecogido);
    pickupRow->addWidget(new QLabel(tr("el")));
    pickupRow->addWidget(m_deFechaRecogida);
    pickupRow->addStretch();
    stateForm->addRow(pickupRow);
    layout->addWidget(m_grpState);

    // Actions + result -------------------------------------------------------
    QHBoxLayout *actions = new QHBoxLayout();
    QPushButton *btnReset = UiKit::secondaryButton(tr("Limpiar formulario"), "btnReset");
    actions->addWidget(btnReset);
    actions->addStretch();
    QPushButton *btnSave = UiKit::primaryButton(tr("Añadir prenda"), "btnSave");
    actions->addWidget(btnSave);
    layout->addLayout(actions);
    m_lblResult = new UiKit::ResultPanel();
    layout->addWidget(m_lblResult);
    layout->addLayout(UiKit::closeRow(this));

    connect(btnSearch,   &QPushButton::clicked, this, &AddGarment::onSearchClicked);
    UiKit::onEnter(m_leNRecibo, [this]() { onSearchClicked(); });
    connect(btnSave,     &QPushButton::clicked, this, &AddGarment::onSaveClicked);
    connect(btnReset,    &QPushButton::clicked, this, [this]() {
        resetAllContents();
        m_lblResult->showInfo(tr("Formulario limpio. Busque un recibo."));
    });
    connect(m_leCantidad, &QLineEdit::textChanged, this, &AddGarment::setGarmentPrice);
    connect(m_leSize,     &QLineEdit::textChanged, this, &AddGarment::setGarmentPrice);
    connect(m_cbServicio, &QComboBox::currentTextChanged, this, &AddGarment::setGarmentPrice);
    connect(m_cbPrenda,   &QComboBox::currentTextChanged, this, [this](const QString &text) {
        if (m_cbPrenda->findText(text, Qt::MatchExactly) != -1)
            setGarmentPrice();
    });
    connect(m_chkPagado,   &QCheckBox::toggled, m_deFechaPago,     &QWidget::setEnabled);
    connect(m_chkRecogido, &QCheckBox::toggled, m_deFechaRecogida, &QWidget::setEnabled);
}

void AddGarment::loadTicket(const QString &ticketNum)
{
    m_leNRecibo->setText(ticketNum);
    onSearchClicked();
}

void AddGarment::clearGarmentFields()
{
    m_cbPrenda->setCurrentText("");
    m_cbServicio->setCurrentIndex(0);
    m_leCantidad->clear();
    m_leSize->clear();
    m_leImporte->setText(displayMoney(0.0));
    m_leObservaciones->clear();
    m_chkPagado->setChecked(false);
    m_deFechaPago->setDate(QDate::currentDate());
    m_chkRecogido->setChecked(false);
    m_deFechaRecogida->setDate(QDate::currentDate());
    m_cbPrenda->setFocus();
}

void AddGarment::resetAllContents()
{
    m_ticketFound = false;
    m_searchedTicket.clear();
    m_leNRecibo->clear();
    m_leCliente->clear();
    m_deFechaRecepcion->setDate(QDate::currentDate());
    clearGarmentFields();
    m_deFechaPago->setEnabled(false);
    m_deFechaRecogida->setEnabled(false);
    m_grpTicket->setEnabled(false);
    m_grpGarment->setEnabled(false);
    m_grpState->setEnabled(false);
    m_lblResult->showInfo(tr("Introduzca el Nº de recibo y pulse \"Buscar\"."));
    m_leNRecibo->setFocus();
}

static QString unreadableTicketRefusal()
{
    return UiKit::errorHtml(trText("No se pudo comprobar el recibo."))
           + "<br>" + trText("Consulte el log (Ayuda \u2192 Log de depuración).");
}

static QString paidTicketRefusal(const QString &ticket)
{
    return UiKit::errorHtml(trText("El recibo Nº %1 ya tiene prendas pagadas.").arg(ticket.toHtmlEscaped()))
           + "<br>" + trText("Está enviado a AEAT y no se le pueden añadir prendas: utilice un recibo nuevo.");
}

void AddGarment::onSearchClicked()
{
    const QString ticket = m_leNRecibo->text().trimmed();
    m_ticketFound = false;
    m_searchedTicket.clear();
    m_grpTicket->setEnabled(false);
    m_grpGarment->setEnabled(false);
    m_grpState->setEnabled(false);

    if (ticket.isEmpty()) {
        m_lblResult->setText(UiKit::warnHtml(tr("Falta el Nº de recibo: introduzca el número y pulse \"Buscar\".")));
        return;
    }
    bool headerOk = false;
    const TicketHeader header = ticketHeader(db, ticket, &headerOk);
    if (!headerOk || !header.found) {
        m_lblResult->setText(UiKit::warnHtml(tr("No se ha encontrado el recibo Nº %1.").arg(ticket.toHtmlEscaped())));
        return;
    }
    // A paid ticket has already been submitted to AEAT; only unpaid receipts
    // (not yet submitted) may be altered locally by adding garments.
    bool paidReadOk = false;
    const bool hasPaid = ticketHasPaidGarment(db, ticket, &paidReadOk);
    if (!paidReadOk) {
        m_lblResult->setText(unreadableTicketRefusal());
        return;
    }
    if (hasPaid) {
        m_lblResult->setText(paidTicketRefusal(ticket));
        return;
    }
    m_ticketFound = true;
    m_searchedTicket = ticket;
    m_leCliente->setText(header.client);
    const QDate receptionDate = parseDbDate(header.receptionDate);
    m_deFechaRecepcion->setDate(receptionDate);
    // The payment date is the invoice date AEAT gets: not in the future, not before reception.
    // A previous search's minimum never carries over.
    for (QDateEdit *edit : { m_deFechaPago, m_deFechaRecogida }) {
        edit->clearMinimumDate();
        UiKit::limitDateRange(edit, { receptionDate }, QDate::currentDate());
    }
    // Nor before the day production began: such an invoice would never reach the real AEAT.
    // The current minimum is a candidate too, so the reception date is never lowered.
    UiKit::limitDateRange(m_deFechaPago,
                          { m_deFechaPago->minimumDate(), productionStartDate ? productionStartDate() : QDate() },
                          QDate::currentDate());
    populateGarments();
    m_grpTicket->setEnabled(true);
    m_grpGarment->setEnabled(true);
    m_grpState->setEnabled(true);
    m_lblResult->setText(UiKit::okHtml(tr("Recibo Nº %1 encontrado.").arg(ticket.toHtmlEscaped()))
                         + "<br>" + tr("Rellene la prenda y pulse \"Añadir prenda\"."));
    m_cbPrenda->setFocus();
}

void AddGarment::populateGarments()
{
    m_cbPrenda->clear();
    m_cbPrenda->addItems(readColumnFromTable(db, "nombre", "prendas", ""));
    m_cbPrenda->setCurrentText("");
}

void AddGarment::setGarmentPrice()
{
    if (m_leCantidad->text().isEmpty() || m_cbPrenda->findText(m_cbPrenda->currentText(), Qt::MatchExactly) == -1) {
        m_leImporte->setText(displayMoney(0.0));
        return;
    }
    // Comma-decimal normalisation + size factor live in sql_lite::garmentImporte
    // (unit-tested); see its comment for why the comma matters (m2 garments).
    const double amount = garmentImporte(
        m_leCantidad->text(), m_leSize->text(),
        readGarmentPrice(db, m_cbPrenda->currentText(), m_cbServicio->currentText()));
    m_leImporte->setText(displayMoney(amount));
}

QString AddGarment::validationError()
{
    // The number may have been retyped after the search: that ticket was never checked.
    if (!m_ticketFound || m_leNRecibo->text().trimmed() != m_searchedTicket) {
        qWarning() << "AddGarment: save without a search for receipt number" << m_leNRecibo->text();
        return UiKit::errorHtml(tr("No se ha buscado ningún Nº recibo previo a guardar los datos actuales."))
               + "<br>" + tr("Pulse \"Buscar\" con el número del recibo al que se añade la prenda.");
    }
    // Re-checked at save: a ticket paid meanwhile has an invoice at AEAT and cannot grow.
    bool paidReadOk = false;
    const bool hasPaid = ticketHasPaidGarment(db, m_searchedTicket, &paidReadOk);
    if (!paidReadOk)
        return unreadableTicketRefusal();
    if (hasPaid) {
        qWarning() << "AddGarment: ticket" << m_searchedTicket << "has a paid garment - refused";
        return paidTicketRefusal(m_searchedTicket);
    }
    if (m_leCliente->text().isEmpty() || m_cbPrenda->currentText().isEmpty() || m_leCantidad->text().isEmpty())
        return UiKit::errorHtml(tr("Formulario incompleto."))
               + "<br>" + tr("Rellene al menos la prenda y la cantidad.");
    if (m_cbPrenda->findText(m_cbPrenda->currentText(), Qt::MatchExactly) == -1)
        return UiKit::errorHtml(tr("La prenda \"%1\" no está en la lista de prendas.")
                                    .arg(m_cbPrenda->currentText().toHtmlEscaped()))
               + "<br>" + tr("Elíjala de la lista (o añádala antes a la lista de prendas).");
    if (m_leCantidad->text().toInt() <= 0)
        return UiKit::errorHtml(tr("La cantidad tiene que ser al menos 1."));
    if (garmentUnmeasured(m_cbPrenda->currentText(), m_leSize->text()))
        return UiKit::errorHtml(tr("La prenda se cobra por m2: introduzca su tamaño."));
    if (moneyText(m_leImporte->text()).toDouble() <= 0.0)
        return UiKit::errorHtml(tr("El importe de la prenda es 0,00."))
               + "<br>" + tr("Compruebe la prenda, el servicio, la cantidad y el tamaño.");
    if (m_chkPagado->isChecked()) {                                 // a paid garment is an invoice
        const QString blocked = invoicingBlocked ? invoicingBlocked()
                                                 : tr("Verifactu no está disponible: no se puede cobrar.");
        if (!blocked.isEmpty())
            return UiKit::errorHtml(blocked);
        const QString early = invoiceDateRefused ? invoiceDateRefused(m_deFechaPago->date()) : QString();
        if (!early.isEmpty())
            return UiKit::errorHtml(early);
    }
    if (quarterIsClosed(db, m_deFechaRecepcion->date()))
        return UiKit::errorHtml(tr("Trimestre bloqueado."))
               + "<br>" + tr("La fecha de recepción del ticket pertenece a un trimestre cerrado por la contabilidad.");
    if (m_chkPagado->isChecked() && quarterIsClosed(db, m_deFechaPago->date()))
        return UiKit::errorHtml(tr("Trimestre bloqueado."))
               + "<br>" + tr("La fecha de pago pertenece a un trimestre cerrado por la contabilidad.");
    return QString();
}

void AddGarment::onSaveClicked()
{
    const QString error = validationError();
    if (!error.isEmpty()) {
        m_lblResult->setText(error);
        return;
    }
    const QString ticket = m_searchedTicket;
    const QString garment = m_cbPrenda->currentText();
    const QString amount = displayMoney(m_leImporte->text());
    const bool paid = m_chkPagado->isChecked();
    m_saveError.clear();
    if (!saveGarment()) {
        m_lblResult->setText(!m_saveError.isEmpty() ? m_saveError
                             : UiKit::errorHtml(tr("No se pudo añadir la prenda."))
                               + "<br>" + tr("Consulte el log (Ayuda \u2192 Log de depuración)."));
        return;
    }
    // A paid ticket is refused from now on; an unpaid one stays loaded for the next garment.
    if (paid)
        resetAllContents();
    else
        clearGarmentFields();
    m_lblResult->setText(UiKit::okHtml(tr("Prenda añadida al recibo Nº %1: %2, %3 €.")
                                           .arg(ticket.toHtmlEscaped(), garment.toHtmlEscaped(), amount))
                         + (paid ? "<br>" + tr("Pagada: se envía a AEAT como la factura %1; la confirmación "
                                               "aparece en la barra de estado.").arg(ticket.toHtmlEscaped())
                                 : QString()));
}

bool AddGarment::saveGarment()
{
    const QString hash = genHash16();
    IngresoGarmentRow row;
    row.nRecibo        = m_searchedTicket;
    row.cliente        = m_leCliente->text();
    row.fechaRecepcion = dbDateText(m_deFechaRecepcion->date());
    row.fechaPago      = m_chkPagado->isChecked() ? dbDateText(m_deFechaPago->date()) : QString();
    row.fechaRecogida  = m_chkRecogido->isChecked() ? dbDateText(m_deFechaRecogida->date()) : QString();
    row.importe        = m_leImporte->text();
    row.pagado         = m_chkPagado->isChecked() ? INGRESOS_PAGADO_SI : INGRESOS_PAGADO_NO;
    row.estado         = m_chkRecogido->isChecked() ? INGRESOS_ESTADO_RECOGIDO : INGRESOS_ESTADO_EN_TIENDA;
    row.cantidad       = m_leCantidad->text();
    row.prenda         = m_cbPrenda->currentText();
    row.size           = m_leSize->text().replace(',', '.');
    row.servicio       = m_cbServicio->currentText();
    row.observaciones  = m_leObservaciones->text();
    row.hash           = hash;
    // Issue #41: a garment added to an existing ticket is un-submitted like a
    // freshly saved row, so it follows the same rule - SIN COBRAR while unpaid,
    // PENDIENTE once it is paid and an AEAT submit is due.
    row.verifactuEstado = verifactuEstadoToString(
        row.pagado == QLatin1String(INGRESOS_PAGADO_SI) ? VerifactuEstado::NotSubmitted
                                          : VerifactuEstado::Unpaid);
    qDebug() << "AddGarment: INSERT INTO ingresos ticket=" << row.nRecibo
             << "prenda=" << row.prenda << "cantidad=" << row.cantidad << "importe=" << row.importe
             << "pagado=" << row.pagado << "hash=" << hash;
    // A ticket with a paid garment is refused above, but a number the direct client already
    // used for an invoice of it (a payment never stored as paid) is not reused.
    if (row.pagado == QLatin1String(INGRESOS_PAGADO_SI)) {
        bool seqOk = false;
        row.verifactuInvoiceSeq = nextVerifactuInvoiceSeq(db, row.nRecibo, aeatEnvironment ? aeatEnvironment() : QString(), &seqOk);
        if (!seqOk) {
            qWarning() << "AddGarment: invoice number unreadable for ticket" << row.nRecibo << "- nothing stored";
            m_saveError = UiKit::errorHtml(tr("No se pudo leer la numeración de facturas del recibo."))
                          + "<br>" + tr("No se ha guardado nada. Consulte el log (Ayuda \u2192 Log de depuración).");
            return false;
        }
    }
    if (!insertGarmentRow(db, row))
        return false;
    emit garmentSaved(row.nRecibo);
    if (row.pagado == QLatin1String(INGRESOS_PAGADO_SI))
        emit paidGarmentSaved(row.nRecibo, parseDbDate(row.fechaPago),
                              moneyText(row.importe).toDouble(), row.verifactuInvoiceSeq);
    return true;
}
