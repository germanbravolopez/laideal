#include "imprimir.h"
#include "sql_lite.h"
#include "appsettings.h"

#include "ticketrenderer.h"
#include "thermalprinter.h"
#include "statusapiprinter.h"

#include <QDate>
#include <QImage>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

Imprimir::Imprimir(const QSqlDatabase &database, QWidget *parent)
    : QDialog(parent), db(database)
{
    setupUi(this);
}

void Imprimir::setupUi(QDialog *Imprimir)
{
    if (Imprimir->objectName().isEmpty())
        Imprimir->setObjectName("Imprimir");
    Imprimir->setWindowModality(Qt::WindowModal);
    Imprimir->resize(190, 90);
    QFont font;
    font.setPointSize(12);
    Imprimir->setFont(font);
    Imprimir->setLocale(QLocale(QLocale::Spanish, QLocale::Spain));
    formLayout = new QFormLayout(Imprimir);
    formLayout->setObjectName("formLayout");
    lbl_n_ticket = new QLabel(Imprimir);
    lbl_n_ticket->setObjectName("lbl_n_ticket");

    formLayout->setWidget(0, QFormLayout::LabelRole, lbl_n_ticket);

    le_n_ticket = new QLineEdit(Imprimir);
    le_n_ticket->setObjectName("le_n_ticket");

    formLayout->setWidget(0, QFormLayout::FieldRole, le_n_ticket);

    bb_ok_cancel = new QDialogButtonBox(Imprimir);
    bb_ok_cancel->setObjectName("bb_ok_cancel");
    bb_ok_cancel->setOrientation(Qt::Horizontal);
    bb_ok_cancel->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

    formLayout->setWidget(1, QFormLayout::SpanningRole, bb_ok_cancel);

    Imprimir->setWindowTitle(QCoreApplication::translate("Imprimir", "Dialog", nullptr));
    lbl_n_ticket->setText(QCoreApplication::translate("Imprimir", "N. Ticket:", nullptr));
    QObject::connect(bb_ok_cancel, &QDialogButtonBox::accepted, Imprimir, qOverload<>(&QDialog::accept));
    QObject::connect(bb_ok_cancel, &QDialogButtonBox::rejected, Imprimir, qOverload<>(&QDialog::reject));

    QMetaObject::connectSlotsByName(Imprimir);
}

void Imprimir::getTicketInfo()
{
    sqlQueryModel = new QSqlQueryModel;
    db.open();
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
        q.bindValue(":n_recibo", le_n_ticket->text());
        q.bindValue(":seq", invoiceSeq);
        q.bindValue(":anulado", QStringLiteral(INGRESOS_ESTADO_ANULADO));
    } else {
        q.prepare("SELECT * FROM ingresos WHERE n_recibo = :n_recibo "
                  "AND (estado IS NULL OR estado != :anulado)");
        q.bindValue(":n_recibo", le_n_ticket->text());
        q.bindValue(":anulado", QStringLiteral(INGRESOS_ESTADO_ANULADO));
    }
    q.exec();
    sqlQueryModel->setQuery(std::move(q));
    db.close();
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
    return verifactuDisplayInvoiceId(invoiceIds, le_n_ticket->text());
}

bool Imprimir::checkTicketPaid(int row)
{
    if (sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_PAGADO)).toString() == "NO")
        return false;
    return true;
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
        if (e == VerifactuEstado::Anulada || e == VerifactuEstado::Rectificada || e == VerifactuEstado::Error) {
            anyBlocking = true;
            break;
        }
    }
    if (!hasCsv || anyBlocking)
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
        invoiceDate = QDate::fromString(
            sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_FECHA_PAGO)).toString(), "dd-MM-yyyy");
        if (invoiceDate.isValid()) break;
    }
    if (!invoiceDate.isValid())
        return QPixmap();

    double total = 0.0;
    for (int row = 0; row < sqlQueryModel->rowCount(); row++) {
        total += sqlQueryModel->data(sqlQueryModel->index(row, INGRESOS_COL_IMPORTE)).toDouble();
    }

    double ivaRate = AppSettings::instance()->ivaRate();
    double taxBase = total / (1.0 + ivaRate / 100.0);

    // Reprint path: fire async QR fetch and wait up to 5s in a local event loop.
    // If AEAT does not reply in time, print without QR (logged warning).
    const QString reqId = verifactuIntegration->generateQRAsync(
        invoiceNumber, invoiceDate, taxBase, ivaRate, "Servicios de lavanderia");
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

QString Imprimir::addExtraInfoToInvoice(QString title, QString request)
{
    bool ok;
    QString text = QInputDialog::getText(this, title,
                                         request, QLineEdit::Normal,
                                         "", &ok);
    if (ok)
        return text;
    else
        return "";
}

int Imprimir::paperDots() const
{
    // 80 mm roll = 576 printable dots, 58 mm = 420 (203 dpi). Anything other
    // than 58 maps to the 80 mm default.
    return AppSettings::instance()->paperWidthMm() == 58 ? 420 : 576;
}

void Imprimir::buildTicket(bool copyForClient, bool addPayedInfo)
{
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
    d.invoiceId         = displayInvoiceId();
    const QString client = sqlQueryModel->data(sqlQueryModel->index(0, INGRESOS_COL_CLIENTE)).toString();
    d.clientName     = client;
    d.receptionDate  = sqlQueryModel->data(sqlQueryModel->index(0, INGRESOS_COL_FECHA_RECEPCION)).toString().replace("-", "/");

    // Complete invoice: billing address (looked up, else prompted) + DNI (prompted).
    if (isCompleteInvoice) {
        QString clientAddress = searchItemFromClient(db, "direccion", client, false);
        if (clientAddress.isEmpty())
            clientAddress = addExtraInfoToInvoice("Añadir dirección de facturación", "Dirección:");
        d.clientAddress = clientAddress;
        d.clientDni     = addExtraInfoToInvoice("Añadir DNI", "DNI:");
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
        if (size != "" && size != "0.00")
            garmentName.append(" - " + size);
        g.name = garmentName;
        const double importe = sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_IMPORTE)).toDouble();
        g.amount = QString::number(importe, 'f', 2);
        d.garments.append(g);
        total += importe;
    }
    d.total   = total;
    d.ivaRate = settings->ivaRate();

    d.addPayedInfo  = addPayedInfo;
    d.copyForClient = copyForClient;

    // Verifactu QR - facturas only. Recibos are claim tickets, not tax documents:
    // they carry no Verifactu metadata, so a QR would mislead the customer into
    // thinking the receipt was submitted to AEAT. Raster the AEAT-issued pixmap
    // verbatim so the printed QR is byte-exact with what AEAT registered.
    const QPixmap qr = !isRecibo ? resolveQrCode() : QPixmap();
    if (!qr.isNull()) {
        d.qr = qr.scaled(192, 192, Qt::KeepAspectRatio, Qt::SmoothTransformation).toImage();
        // Disp. Final Primera RD 1007/2023: print the verification leyenda only
        // for rows AEAT actually accepted (estado = ENVIADA), never for tickets
        // still PENDIENTE, in ERROR, or ANULADA.
        const QString verifactuState = sqlQueryModel->data(
            sqlQueryModel->index(0, INGRESOS_COL_VERIFACTU_ESTADO)).toString();
        d.verifactuVerifiable = (verifactuState == verifactuEstadoToString(VerifactuEstado::Enviada));
    }

    d.timestamp = QDateTime::currentDateTime().toString("dd/MM/yyyy - hh:mm:ss");

    m_ticketBytes = TicketRenderer::render(d, paperDots());
}

void Imprimir::printTicket()
{
    // Send the ESC/POS bytes built by buildTicket() straight to the printer
    // queue as RAW spool data - no Excel, no .vbs, no cscript.
    if (m_ticketBytes.isEmpty()) {
        qWarning() << "Imprimir::printTicket: no ticket built";
        return;
    }
    const QString printer = AppSettings::instance()->printerName();
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Optional Status API path: read the device status (Epson EPSStmApi.dll) and
    // send the same bytes through it. sendAndReadStatus() reads status BEFORE the
    // send, so a cover-open / paper-out is surfaced here even though it makes the
    // send itself fail. If the DLL/APD isn't installed it reports failure with an
    // empty status and we fall through to the RAW spooler, so enabling the flag
    // never stops printing.
    if (AppSettings::instance()->useStatusApi()) {
        PrinterStatus status;
        QString statusErr;
        // Bounded call: the Epson DLL runs on a worker with a 10 s watchdog so a
        // printer firmware/driver quirk that hangs a Bi* call can never freeze the
        // POS UI - on timeout it falls back to RAW and disables the API for the
        // session. 10 s sits well above the real worst case: a cover-open/no-paper
        // send returns ERR_ACCESS after the DLL's own internal ~5 s (it does NOT
        // honour our BiDirectIOEx timeout on that path), ~5.5 s total. A genuine
        // hang is indefinite, so the wide margin distinguishes the two and a normal
        // offline print is never mistaken for a hang. See docs/modules/printing.md.
        const bool sent = StatusApiPrinter::sendAndReadStatusBounded(
            m_ticketBytes, printer, &status, &statusErr, 10000);
        QApplication::restoreOverrideCursor();
        bool warned = false;
        if (status.valid && (status.hasError() || status.hasWarning())) {
            qWarning() << "Imprimir::printTicket status:" << status.summary()
                       << Qt::hex << status.raw;
            QMessageBox::warning(this, "Impresora", status.summary(),
                                 QMessageBox::Ok, QMessageBox::Ok);
            warned = true;
        }
        // Fatal fault (cutter/mechanical/unrecoverable): do not queue the ticket -
        // the operator must fix the printer and reprint.
        if (status.valid && status.isFatal())
            return;
        if (sent)
            return;
        // The send was refused but the ASB did not decode to a known fault: the
        // TM-T20III reports an open cover / offline as ASB_NO_RESPONSE (0x1), not
        // ASB_COVER_OPEN. When we did reach the printer's status (status.valid),
        // warn generically so the operator gets feedback; a missing DLL/API leaves
        // status invalid and stays silent (RAW alone works there).
        if (status.valid && !warned) {
            QMessageBox::warning(this, "Impresora",
                "No se pudo enviar a la impresora.\n"
                "Comprueba que la tapa está cerrada, que hay papel y la conexión.",
                QMessageBox::Ok, QMessageBox::Ok);
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
        QMessageBox::critical(this, "Imprimir",
                              "No se pudo imprimir el ticket.\n" + err,
                              QMessageBox::Ok, QMessageBox::Ok);
    }
}

void Imprimir::on_bb_ok_cancel_accepted()
{
    if (le_n_ticket->text() != selectFromWhereLike(db, "n_recibo", "ingresos", "n_recibo", le_n_ticket->text(), true, true)) {
        QMessageBox::information(this, "Imprimir",
                              "El número de recibo " + le_n_ticket->text() + " no se ha encontrado en la base de datos.\n"
                              "Utilizar otro número o buscarlo en la lista de ingresos.",
                              QMessageBox::Ok,
                              QMessageBox::Ok);
        return;
    }

    // Factura path: enumerate the (seq, invoice_id) pairs that have actually
    // been submitted to AEAT for this n_recibo. A multi-seq ticket cannot be
    // printed as one factura because there is no AEAT submission for the bare
    // <n_recibo> covering the whole. A single-seq ticket - including legacy
    // 8.0-8.4 tickets where every row has seq=0 and an empty invoice_id - is
    // scoped to that seq so getTicketInfo filters out leftover unpaid rows
    // and the printed Nº matches what AEAT has on record.
    QList<QPair<int, QString>> events; // (seq, literal invoice_id; empty = bare n_recibo / legacy)
    if (!isRecibo) {
        db.open();
        QSqlQuery sq(db);
        sq.prepare("SELECT verifactu_invoice_seq, "
                   "       COALESCE(MAX(verifactu_invoice_id), '') "
                   "FROM ingresos "
                   "WHERE n_recibo = :n AND verifactu_estado IS NOT NULL "
                   "AND verifactu_estado != '' "
                   "GROUP BY verifactu_invoice_seq "
                   "ORDER BY verifactu_invoice_seq");
        sq.bindValue(":n", le_n_ticket->text());
        if (sq.exec()) {
            while (sq.next())
                events.append({ sq.value(0).toInt(), sq.value(1).toString() });
        }
        db.close();
    }

    auto labelFor = [this](const QPair<int, QString> &ev) {
        // Authoritative when set: literal column matches what AEAT received.
        if (!ev.second.isEmpty()) return ev.second;
        // Empty column: reconstruct the AEAT InvoiceID from n_recibo + seq
        // (bare for seq 0 legacy/save-time, <n>-<seq> for a PayDialog event).
        return verifactuInvoiceId(le_n_ticket->text(), ev.first);
    };

    if (events.size() > 1) {
        QStringList options;
        for (const auto &ev : events)
            options << labelFor(ev);
        const QString allOption = tr("Imprimir todas");
        options << allOption;
        bool ok = false;
        const QString choice = QInputDialog::getItem(this, tr("Múltiples pagos parciales"),
            tr("Este ticket tiene %1 facturas distintas en AEAT.\n¿Cuál imprimir?").arg(events.size()),
            options, options.size() - 1, /*editable=*/false, &ok);
        if (!ok) return;

        QList<QPair<int, QString>> toPrint;
        if (choice == allOption) {
            toPrint = events;
        } else {
            toPrint << events[options.indexOf(choice)];
        }
        for (const auto &ev : toPrint) {
            invoiceSeq = ev.first;
            getTicketInfo();
            buildTicket(/*copyForClient=*/true, /*addPayedInfo=*/false);
            if (AppSettings::instance()->enablePrinting())
                printTicket();
        }
        return;
    }

    // Single-seq factura: scope to the only seq so getTicketInfo filters out
    // any leftover unpaid rows and verifactu_invoice_id resolves from a paid
    // row instead of row 0 (which may be unpaid).
    if (!isRecibo && events.size() == 1)
        invoiceSeq = events.first().first;
    getTicketInfo();
    if (isRecibo || (!isRecibo && checkAnyItemPaid())) {
        buildTicket(true, false);
        if (AppSettings::instance()->enablePrinting()) {
            printTicket();
        }
        if (isRecibo) {
            int resp = QMessageBox::question(this, "Copia establecimiento",
                                             "¿Desea copia para el establecimiento?",
                                             QMessageBox::Yes | QMessageBox::No,
                                             QMessageBox::Yes);
            if (resp == QMessageBox::Yes) {
                buildTicket(false, false);
                if (AppSettings::instance()->enablePrinting()) {
                    printTicket();
                }
            }
        }
    } else if (!isRecibo)
        QMessageBox::information(this, "Imprimir",
                                 "No hay ninguna prenda pagada en el recibo " + le_n_ticket->text() + ".",
                                 QMessageBox::Ok,
                                 QMessageBox::Ok);
}

void Imprimir::on_bb_ok_cancel_rejected()
{
    this->close();
}
