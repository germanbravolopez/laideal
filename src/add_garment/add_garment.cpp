#include "add_garment.h"
#include "sql_lite.h"
#include "uikit.h"
#include "../verifactu/verifactutypes.h"

#include <QCheckBox>
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
#include <QSqlQuery>
#include <QSqlQueryModel>
#include <QVBoxLayout>

AddGarment::AddGarment(const QSqlDatabase &database, QWidget *parent) :
    QDialog(parent),
    db(database)
{
    setAttribute(Qt::WA_DeleteOnClose);
    UiKit::setUpDialog(this, "Añadir nuevas prendas");
    m_ticketModel = new QSqlQueryModel(this);
    buildUi();
    resetAllContents();
}

static QDateEdit *dateEdit(const QString &objectName)
{
    QDateEdit *edit = new QDateEdit(QDate::currentDate());
    edit->setObjectName(objectName);
    edit->setCalendarPopup(true);
    edit->setDisplayFormat("dd-MM-yyyy");
    return edit;
}

void AddGarment::buildUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(UiKit::introPanel(
        "Añade una prenda a un recibo que todavía no tiene prendas pagadas. Busque primero el "
        "recibo; si la prenda se añade como pagada, se envía a AEAT como la factura del recibo."));

    // Search ---------------------------------------------------------------
    QHBoxLayout *searchRow = new QHBoxLayout();
    searchRow->addWidget(new QLabel("Nº recibo:"));
    m_leNRecibo = new QLineEdit();
    m_leNRecibo->setObjectName("leNRecibo");   // stable names for the e2e test bench
    m_leNRecibo->setPlaceholderText("Ej: 24417");
    searchRow->addWidget(m_leNRecibo, 1);
    QPushButton *btnSearch = UiKit::secondaryButton("Buscar", "btnSearch");
    searchRow->addWidget(btnSearch);
    layout->addLayout(searchRow);

    // Receipt found ----------------------------------------------------------
    m_grpTicket = new QGroupBox("Recibo");
    QFormLayout *ticketForm = new QFormLayout(m_grpTicket);
    m_leCliente = new QLineEdit();
    m_leCliente->setObjectName("leCliente");
    m_leCliente->setReadOnly(true);
    ticketForm->addRow("Cliente:", m_leCliente);
    m_deFechaRecepcion = dateEdit("deFechaRecepcion");
    m_deFechaRecepcion->setReadOnly(true);
    m_deFechaRecepcion->setButtonSymbols(QAbstractSpinBox::NoButtons);
    ticketForm->addRow("Fecha recepción:", m_deFechaRecepcion);
    layout->addWidget(m_grpTicket);

    // Garment ----------------------------------------------------------------
    m_grpGarment = new QGroupBox("Prenda");
    QFormLayout *garmentForm = new QFormLayout(m_grpGarment);
    m_cbPrenda = new QComboBox();
    m_cbPrenda->setObjectName("cbPrenda");
    m_cbPrenda->setEditable(true);
    garmentForm->addRow("Prenda:", m_cbPrenda);
    m_cbServicio = new QComboBox();
    m_cbServicio->setObjectName("cbServicio");
    m_cbServicio->addItems({ "Limp.", "Plan." });
    garmentForm->addRow("Servicio:", m_cbServicio);
    m_leCantidad = new QLineEdit();
    m_leCantidad->setObjectName("leCantidad");
    m_leCantidad->setValidator(new QIntValidator(1, 9999, m_leCantidad));
    garmentForm->addRow("Cantidad:", m_leCantidad);
    m_leSize = new QLineEdit();
    m_leSize->setObjectName("leSize");
    m_leSize->setPlaceholderText("Solo prendas por m2");
    garmentForm->addRow("Tamaño (m2):", m_leSize);
    m_leImporte = new QLineEdit();
    m_leImporte->setObjectName("leImporte");
    m_leImporte->setReadOnly(true);
    garmentForm->addRow("Importe:", m_leImporte);
    m_leObservaciones = new QLineEdit();
    m_leObservaciones->setObjectName("leObservaciones");
    m_leObservaciones->setPlaceholderText("Opcional");
    garmentForm->addRow("Observaciones:", m_leObservaciones);
    layout->addWidget(m_grpGarment);

    // Payment and pickup -----------------------------------------------------
    m_grpState = new QGroupBox("Estado");
    QFormLayout *stateForm = new QFormLayout(m_grpState);
    m_chkPagado = new QCheckBox("Pagada");
    m_chkPagado->setObjectName("chkPagado");
    m_deFechaPago = dateEdit("deFechaPago");
    QHBoxLayout *payRow = new QHBoxLayout();
    payRow->addWidget(m_chkPagado);
    payRow->addWidget(new QLabel("el"));
    payRow->addWidget(m_deFechaPago);
    payRow->addStretch();
    stateForm->addRow(payRow);
    m_chkRecogido = new QCheckBox("Recogida");
    m_chkRecogido->setObjectName("chkRecogido");
    m_deFechaRecogida = dateEdit("deFechaRecogida");
    QHBoxLayout *pickupRow = new QHBoxLayout();
    pickupRow->addWidget(m_chkRecogido);
    pickupRow->addWidget(new QLabel("el"));
    pickupRow->addWidget(m_deFechaRecogida);
    pickupRow->addStretch();
    stateForm->addRow(pickupRow);
    layout->addWidget(m_grpState);

    // Actions + result -------------------------------------------------------
    QHBoxLayout *actions = new QHBoxLayout();
    QPushButton *btnReset = UiKit::secondaryButton("Limpiar formulario", "btnReset");
    actions->addWidget(btnReset);
    actions->addStretch();
    QPushButton *btnSave = UiKit::primaryButton("Añadir prenda", "btnSave");
    actions->addWidget(btnSave);
    layout->addLayout(actions);
    m_lblResult = new UiKit::ResultPanel();
    layout->addWidget(m_lblResult);
    layout->addLayout(UiKit::closeRow(this));

    connect(btnSearch,   &QPushButton::clicked, this, &AddGarment::onSearchClicked);
    connect(m_leNRecibo, &QLineEdit::returnPressed, this, &AddGarment::onSearchClicked);
    connect(btnSave,     &QPushButton::clicked, this, &AddGarment::onSaveClicked);
    connect(btnReset,    &QPushButton::clicked, this, [this]() {
        resetAllContents();
        m_lblResult->showInfo("Formulario limpio. Busque un recibo.");
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

void AddGarment::resetAllContents()
{
    ticketFound = false;
    m_searchedTicket.clear();
    m_leNRecibo->clear();
    m_leCliente->clear();
    m_deFechaRecepcion->setDate(QDate::currentDate());
    m_cbPrenda->setCurrentText("");
    m_cbServicio->setCurrentIndex(0);
    m_leCantidad->clear();
    m_leSize->clear();
    m_leImporte->setText("0.00");
    m_leObservaciones->clear();
    m_chkPagado->setChecked(false);
    m_deFechaPago->setDate(QDate::currentDate());
    m_deFechaPago->setEnabled(false);
    m_chkRecogido->setChecked(false);
    m_deFechaRecogida->setDate(QDate::currentDate());
    m_deFechaRecogida->setEnabled(false);
    m_grpTicket->setEnabled(false);
    m_grpGarment->setEnabled(false);
    m_grpState->setEnabled(false);
    m_lblResult->showInfo("Introduzca el Nº de recibo y pulse \"Buscar\".");
    m_leNRecibo->setFocus();
}

static QString paidTicketRefusal(const QString &ticket)
{
    return UiKit::errorHtml("El recibo Nº " + ticket.toHtmlEscaped() + " ya tiene prendas pagadas.")
           + "<br>Está enviado a AEAT y no se le pueden añadir prendas: utilice un recibo nuevo.";
}

void AddGarment::onSearchClicked()
{
    const QString ticket = m_leNRecibo->text().trimmed();
    ticketFound = false;
    m_searchedTicket.clear();
    m_grpTicket->setEnabled(false);
    m_grpGarment->setEnabled(false);
    m_grpState->setEnabled(false);

    db.open();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM ingresos WHERE n_recibo = :n_recibo");
    q.bindValue(":n_recibo", ticket);
    q.exec();
    m_ticketModel->setQuery(std::move(q));
    db.close();
    if (ticket.isEmpty() || m_ticketModel->rowCount() == 0) {
        m_lblResult->setText(UiKit::warnHtml("No se ha encontrado el recibo Nº " + ticket.toHtmlEscaped() + "."));
        return;
    }
    // A paid ticket has already been submitted to AEAT; only unpaid receipts
    // (not yet submitted) may be altered locally by adding garments.
    if (ticketHasPaidGarment(db, ticket)) {
        m_lblResult->setText(paidTicketRefusal(ticket));
        return;
    }
    ticketFound = true;
    m_searchedTicket = ticket;
    m_leCliente->setText(m_ticketModel->data(m_ticketModel->index(0, INGRESOS_COL_CLIENTE)).toString());
    m_deFechaRecepcion->setDate(QDate::fromString(
        m_ticketModel->data(m_ticketModel->index(0, INGRESOS_COL_FECHA_RECEPCION)).toString(), "dd-MM-yyyy"));
    populateGarments();
    m_grpTicket->setEnabled(true);
    m_grpGarment->setEnabled(true);
    m_grpState->setEnabled(true);
    m_lblResult->setText(UiKit::okHtml("Recibo Nº " + ticket.toHtmlEscaped() + " encontrado.")
                         + "<br>Rellene la prenda y pulse \"Añadir prenda\".");
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
    if (m_leCantidad->text().isEmpty()) {
        m_leImporte->setText("0.00");
        return;
    }
    // Comma-decimal normalisation + size factor live in sql_lite::garmentImporte
    // (unit-tested); see its comment for why the comma matters (m2 garments).
    const double importe = garmentImporte(
        m_leCantidad->text(), m_leSize->text(),
        readGarmentPrice(db, m_cbPrenda->currentText(), m_cbServicio->currentText()));
    m_leImporte->setText(moneyText(importe));
}

QString AddGarment::validationError()
{
    // The number may have been retyped after the search: that ticket was never checked.
    if (!ticketFound || m_leNRecibo->text().trimmed() != m_searchedTicket) {
        qWarning() << "AddGarment: save without a search for receipt number" << m_leNRecibo->text();
        return UiKit::errorHtml("No se ha buscado ningún Nº recibo previo a guardar los datos actuales.")
               + "<br>Pulse \"Buscar\" con el número del recibo al que se añade la prenda.";
    }
    // Re-checked at save: a ticket paid meanwhile has an invoice at AEAT and cannot grow.
    if (ticketHasPaidGarment(db, m_searchedTicket)) {
        qWarning() << "AddGarment: ticket" << m_searchedTicket << "has a paid garment - refused";
        return paidTicketRefusal(m_searchedTicket);
    }
    if (m_leCliente->text().isEmpty() || m_cbPrenda->currentText().isEmpty() || m_leCantidad->text().isEmpty())
        return UiKit::errorHtml("Formulario incompleto.")
               + "<br>Rellene al menos la prenda y la cantidad.";
    if (m_cbPrenda->currentText().contains("m2") && m_leSize->text().replace(',', '.').toDouble() <= 0)
        return UiKit::errorHtml("La prenda se cobra por m2: introduzca su tamaño.");
    if (m_chkPagado->isChecked() && quarterIsClosed(db, m_deFechaPago->date()))
        return UiKit::errorHtml("Trimestre bloqueado.")
               + "<br>La fecha de pago pertenece a un trimestre cerrado por la contabilidad.";
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
    const QString amount = moneyText(m_leImporte->text()).replace('.', ',');
    const bool paid = m_chkPagado->isChecked();
    if (!saveGarment()) {
        m_lblResult->setText(UiKit::errorHtml("No se pudo añadir la prenda.")
                             + "<br>Consulte el log (Ayuda → Mostrar log).");
        return;
    }
    resetAllContents();
    m_lblResult->setText(UiKit::okHtml("Prenda añadida al recibo Nº " + ticket.toHtmlEscaped() + ": "
                                       + garment.toHtmlEscaped() + ", " + amount + " €.")
                         + (paid ? "<br>Pagada: se envía a AEAT como la factura " + ticket.toHtmlEscaped()
                                   + "; la confirmación aparece en la barra de estado."
                                 : QString()));
}

bool AddGarment::saveGarment()
{
    const QString hash = genHash16();
    IngresoGarmentRow row;
    row.nRecibo        = m_searchedTicket;
    row.cliente        = m_leCliente->text();
    row.fechaRecepcion = m_deFechaRecepcion->date().toString("dd-MM-yyyy");
    row.fechaPago      = m_chkPagado->isChecked() ? m_deFechaPago->date().toString("dd-MM-yyyy") : QString();
    row.fechaRecogida  = m_chkRecogido->isChecked() ? m_deFechaRecogida->date().toString("dd-MM-yyyy") : QString();
    row.importe        = m_leImporte->text();
    row.pagado         = m_chkPagado->isChecked() ? "SI" : "NO";
    row.estado         = m_chkRecogido->isChecked() ? "Recogido" : "En tienda";
    row.cantidad       = m_leCantidad->text();
    row.prenda         = m_cbPrenda->currentText();
    row.size           = m_leSize->text().replace(',', '.');
    row.servicio       = m_cbServicio->currentText();
    row.observaciones  = m_leObservaciones->text();
    row.editLock       = "0";
    row.hash           = hash;
    // Issue #41: a garment added to an existing ticket is un-submitted like a
    // freshly saved row, so it follows the same rule - SIN COBRAR while unpaid,
    // PENDIENTE once it is paid and an AEAT submit is due.
    row.verifactuEstado = verifactuEstadoToString(
        row.pagado == QLatin1String("SI") ? VerifactuEstado::NotSubmitted
                                          : VerifactuEstado::Unpaid);
    qDebug() << "AddGarment: INSERT INTO ingresos ticket=" << row.nRecibo << "cliente=" << row.cliente
             << "prenda=" << row.prenda << "cantidad=" << row.cantidad << "importe=" << row.importe
             << "pagado=" << row.pagado << "hash=" << hash;
    if (!insertGarmentRow(db, row))
        return false;
    // A ticket that already has a paid garment is refused above, so this payment is
    // the ticket's first invoice: seq 0, InvoiceID = n_recibo.
    if (row.pagado == QLatin1String("SI"))
        emit paidGarmentSaved(row.nRecibo, QDate::fromString(row.fechaPago, "dd-MM-yyyy"),
                              moneyText(row.importe).toDouble());
    return true;
}
