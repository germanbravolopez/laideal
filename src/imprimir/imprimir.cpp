#include "aeatqr.h"
#include "imprimir.h"

#include <QSet>
#include "sql_lite.h"
#include "verifactutypes.h"
#include "appsettings.h"
#include "uikit.h"

#include "ticketrenderer.h"
#include "thermalprinter.h"
#include "statusapiprinter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDebug>
#include <QEventLoop>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlError>
#include <QTimer>
#include <QVBoxLayout>

Imprimir::Imprimir(const QSqlDatabase &database, QWidget *parent)
    : QDialog(parent), db(database)
{
    setWindowModality(Qt::WindowModal);
    UiKit::setUpDialog(this, "Imprimir", 460);
    buildUi();
}

void Imprimir::buildUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    m_lblIntro = UiKit::introPanel(QString());   // text per mode, see showEvent()
    layout->addWidget(m_lblIntro);

    QGroupBox *grpTicket = new QGroupBox("Ticket");
    QFormLayout *ticketForm = new QFormLayout(grpTicket);
    le_n_ticket = new QLineEdit();
    le_n_ticket->setObjectName("le_n_ticket");   // stable names for the e2e test bench
    le_n_ticket->setPlaceholderText("Ej: 24417");
    ticketForm->addRow("Nº recibo:", le_n_ticket);
    m_lblEvent = new QLabel("Factura:");
    m_cbEvent = new QComboBox();
    m_cbEvent->setObjectName("cbEvent");
    ticketForm->addRow(m_lblEvent, m_cbEvent);
    m_lblEvent->setVisible(false);
    m_cbEvent->setVisible(false);
    m_chkShopCopy = new QCheckBox("Imprimir también la copia del establecimiento");
    m_chkShopCopy->setObjectName("chkShopCopy");
    m_chkShopCopy->setChecked(true);
    ticketForm->addRow(m_chkShopCopy);
    layout->addWidget(grpTicket);

    m_grpClient = new QGroupBox("Datos del cliente para la factura completa");
    QFormLayout *clientForm = new QFormLayout(m_grpClient);
    m_leAddress = new QLineEdit();
    m_leAddress->setObjectName("leAddress");
    m_leAddress->setPlaceholderText("Vacío: la dirección guardada del cliente");
    clientForm->addRow("Dirección:", m_leAddress);
    m_leDni = new QLineEdit();
    m_leDni->setObjectName("leDni");
    clientForm->addRow("DNI / NIF:", m_leDni);
    layout->addWidget(m_grpClient);

    QHBoxLayout *actions = new QHBoxLayout();
    actions->addStretch();
    m_btnPrint = UiKit::primaryButton("Imprimir", "btnPrint");
    actions->addWidget(m_btnPrint);
    layout->addLayout(actions);
    m_lblResult = new UiKit::ResultPanel();
    layout->addWidget(m_lblResult);
    layout->addLayout(UiKit::closeRow(this));

    connect(m_btnPrint, &QPushButton::clicked, this, &Imprimir::onPrintClicked);
    UiKit::onEnter(le_n_ticket, [this]() { onPrintClicked(); });
    // Another number: its partial-payment invoices are listed again on the next print.
    connect(le_n_ticket, &QLineEdit::textEdited, this, [this]() {
        m_eventsTicket.clear();
        m_lblEvent->setVisible(false);
        m_cbEvent->setVisible(false);
    });
}

void Imprimir::showEvent(QShowEvent *event)
{
    // The mode (isRecibo / isCompleteInvoice) is set by the caller after construction.
    const QString what = isRecibo ? "el recibo" : isCompleteInvoice ? "la factura completa" : "la factura";
    m_lblIntro->setText(isRecibo
        ? "Imprime el recibo (resguardo) de un ticket con todas sus prendas: la copia del cliente "
          "y, si se marca, la del establecimiento."
        : isCompleteInvoice
            ? "Imprime la factura completa de un ticket pagado, con la dirección y el DNI / NIF del "
              "cliente, y el código QR de AEAT si la factura está confirmada."
            : "Imprime la factura simplificada de un ticket pagado, con el código QR de AEAT si la "
              "factura está confirmada.");
    m_grpClient->setVisible(isCompleteInvoice);
    m_chkShopCopy->setVisible(isRecibo);
    m_btnPrint->setText(isRecibo ? "Imprimir recibo" : "Imprimir factura");
    if (m_lblResult->text().isEmpty())
        m_lblResult->showInfo("Introduzca el Nº de recibo y pulse \"" + m_btnPrint->text() + "\".");
    qDebug() << "Imprimir: opened to print" << what;
    QDialog::showEvent(event);
}

void Imprimir::getTicketInfo()
{
    if (!sqlQueryModel)
        sqlQueryModel = new QSqlQueryModel(this);
    ConnectionScope scope(db);
    QSqlQuery q(db);
    // Anulado rows (locally voided garments) never appear on a printed recibo or
    // factura. The factura branch already filters pagado='SI' (Anulado is unpaid),
    // but exclude it explicitly on both paths so the rule is unambiguous. The
    // (estado IS NULL OR ...) guard keeps legacy NULL-estado rows on the ticket -
    // SQLite treats `NULL != 'Anulado'` as NULL (excluded), not true.
    if (invoiceSeq >= 0) {
        // verifactu_invoice_seq DEFAULTs to 0 on every row, so a seq filter
        // alone would also match unpaid rows of the same ticket. Restrict
        // to paid rows so a seq=0 payment event doesn't pull in still-unpaid
        // garments (e.g. PayDialog event 0 under disabled Verifactu).
        q.prepare("SELECT * FROM ingresos WHERE n_recibo = :n_recibo "
                  "AND verifactu_invoice_seq = :seq AND pagado = 'SI' "
                  "AND (estado IS NULL OR estado != :anulado)");
        q.bindValue(":n_recibo", le_n_ticket->text().trimmed());
        q.bindValue(":seq", invoiceSeq);
        q.bindValue(":anulado", QStringLiteral(INGRESOS_ESTADO_ANULADO));
    } else {
        q.prepare("SELECT * FROM ingresos WHERE n_recibo = :n_recibo "
                  "AND (estado IS NULL OR estado != :anulado)");
        q.bindValue(":n_recibo", le_n_ticket->text().trimmed());
        q.bindValue(":anulado", QStringLiteral(INGRESOS_ESTADO_ANULADO));
    }
    if (!q.exec())
        qWarning() << "Imprimir::getTicketInfo: query failed:" << q.lastError().text();
    sqlQueryModel->setQuery(std::move(q));
    while (sqlQueryModel->canFetchMore())   // the rows are read after the connection closes
        sqlQueryModel->fetchMore();
}

QString Imprimir::displayInvoiceId() const
{
    if (!sqlQueryModel) return le_n_ticket->text();
    // Selection rule (first non-empty literal, else bare n_recibo) is the pure
    // sql_lite::verifactuDisplayInvoiceId seam; here we just gather the column.
    QStringList invoiceIds;
    for (int r = 0; r < sqlQueryModel->rowCount(); ++r) {
        invoiceIds << sqlQueryModel->data(
            sqlQueryModel->index(r, INGRESOS_COL_VERIFACTU_INVOICE_ID)).toString();
    }
    return verifactuDisplayInvoiceId(invoiceIds, le_n_ticket->text().trimmed());
}

bool Imprimir::checkTicketPaid(int row)
{
    return sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_PAGADO)).toString() == "SI";
}

bool Imprimir::checkAnyItemPaid()
{
    for (int row = 0; row < sqlQueryModel->rowCount(); row++) {
        if (checkTicketPaid(row))
            return true;
    }
    return false;
}

QPixmap Imprimir::resolveQrCode()
{
    m_qrFromQueuedRecord = false;
    if (!qrCode.isNull())
        return qrCode;

    if (!verifactuIntegration || !verifactuIntegration->isConfigured())
        return QPixmap();

    if (!sqlQueryModel || sqlQueryModel->rowCount() == 0)
        return QPixmap();

    // Aggregate across rows of the ticket. A single n_recibo can hold:
    //  - rows submitted to AEAT (verifactu_csv set, estado=ENVIADA)
    //  - rows added later via split-garment / add-garment (estado empty / PENDIENTE)
    //  - rows superseded by anulacion / rectificativa (ANULADA / RECTIFICADA)
    //  - rows that failed submission (ERROR)
    // Only emit a QR when at least one row carries a real CSV AND no row is in a
    // state that would make the QR misleading (ANULADA / RECTIFICADA / ERROR).
    bool hasCsv = false;
    bool anyBlocking = false;
    for (int row = 0; row < sqlQueryModel->rowCount(); ++row) {
        if (!sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_VERIFACTU_CSV)).toString().isEmpty())
            hasCsv = true;
        const VerifactuEstado e = verifactuEstadoFromString(
            sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_VERIFACTU_ESTADO)).toString());
        if (e == VerifactuEstado::Anulada || e == VerifactuEstado::Rectificada || e == VerifactuEstado::Error
            || e == VerifactuEstado::Unknown) {   // an unrecognised estado: no QR (nor leyenda)
            anyBlocking = true;
            break;
        }
    }
    if (anyBlocking)
        return QPixmap();

    // Prefer the literal AEAT InvoiceID stored at submit time so the QR matches
    // exactly what AEAT has on record (legacy 8.0-8.4 rows have it empty and we
    // fall back to bare n_recibo - same string they were submitted under).
    QString invoiceNumber = displayInvoiceId();
    // The QR's FechaExpedicion must equal the date submitted to AEAT, which is
    // the payment date (fecha_pago) of the printed rows: PayDialog submits with
    // fecha_pago and late-pickup retries with the payment date, while save-time
    // submissions store fecha_pago = fecha_recepcion, so fecha_pago is correct
    // for every path. (The old code read fecha_recepcion, which diverged from
    // AEAT whenever payment day != reception day, e.g. a partial payment.)
    // Scan for the first paid row's fecha_pago - row 0 may be unpaid in the
    // unfiltered legacy reprint path.
    QDate invoiceDate;
    for (int row = 0; row < sqlQueryModel->rowCount(); ++row) {
        if (!checkTicketPaid(row))
            continue;
        invoiceDate = QDate::fromString(
            sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_FECHA_PAGO)).toString(), "dd-MM-yyyy");
        if (invoiceDate.isValid()) break;
    }
    if (!invoiceDate.isValid())
        return QPixmap();

    // The QR describes the invoice AEAT received: the paid rows of ONE payment event.
    double total = 0.0;
    QSet<int> seqs;
    for (int row = 0; row < sqlQueryModel->rowCount(); row++) {
        if (sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_PAGADO)).toString() != QLatin1String("SI"))
            continue;
        total += sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_IMPORTE)).toDouble();
        seqs.insert(sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_VERIFACTU_INVOICE_SEQ)).toInt());
    }
    if (seqs.size() > 1) {
        qWarning() << "Imprimir::resolveQrCode: rows of" << seqs.size() << "payment events loaded - no QR";
        return QPixmap();
    }

    double ivaRate = AppSettings::instance()->ivaRate();
    double taxBase = total / (1.0 + ivaRate / 100.0);

    if (!hasCsv) {
        // Not confirmed by AEAT yet: only the direct client, whose record is generated,
        // chained and queued, has a QR (the one printed at issue), with its leyenda.
        const QPixmap queued = verifactuIntegration->qrBeforeReply(invoiceNumber, invoiceDate, taxBase, ivaRate);
        if (queued.isNull())
            return QPixmap();
        m_qrFromQueuedRecord = true;
        qrCode = queued;
        return qrCode;
    }

    // Reprint of an accepted invoice: the verification URL stored with AEAT's answer, drawn
    // as issued. Never rebuilt from today's environment (after the production switch an
    // older test invoice would get a production QR) nor from today's IVA rate.
    for (int row = 0; row < sqlQueryModel->rowCount(); ++row) {
        if (!checkTicketPaid(row))
            continue;
        const QString url = sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_VERIFACTU_URL_QR)).toString();
        if (url.startsWith(QLatin1String("https://"))) {
            qrCode = QPixmap::fromImage(AeatQr::image(url, 5));
            return qrCode;
        }
    }

    // No stored URL: only older gateway rows. The direct client would rebuild the URL from
    // today's environment and IVA rate, which may not be the invoice's: no QR.
    if (verifactuIntegration->isDirect()) {
        qWarning() << "Imprimir::resolveQrCode:" << invoiceNumber << "has no stored verification URL - printed without QR";
        return QPixmap();
    }
    // The gateway: ask it, waiting up to 5 s in a local event loop.
    // If it does not reply in time, print without QR (logged warning).
    const QString reqId = verifactuIntegration->generateQRAsync(
        invoiceNumber, invoiceDate, taxBase, ivaRate, kInvoiceDescription);
    if (reqId.isEmpty()) {
        qWarning() << "GetQrCode rejected for ticket" << invoiceNumber
                   << ":" << verifactuIntegration->getLastError();
        return QPixmap();
    }

    VerifactuResult result;
    QEventLoop loop;
    auto conn = connect(verifactuIntegration, &VerifactuIntegration::requestFinished, this,
            [&loop, &result, reqId](const QString &id, const VerifactuResult &r) {
        if (id != reqId) return;
        result = r;
        loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    disconnect(conn);

    if (result.qrCode.isNull()) {
        qWarning() << "GetQrCode failed or timed out for ticket" << invoiceNumber
                   << ":" << (result.errorDescription.isEmpty() ? "timeout (5s)" : result.errorDescription);
        return QPixmap();
    }

    qrCode = result.qrCode;
    return qrCode;
}

int Imprimir::paperDots() const
{
    // 80 mm roll = 576 printable dots, 58 mm = 420 (203 dpi). Anything other
    // than 58 maps to the 80 mm default.
    return AppSettings::instance()->paperWidthMm() == 58 ? 420 : 576;
}

void Imprimir::buildTicket(bool copyForClient, bool addPayedInfo)
{
    // Nothing to print (unknown number, every garment voided): never a blank ticket.
    if (!sqlQueryModel || sqlQueryModel->rowCount() == 0) {
        qWarning() << "Imprimir::buildTicket: no printable rows for ticket" << le_n_ticket->text() << "- nothing built";
        m_ticketBytes.clear();
        return;
    }
    AppSettings *settings = AppSettings::instance();

    TicketData d;
    d.businessName = settings->businessName();
    d.legalName    = settings->verifactuName();
    d.nif          = settings->verifactuNif();
    d.address      = settings->businessAddress();
    d.city         = settings->businessCity();
    d.phone        = settings->businessPhone();

    d.isRecibo          = isRecibo;
    d.isCompleteInvoice = isCompleteInvoice;
    // A recibo is the claim ticket: its number is the n_recibo, never a payment event's invoice id.
    d.invoiceId         = isRecibo ? le_n_ticket->text().trimmed() : displayInvoiceId();
    const QString client = sqlQueryModel->data(sqlQueryModel->index(0, INGRESOS_COL_CLIENTE)).toString();
    d.clientName     = client;
    d.receptionDate  = sqlQueryModel->data(sqlQueryModel->index(0, INGRESOS_COL_FECHA_RECEPCION)).toString().replace("-", "/");

    // A factura shows its date of issue - the payment date, as in the QR - and says so
    // when the invoice was cancelled or rectified since.
    m_lastVoidedMarker.clear();
    if (!isRecibo) {
        for (int row = 0; row < sqlQueryModel->rowCount(); ++row) {
            if (!checkTicketPaid(row))
                continue;
            if (d.issueDate.isEmpty())
                d.issueDate = sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_FECHA_PAGO)).toString().replace("-", "/");
            const VerifactuEstado e = verifactuEstadoFromString(
                sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_VERIFACTU_ESTADO)).toString());
            if (e == VerifactuEstado::Anulada || e == VerifactuEstado::Rectificada) {
                d.voidedMarker = e == VerifactuEstado::Anulada ? QStringLiteral("ANULADA") : QStringLiteral("RECTIFICADA");
                d.voidedDate   = sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_FECHA_ANULACION)).toString().replace("-", "/");
            }
        }
        m_lastVoidedMarker = d.voidedMarker;
    }

    // Complete invoice: billing address (typed, else the client's) + DNI / NIF (typed).
    if (isCompleteInvoice) {
        const QString typed = m_leAddress->text().trimmed();
        d.clientAddress = typed.isEmpty() ? searchItemFromClient(db, "direccion", client, false) : typed;
        d.clientDni     = m_leDni->text().trimmed();
    }

    // Garment rows: every row on a recibo, only paid rows on a factura.
    double total = 0.0;
    for (int rowCnt = 0; rowCnt < sqlQueryModel->rowCount(); rowCnt++) {
        if (!(isRecibo || checkTicketPaid(rowCnt)))
            continue;
        TicketGarmentLine g;
        g.quantity = sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_CANTIDAD)).toString();
        QString garmentName = sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_PRENDA)).toString();
        const QString size = QString::number(
            sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_SIZE)).toFloat(), 'f', 2);
        if (size != "0.00")
            garmentName.append(" - " + displayDecimal(size));
        g.name = garmentName;
        const double importe = sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_IMPORTE)).toDouble();
        g.amount = displayMoney(importe);
        d.garments.append(g);
        total += importe;
    }
    d.total   = total;
    d.ivaRate = settings->ivaRate();

    d.addPayedInfo  = addPayedInfo;
    d.copyForClient = copyForClient;

    // Verifactu QR - facturas only. Recibos are claim tickets, not tax documents:
    // they carry no Verifactu metadata, so a QR would mislead the customer into
    // thinking the receipt was submitted to AEAT. The symbol is the one issued (the
    // same modules), redrawn at the size the Orden requires.
    const QPixmap qr = !isRecibo ? resolveQrCode() : QPixmap();
    if (!qr.isNull()) {
        d.qr = TicketRenderer::printableQr(qr.toImage(), paperDots());
        if (d.qr.isNull()) {
            qWarning() << "Imprimir: QR not readable, printed as received";
            d.qr = qr.toImage();
        }
        // Disp. Final Primera RD 1007/2023: print the verification leyenda only
        // for rows AEAT actually accepted (estado = ENVIADA), never for tickets
        // still PENDIENTE, in ERROR, or ANULADA - except the print at issue of a
        // record the direct client has queued, or of an accepted reply whose row write failed.
        // Decided by the paid rows (the invoice's rows, like the QR amount), never by row 0:
        // each must be ENVIADA, or NotSubmitted in those two cases.
        const QString sent = verifactuEstadoToString(VerifactuEstado::Enviada);
        const QString queued = verifactuEstadoToString(VerifactuEstado::NotSubmitted);
        const bool queuedAllowed = verifactuIssued || m_qrFromQueuedRecord;
        bool anyPaid = false;
        bool allVerifiable = true;
        for (int row = 0; row < sqlQueryModel->rowCount(); ++row) {
            if (!checkTicketPaid(row))
                continue;
            anyPaid = true;
            const QString state = sqlQueryModel->data(
                sqlQueryModel->index(row, INGRESOS_COL_VERIFACTU_ESTADO)).toString();
            if (state != sent && !(queuedAllowed && state == queued))
                allVerifiable = false;
        }
        d.verifactuVerifiable = anyPaid && allVerifiable;
    }

    d.timestamp = QDateTime::currentDateTime().toString("dd/MM/yyyy - hh:mm:ss");

    m_ticketBytes = TicketRenderer::render(d, paperDots());
}

bool Imprimir::printTicket()
{
    // Send the ESC/POS bytes built by buildTicket() straight to the printer
    // queue as RAW spool data - no Excel, no .vbs, no cscript.
    if (m_ticketBytes.isEmpty()) {
        qWarning() << "Imprimir::printTicket: no ticket built";
        return false;
    }
    const QString printer = AppSettings::instance()->printerName();
    QString err;
    m_lastSendUncertain = false;
    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Optional Status API path (Epson DLL): without the DLL it fails with an empty status
    // and the RAW spooler below takes over, so the flag never stops printing.
    if (AppSettings::instance()->useStatusApi()) {
        PrinterStatus status;
        QString statusErr;
        // 10 s watchdog: above the DLL's own ~5.5 s on a cover-open send, below a real hang
        // (docs/modules/printing.md).
        bool mayStillPrint = false;
        const bool sent = StatusApiPrinter::sendAndReadStatusBounded(
            m_ticketBytes, printer, &status, &statusErr, 10000, &mayStillPrint);
        QApplication::restoreOverrideCursor();
        // The hang was inside the send itself: the Epson DLL may still print this
        // ticket, so RAW would print it twice. Leave the call to the operator.
        if (mayStillPrint) {
            m_lastSendUncertain = true;
            qWarning() << "Imprimir::printTicket: Status API blocked during the send, not resending by RAW:" << statusErr;
            reportPrinterProblem("La impresora no respondió al enviar el ticket y puede que se imprima igualmente.\n"
                                 "Compruebe la impresora antes de volver a imprimirlo.", true);
            return false;
        }
        bool warned = false;
        if (status.valid && (status.hasError() || status.hasWarning())) {
            qWarning() << "Imprimir::printTicket status:" << status.summary()
                       << Qt::hex << status.raw;
            reportPrinterProblem(status.summary(), false);
            warned = true;
        }
        // Fatal fault (cutter/mechanical/unrecoverable): do not queue the ticket -
        // the operator must fix the printer and reprint.
        if (status.valid && status.isFatal())
            return false;
        if (sent)
            return true;
        // The send was refused but the ASB did not decode to a known fault: the
        // TM-T20III reports an open cover / offline as ASB_NO_RESPONSE (0x1), not
        // ASB_COVER_OPEN. When we did reach the printer's status (status.valid),
        // warn generically so the operator gets feedback; a missing DLL/API leaves
        // status invalid and stays silent (RAW alone works there).
        if (status.valid && !warned) {
            reportPrinterProblem("No se pudo enviar a la impresora.\n"
                                 "Comprueba que la tapa está cerrada, que hay papel y la conexión.", false);
        }
        // Recoverable state (cover open / paper out) or a non-status send failure:
        // fall through to the RAW spooler, which queues the job so it prints once
        // the cover is closed / paper is replaced.
        qWarning() << "Imprimir::printTicket: Status API did not send, using RAW:" << statusErr;
        QApplication::setOverrideCursor(Qt::WaitCursor);
    }

    const bool ok = ThermalPrinter::send(m_ticketBytes, printer, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        qCritical() << "Imprimir::printTicket:" << err;
        reportPrinterProblem("No se pudo imprimir el ticket.\n" + err, true);
    }
    return ok;
}

void Imprimir::reportPrinterProblem(const QString &text, bool critical)
{
    if (isVisible()) {
        m_printProblems << text;
        return;
    }
    if (critical)
        QMessageBox::critical(this, "Imprimir", text, QMessageBox::Ok, QMessageBox::Ok);
    else
        QMessageBox::warning(this, "Impresora", text, QMessageBox::Ok, QMessageBox::Ok);
}

void Imprimir::onPrintClicked()
{
    m_printProblems.clear();
    // The window stays open: a QR fetched for the previous print belongs to that invoice.
    qrCode = QPixmap();
    const QString ticket = le_n_ticket->text().trimmed();
    if (ticket.isEmpty() || ticket != selectFromWhereLike(db, "n_recibo", "ingresos", "n_recibo", ticket, true, false)) {
        m_lblResult->setText(UiKit::warnHtml("No se ha encontrado el recibo Nº " + ticket.toHtmlEscaped() + ".")
                             + "<br>Utilice otro número o búsquelo en el listado de ingresos.");
        return;
    }
    const bool printing = AppSettings::instance()->enablePrinting();
    const QString printingOff = printing ? QString()
        : "<br>" + UiKit::warnHtml("La impresión está desactivada en Configuración: no se ha enviado a la impresora.");
    const auto problems = [this]() {
        QStringList lines;
        for (const QString &problem : m_printProblems)
            lines << problem.toHtmlEscaped().replace('\n', "<br>");
        return lines.isEmpty() ? QString() : "<br>" + UiKit::errorHtml(lines.join("<br>"));
    };
    if (isCompleteInvoice && m_leDni->text().trimmed().isEmpty()) {
        m_lblResult->setText(UiKit::warnHtml("Escriba el DNI / NIF del cliente.")
                             + "<br>La factura completa lleva el NIF de quien la pide (art. 7.2 RD 1619/2012).");
        return;
    }
    invoiceSeq = -1;

    // Factura path: enumerate the (seq, invoice_id) pairs that have actually
    // been submitted to AEAT for this n_recibo. A multi-seq ticket cannot be
    // printed as one factura because there is no AEAT submission for the bare
    // <n_recibo> covering the whole. A single-seq ticket - including legacy
    // 8.0-8.4 tickets where every row has seq=0 and an empty invoice_id - is
    // scoped to that seq so getTicketInfo filters out leftover unpaid rows
    // and the printed Nº matches what AEAT has on record.
    QList<QPair<int, QString>> events; // (seq, literal invoice_id; empty = bare n_recibo / legacy)
    if (!isRecibo) {
        ConnectionScope scope(db);
        QSqlQuery sq(db);
        sq.prepare("SELECT verifactu_invoice_seq, "
                   "       COALESCE(MAX(verifactu_invoice_id), '') "
                   "FROM ingresos "
                   "WHERE n_recibo = :n AND verifactu_estado IS NOT NULL "
                   "AND verifactu_estado != '' "
                   "GROUP BY verifactu_invoice_seq "
                   "ORDER BY verifactu_invoice_seq");
        sq.bindValue(":n", ticket);
        if (sq.exec()) {
            while (sq.next())
                events.append({ sq.value(0).toInt(), sq.value(1).toString() });
        }
    }
    const auto labelFor = [&ticket](const QPair<int, QString> &ev) {
        // Authoritative when set: literal column matches what AEAT received.
        // Empty column: reconstruct the AEAT InvoiceID from n_recibo + seq.
        return ev.second.isEmpty() ? verifactuInvoiceId(ticket, ev.first) : ev.second;
    };

    if (events.size() > 1) {
        // Several partial payments: the operator picks one (or all) in the window.
        if (m_eventsTicket != ticket) {
            m_cbEvent->clear();
            for (const auto &ev : events)
                m_cbEvent->addItem(labelFor(ev));
            m_cbEvent->addItem("Todas las facturas");
            m_cbEvent->setCurrentIndex(m_cbEvent->count() - 1);
            m_lblEvent->setVisible(true);
            m_cbEvent->setVisible(true);
            m_eventsTicket = ticket;
            m_lblResult->setText(UiKit::warnHtml(QString("El recibo Nº %1 tiene %2 facturas en AEAT (pagos parciales).")
                                                     .arg(ticket.toHtmlEscaped()).arg(events.size()))
                                 + "<br>Elija cuál imprimir y pulse \"" + m_btnPrint->text() + "\" de nuevo.");
            return;
        }
        QList<QPair<int, QString>> toPrint;
        if (m_cbEvent->currentIndex() >= events.size())
            toPrint = events;
        else
            toPrint << events[m_cbEvent->currentIndex()];
        QStringList printed;
        QStringList notPrinted;
        for (const auto &ev : toPrint) {
            invoiceSeq = ev.first;
            qrCode = QPixmap();
            getTicketInfo();
            if (sqlQueryModel->rowCount() == 0) {
                notPrinted << labelFor(ev) + " (sin prendas pagadas)";
                continue;
            }
            buildTicket(/*copyForClient=*/true, /*addPayedInfo=*/false);
            if (AppSettings::instance()->enablePrinting() && !printTicket()) {
                notPrinted << labelFor(ev) + (m_lastSendUncertain ? " (puede que se imprima: compruebe la impresora)"
                                                                  : " (error de impresión)");
                continue;
            }
            printed << labelFor(ev) + (m_lastVoidedMarker.isEmpty()
                                           ? QString() : " (" + m_lastVoidedMarker.toLower() + ", sin QR)");
        }
        QString summary;
        if (!printed.isEmpty()) {
            const QString text = "Facturas impresas: " + printed.join(", ").toHtmlEscaped() + ".";
            summary = notPrinted.isEmpty() ? UiKit::okHtml(text) : UiKit::warnHtml(text);
        }
        if (!notPrinted.isEmpty())
            summary += (summary.isEmpty() ? QString() : "<br>")
                       + UiKit::warnHtml("No se han impreso: " + notPrinted.join(", ").toHtmlEscaped() + ".");
        m_lblResult->setText(summary + printingOff + problems());
        return;
    }

    // Single-seq factura: scope to the only seq so getTicketInfo filters out
    // any leftover unpaid rows and verifactu_invoice_id resolves from a paid
    // row instead of row 0 (which may be unpaid).
    if (!isRecibo && events.size() == 1)
        invoiceSeq = events.first().first;
    getTicketInfo();
    if (sqlQueryModel->rowCount() == 0) {
        m_lblResult->setText(UiKit::warnHtml("El recibo Nº " + ticket.toHtmlEscaped() + " no tiene prendas que imprimir "
                                             "(todas anuladas)."));
        return;
    }
    if (!isRecibo && !checkAnyItemPaid()) {
        m_lblResult->setText(UiKit::warnHtml("No hay ninguna prenda pagada en el recibo Nº " + ticket.toHtmlEscaped() + ".")
                             + "<br>Una factura solo se emite por lo cobrado; imprima el recibo.");
        return;
    }
    // A reprinted recibo shows the WHOLE ticket, so it may only claim IMPORTE
    // PAGADO when every garment is paid - "any paid" would mark a partially-paid
    // ticket as settled in full. Facturas never carry the marker (payment is
    // implied), same as every other factura path.
    const bool paidMarker = isRecibo && ticketAllGarmentsPaid(db, ticket);
    buildTicket(true, paidMarker);
    const bool clientCopyFailed = printing && !printTicket();
    if (isRecibo) {
        const bool shopCopy = m_chkShopCopy->isChecked();
        if (shopCopy && !clientCopyFailed) {   // no second failure on top of the first
            buildTicket(false, paidMarker);
            if (printing)
                printTicket();
        }
        const QString result = clientCopyFailed
            ? UiKit::warnHtml("El recibo Nº " + ticket.toHtmlEscaped() + " no se ha impreso.")
            : UiKit::okHtml("Recibo Nº " + ticket.toHtmlEscaped() + " impreso"
                            + (shopCopy ? " (copia del cliente y del establecimiento)." : " (copia del cliente)."));
        m_lblResult->setText(result + printingOff + problems());
        return;
    }
    const QString noQr = !m_lastVoidedMarker.isEmpty()
        ? "<br>" + UiKit::warnHtml("La factura está " + m_lastVoidedMarker.toLower()
                                   + ": la copia lo indica y no lleva código QR.")
        : !qrCode.isNull() ? QString()
        : (!verifactuIntegration || !verifactuIntegration->isConfigured())
            ? "<br>Sin código QR: Verifactu no está activado."
            : "<br>Sin código QR: la factura no está confirmada por AEAT o AEAT no respondió a tiempo.";
    const QString invoiceName = (isCompleteInvoice ? "Factura completa " : "Factura ") + displayInvoiceId().toHtmlEscaped();
    m_lblResult->setText((clientCopyFailed ? UiKit::warnHtml(invoiceName + " no se ha impreso.")
                                           : UiKit::okHtml(invoiceName + " impresa."))
                         + noQr + printingOff + problems());
}
