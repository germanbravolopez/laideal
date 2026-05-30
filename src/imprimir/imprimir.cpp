#include "imprimir.h"
#include "sql_lite.h"
#include "appsettings.h"

#include "xlsxdocument.h"
#include "xlsxcellrange.h"

#include <QDate>
#include <QImage>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QEventLoop>
#include <QTextStream>
#include <QTimer>

Imprimir::Imprimir(QWidget *parent)
    : QDialog(parent)
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
    if (invoiceSeq >= 0) {
        q.prepare("SELECT * FROM ingresos WHERE n_recibo = :n_recibo "
                  "AND verifactu_invoice_seq = :seq");
        q.bindValue(":n_recibo", le_n_ticket->text());
        q.bindValue(":seq", invoiceSeq);
    } else {
        q.prepare("SELECT * FROM ingresos WHERE n_recibo = :n_recibo");
        q.bindValue(":n_recibo", le_n_ticket->text());
    }
    q.exec();
    sqlQueryModel->setQuery(std::move(q));
    db.close();
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
    QString invoiceNumber = sqlQueryModel->data(sqlQueryModel->index(0, INGRESOS_COL_VERIFACTU_INVOICE_ID)).toString();
    if (invoiceNumber.isEmpty())
        invoiceNumber = sqlQueryModel->data(sqlQueryModel->index(0, INGRESOS_COL_N_RECIBO)).toString();
    QString dateStr = sqlQueryModel->data(sqlQueryModel->index(0, INGRESOS_COL_FECHA_RECEPCION)).toString();
    QDate invoiceDate = QDate::fromString(dateStr, "dd-MM-yyyy");
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

void Imprimir::createTicketExcel(bool copyForClient, bool addPayedInfo)
{
    // The xlsx is regenerated on every print at a fixed path under the user's
    // home dir - no user-configurable template setting any more.
    QString excelPath = AppSettings::ticketExcelPath();
    if (QFile::exists(excelPath)) {
        QFile::remove(excelPath);
    }
    int row = 1;
    QXlsx::Document excel(excelPath);
    excel.setColumnWidth(1, 4);
    excel.setColumnWidth(2, 20);
    excel.setColumnWidth(3, 7.5);
    excel.setPageMargins(0.2, 0.1, 0.1, 0.4);

    // Header of the ticket
    AppSettings *settings = AppSettings::instance();
    QXlsx::Format formatBigTitle;
    formatBigTitle.setFontSize(22);
    formatBigTitle.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
    excel.write(row, 1, settings->businessName(), formatBigTitle);
    row++;

    QXlsx::Format formatHeaderBold;
    formatHeaderBold.setFontSize(11);
    formatHeaderBold.setFontBold(true);
    excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
    excel.write(row, 1, "     " + settings->verifactuName(), formatHeaderBold);
    row++;
    excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
    excel.write(row, 1, "     NIF: " + settings->verifactuNif(), formatHeaderBold);
    row++;
    excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
    excel.write(row, 1, "     " + settings->businessAddress(), formatHeaderBold);
    row++;
    excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
    excel.write(row, 1, "     " + settings->businessCity(), formatHeaderBold);
    row++;
    formatHeaderBold.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    formatHeaderBold.setBottomBorderStyle(QXlsx::Format::BorderDouble);
    excel.setRowHeight(row, 24);
    excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
    excel.write(row, 1, "Tlf: " + settings->businessPhone(), formatHeaderBold);
    excel.write(row, 2, "", formatHeaderBold);
    excel.write(row, 3, "", formatHeaderBold);
    row++;

    // Start filling with current data
    if (isRecibo) {
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("Recibo"));
        row++;
    } else {
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("FACTURA SIMPLIFICADA"));
        row++;
    }
    // N_recibo
    excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
    QXlsx::Format formatBoldRightAlign;
    formatBoldRightAlign.setFontBold(true);
    formatBoldRightAlign.setHorizontalAlignment(QXlsx::Format::AlignRight);
    // Read the literal AEAT InvoiceID from the loaded rows (8.5+); empty on
    // legacy 8.0-8.4 rows that were submitted as bare n_recibo.
    QString displayInvoiceId = sqlQueryModel->rowCount() > 0
        ? sqlQueryModel->data(sqlQueryModel->index(0, INGRESOS_COL_VERIFACTU_INVOICE_ID)).toString()
        : QString();
    if (displayInvoiceId.isEmpty())
        displayInvoiceId = le_n_ticket->text();
    excel.write(row, 1, QString("Nº: " + displayInvoiceId), formatBoldRightAlign);
    row++;
    // Client data
    excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
    excel.write(row, 1, QString("Cliente: " + sqlQueryModel->data(sqlQueryModel->index(0 , INGRESOS_COL_CLIENTE)).toString()));
    row++;
    // Ticket dates
    excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
    excel.write(row, 1, QString("Recepción: " + sqlQueryModel->data(sqlQueryModel->index(0 , INGRESOS_COL_FECHA_RECEPCION)).toString().replace("-","/")));
    row++;
    // Extra information in case of complete invoice: address and DNI
    if (isCompleteInvoice) {
        QString clientAddress;
        clientAddress = searchItemFromClient(db, "direccion", sqlQueryModel->data(sqlQueryModel->index(0 , INGRESOS_COL_CLIENTE)).toString(), false);
        if (clientAddress == "")
            clientAddress = addExtraInfoToInvoice("Añadir dirección de facturación", "Dirección:");
        // Cut address string in case it is too long
        if (clientAddress.size() >= 42) {
            excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
            excel.write(row, 1, QString("Dirección: " + clientAddress.left(41)));
            row++;
            excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
            excel.write(row, 1, QString("Dirección: " + clientAddress.mid(41)));
            row++;
        } else {
            excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
            excel.write(row, 1, QString("Dirección: " + clientAddress));
            row++;
        }
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("DNI: " + addExtraInfoToInvoice("Añadir DNI", "DNI:")));
        row++;
    }
    // Add double line empty row
    excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
    QXlsx::Format formatDoubleLine;
    formatDoubleLine.setBottomBorderStyle(QXlsx::Format::BorderDouble);
    excel.write(row, 1, "", formatDoubleLine);
    excel.write(row, 2, "", formatDoubleLine);
    excel.write(row, 3, "", formatDoubleLine);
    row++;
    // Add header for garments table
    QXlsx::Format formatSingleLine;
    formatSingleLine.setBottomBorderStyle(QXlsx::Format::BorderThin);
    excel.write(row, 1, "Uds.", formatSingleLine);
    excel.write(row, 2, "Prendas", formatSingleLine);
    excel.write(row, 3, "Importe", formatSingleLine);
    row++;
    // Configure format for each column of garments table
    QXlsx::Format formatUdsCost, formatGarm;
    formatUdsCost.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    formatUdsCost.setVerticalAlignment(QXlsx::Format::AlignTop);
    formatGarm.setTextWrap(true);
    // Add garments
    float ticketTotalF = 0.0;
    for (int rowCnt = 0; rowCnt < sqlQueryModel->rowCount(); rowCnt++) {
        if (isRecibo || !isRecibo && checkTicketPaid(rowCnt)) {
            QString garmentName = sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_PRENDA)).toString();
            // Complete garment with size info
            QString size = QString::number(sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_SIZE)).toFloat(), 'f', 2);
            if (size != "" && size != "0.00") {
                garmentName.append(" - " + size);
            }
            //// Complete "Alfombra" getting also the observation value (number)
            //QString leftSide = garmentName.left(8);
            //QString obsv = sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_OBSERVACIONES)).toString();
            //if (leftSide == "Alfombra" && obsv != "") {
            //    garmentName.append(" - " + obsv);
            //}
            // Content of each garment in the ticket
            excel.write(row, 1, sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_CANTIDAD)).toString(), formatUdsCost);
            excel.write(row, 2, garmentName, formatGarm);
            excel.write(row, 3, QString::number(sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_IMPORTE)).toFloat(), 'f', 2), formatUdsCost);
            row++;
            //// Add extra information for each garment for facturas simplificadas
            //if (!isRecibo && sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_PAGADO)).toString() == "SI") {
            //    formatGarm.setFontSize(10);
            //    excel.write(row, 2, "Pagad.: " + sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_FECHA_PAGO)).toString().replace("-","/"), formatGarm);
            //    formatGarm.setFontSize(11);
            //    row++;
            //    if (sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_ESTADO)).toString() == "Recogido") {
            //        formatGarm.setFontSize(10);
            //        excel.write(row, 2, "Recog.: " + sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_FECHA_RECOGIDA)).toString().replace("-","/"), formatGarm);
            //        formatGarm.setFontSize(11);
            //        row++;
            //    }
            //}
            ticketTotalF = ticketTotalF + sqlQueryModel->data(sqlQueryModel->index(rowCnt, INGRESOS_COL_IMPORTE)).toFloat();
        }
    }
    // Calculate total price and IVA
    float ivaRate = static_cast<float>(AppSettings::instance()->ivaRate());
    QString ticketTotal = QString::number(ticketTotalF, 'f', 2);
    QString baseImponible = QString::number(ticketTotalF / (1.0f + ivaRate / 100.0f), 'f', 2);
    QString iva = QString::number(ticketTotalF - baseImponible.toFloat(), 'f', 2);
    // Add totals
    QXlsx::Format formatTotals;
    formatTotals.setFontBold(true);
    formatTotals.setTopBorderStyle(QXlsx::Format::BorderThin);
    formatTotals.setBottomBorderStyle(QXlsx::Format::BorderDouble);
    formatTotals.setHorizontalAlignment(QXlsx::Format::AlignRight);
    if (isRecibo) {
        // Write total text
        excel.mergeCells("A" + QString::number(row) + ":B" + QString::number(row));
        excel.write(row, 1, QString("TOTAL (IVA incl.):"), formatTotals);
        excel.write(row, 2, "", formatTotals);
        // Write total cost
        formatTotals.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
        excel.write(row, 3, ticketTotal, formatTotals);
        row++;
    } else {
        // Write base text
        formatTotals.setHorizontalAlignment(QXlsx::Format::AlignRight);
        formatTotals.setBottomBorderStyle(QXlsx::Format::BorderNone);
        excel.mergeCells("A" + QString::number(row) + ":B" + QString::number(row));
        excel.write(row, 1, QString("Base Imponible:"), formatTotals);
        excel.write(row, 2, "", formatTotals);
        // Write base cost
        formatTotals.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
        excel.write(row, 3, baseImponible, formatTotals);
        row++;
        // Write iva text
        formatTotals.setHorizontalAlignment(QXlsx::Format::AlignRight);
        formatTotals.setTopBorderStyle(QXlsx::Format::BorderNone);
        excel.mergeCells("A" + QString::number(row) + ":B" + QString::number(row));
        excel.write(row, 1, QString("IVA (%1%):").arg(ivaRate, 0, 'f', 0), formatTotals);
        // Write iva cost
        formatTotals.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
        excel.write(row, 3, iva, formatTotals);
        row++;
        // Write total text
        formatTotals.setHorizontalAlignment(QXlsx::Format::AlignRight);
        formatTotals.setBottomBorderStyle(QXlsx::Format::BorderDouble);
        excel.mergeCells("A" + QString::number(row) + ":B" + QString::number(row));
        excel.write(row, 1, QString("TOTAL:"), formatTotals);
        excel.write(row, 2, "", formatTotals);
        // Write total cost
        formatTotals.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
        excel.write(row, 3, ticketTotal, formatTotals);
        row++;
    }
    // Insert receipt information
    if (addPayedInfo) {
        QXlsx::Format formatPayed;
        formatPayed.setHorizontalAlignment(QXlsx::Format::AlignRight);
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("IMPORTE PAGADO"), formatPayed);
        row++;
    } else {
        excel.setRowHeight(row, 9.6);
        row++; // Add empty row

    }
    if (isRecibo && copyForClient) {
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("(Copia para el cliente)"));
    } else if (isRecibo && !copyForClient) {
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("(Copia para el establecimiento)"));
    }
    row ++;
    // Insert Verifactu QR code at the bottom
    QPixmap qr = resolveQrCode();
    if (!qr.isNull()) {
        QImage qrImg = qr.scaled(140, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation).toImage();
        excel.insertImage(row, 1, qrImg);
        row = row + 9;
        // Disp. Final Primera RD 1007/2023: invoices submitted in Veri*factu mode must
        // print the verification leyenda alongside the QR. Only emit it for rows actually
        // accepted by AEAT (estado = ENVIADA) so we never claim verifiability for tickets
        // still PENDIENTE, in ERROR, or ANULADA.
        const QString verifactuState = sqlQueryModel->data(
            sqlQueryModel->index(0, INGRESOS_COL_VERIFACTU_ESTADO)).toString();
        if (verifactuState == verifactuEstadoToString(VerifactuEstado::Enviada)) {
            QXlsx::Format formatVerifactuLeyenda;
            formatVerifactuLeyenda.setFontSize(7);
            formatVerifactuLeyenda.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
            excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
            excel.write(row, 1, QString("Factura verificable en la sede electrónica de la AEAT"),
                        formatVerifactuLeyenda);
            excel.setRowHeight(row, 9.6);
            row++;
        }
    }
    excel.setRowHeight(row, 9.6); // Add empty row
    row++;
    // Insert policy - only on the client copy; the establishment copy omits it
    if (copyForClient) {
        QXlsx::Format formatPolicy;
        formatPolicy.setFontSize(7);
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("CONDICIONES GENERALES (RD 1453/1987)"), formatPolicy);
        excel.setRowHeight(row, 9.6);
        row++;
        formatPolicy.setFontSize(5);
        // Clause 1: receipt presentation / identity verification
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("- El recibo deberá presentarse al retirar la prenda. Su pérdida no impide la recogida,"), formatPolicy);
        excel.setRowHeight(row, 6.6);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("pero el cliente acreditará su identidad."), formatPolicy);
        excel.setRowHeight(row, 6.6);
        row++;
        // Clause 2: storage obligation (3 months free, 6 months limit - Art. 6 RD 1453/1987)
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("- Las prendas no recogidas en 3 MESES desde la entrega podrán devengar gastos"), formatPolicy);
        excel.setRowHeight(row, 6.6);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("de custodia. Transcurridos 6 MESES el establecimiento queda liberado de la"), formatPolicy);
        excel.setRowHeight(row, 6.6);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("obligación de custodia."), formatPolicy);
        excel.setRowHeight(row, 6.6);
        row++;
        // Clause 3: accessories - disclaimer applies when noted on this receipt
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("- No se responde de botones, adornos, ni accesorios salvo anotación en este recibo."), formatPolicy);
        excel.setRowHeight(row, 6.6);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("Se recomienda retirarlos antes de entregar."), formatPolicy);
        excel.setRowHeight(row, 6.6);
        row++;
        // Clause 4: pre-cleaning advisory obligation (Art. 6 RD 1453/1987)
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("- Si el estado de la prenda implica riesgo de daño o resultado incierto, se"), formatPolicy);
        excel.setRowHeight(row, 6.6);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("informará al cliente antes de proceder al tratamiento."), formatPolicy);
        excel.setRowHeight(row, 6.6);
        row++;
        // RGPD first-layer notice (LOPDGDD Art. 11 / RGPD Art. 13)
        QString rgpdContact = settings->businessPhone().isEmpty()
                              ? QString("ver cartel en tienda")
                              : settings->businessPhone();
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("- Sus datos son tratados por " + settings->businessName() + " para gestionar el servicio."), formatPolicy);
        excel.setRowHeight(row, 6.6);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("RGPD - Derechos arts.15-22: ") + rgpdContact + ".", formatPolicy);
        excel.setRowHeight(row, 6.6);
        row++;
    }
    // Insert timestamp
    QXlsx::Format formatStamp;
    formatStamp.setFontSize(7);
    formatStamp.setTopBorderStyle(QXlsx::Format::BorderThin);
    excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
    excel.write(row, 1, QDateTime::currentDateTime().toString("dd/MM/yyyy - hh:mm:ss"), formatStamp);
    excel.write(row, 2, "", formatStamp);
    excel.write(row, 3, "", formatStamp);
    row++;
    // Save file
    excel.save();
}

void Imprimir::printTicket()
{
    // Open the regenerated xlsx via Excel COM (VBScript) and send it to the
    // default printer. We regenerate the .vbs each call (templated with the
    // current xlsx path) instead of shipping a separate .bat / settings entry.
    const QString xlsxPath = QDir::toNativeSeparators(AppSettings::ticketExcelPath());
    const QString vbsPath  = AppSettings::ticketPrintScriptPath();

    QFile vbs(vbsPath);
    if (!vbs.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCritical() << "Imprimir::printTicket: cannot write VBScript to" << vbsPath;
        QMessageBox::critical(this, "Imprimir",
                              "No se pudo preparar el script de impresión.",
                              QMessageBox::Ok, QMessageBox::Ok);
        return;
    }
    QTextStream out(&vbs);
    out << "Set objExcel = CreateObject(\"Excel.Application\")\r\n"
        << "objExcel.Visible = False\r\n"
        << "Set objWorkbook = objExcel.Workbooks.Open(\"" << xlsxPath << "\")\r\n"
        << "objWorkbook.PrintOut\r\n"
        << "objWorkbook.Close False\r\n"
        << "objExcel.Quit\r\n";
    vbs.close();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QProcess process;
    process.start("cscript", {"//nologo", "//B", vbsPath});
    process.waitForFinished(60000);
    QApplication::restoreOverrideCursor();
}

void Imprimir::on_bb_ok_cancel_accepted()
{
    if (le_n_ticket->text() == selectFromWhereLike(db, "n_recibo", "ingresos", "n_recibo", le_n_ticket->text(), true, true)) {
        getTicketInfo();
        if (isRecibo || !isRecibo && checkAnyItemPaid()) {
            createTicketExcel(true, false);
            if (AppSettings::instance()->enablePrinting()) {
                printTicket();
            }
            if (isRecibo) {
                int resp = QMessageBox::question(this, "Copia establecimiento",
                                                 "¿Desea copia para el establecimiento?",
                                                 QMessageBox::Yes | QMessageBox::No,
                                                 QMessageBox::Yes);
                if (resp == QMessageBox::Yes) {
                    createTicketExcel(false, false);
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
    } else {
        QMessageBox::information(this, "Imprimir",
                              "El número de recibo " + le_n_ticket->text() + " no se ha encontrado en la base de datos.\n"
                              "Utilizar otro número o buscarlo en la lista de ingresos.",
                              QMessageBox::Ok,
                              QMessageBox::Ok);
    }
}

void Imprimir::on_bb_ok_cancel_rejected()
{
    this->close();
}
