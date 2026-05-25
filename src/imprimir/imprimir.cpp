#include "imprimir.h"
#include "sql_lite.h"
#include "appsettings.h"

#include "xlsxdocument.h"
#include "xlsxcellrange.h"

#include <QDate>
#include <QImage>
#include <QDebug>
#include <QFile>
#include <QEventLoop>
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
    q.prepare("SELECT * FROM ingresos WHERE n_recibo = :n_recibo");
    q.bindValue(":n_recibo", le_n_ticket->text());
    q.exec();
    sqlQueryModel->setQuery(std::move(q));
    db.close();
}

bool Imprimir::checkTicketPaid(int row)
{
    if (sqlQueryModel->data(sqlQueryModel->index(row, TABLE_IS_PAYED)).toString() == "NO")
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

    QString csv = sqlQueryModel->data(sqlQueryModel->index(0, TABLE_VERIFACTU_CSV)).toString();
    if (csv.isEmpty())
        return QPixmap();

    QString invoiceNumber = sqlQueryModel->data(sqlQueryModel->index(0, TABLE_TICKET)).toString();
    QString dateStr = sqlQueryModel->data(sqlQueryModel->index(0, TABLE_DATE_RCP)).toString();
    QDate invoiceDate = QDate::fromString(dateStr, "dd-MM-yyyy");
    if (!invoiceDate.isValid())
        return QPixmap();

    double total = 0.0;
    for (int row = 0; row < sqlQueryModel->rowCount(); row++) {
        total += sqlQueryModel->data(sqlQueryModel->index(row, TABLE_PRICE)).toDouble();
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
    // Check that excel is accessible
    QString excelPath = AppSettings::instance()->printTemplatePath();
    if (QFile::exists(excelPath)) {
        QFile::remove(excelPath);
    }
    int row = 1;
    QXlsx::Document excel(excelPath);
    excel.setColumnWidth(1, 4);
    excel.setColumnWidth(2, 20);
    excel.setColumnWidth(3, 7);

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
    excel.write(row, 1, QString("Nº: " + le_n_ticket->text()), formatBoldRightAlign);
    row++;
    // Client data
    excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
    excel.write(row, 1, QString("Cliente: " + sqlQueryModel->data(sqlQueryModel->index(0 , TABLE_CLIENT)).toString()));
    row++;
    // Ticket dates
    excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
    excel.write(row, 1, QString("Recepción: " + sqlQueryModel->data(sqlQueryModel->index(0 , TABLE_DATE_RCP)).toString().replace("-","/")));
    row++;
    // Extra information in case of complete invoice: address and DNI
    if (isCompleteInvoice) {
        QString clientAddress;
        clientAddress = searchItemFromClient(db, "direccion", sqlQueryModel->data(sqlQueryModel->index(0 , TABLE_CLIENT)).toString(), false);
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
            QString garmentName = sqlQueryModel->data(sqlQueryModel->index(rowCnt, TABLE_GARMENT)).toString();
            // Complete garment with size info
            QString size = QString::number(sqlQueryModel->data(sqlQueryModel->index(rowCnt, TABLE_SIZE)).toFloat(), 'f', 2);
            if (size != "" && size != "0.00") {
                garmentName.append(" - " + size);
            }
            //// Complete "Alfombra" getting also the observation value (number)
            //QString leftSide = garmentName.left(8);
            //QString obsv = sqlQueryModel->data(sqlQueryModel->index(rowCnt, TABLE_OBSERV)).toString();
            //if (leftSide == "Alfombra" && obsv != "") {
            //    garmentName.append(" - " + obsv);
            //}
            // Content of each garment in the ticket
            excel.write(row, 1, sqlQueryModel->data(sqlQueryModel->index(rowCnt, TABLE_QUANTITY)).toString(), formatUdsCost);
            excel.write(row, 2, garmentName, formatGarm);
            excel.write(row, 3, QString::number(sqlQueryModel->data(sqlQueryModel->index(rowCnt, TABLE_PRICE)).toFloat(), 'f', 2), formatUdsCost);
            row++;
            //// Add extra information for each garment for facturas simplificadas
            //if (!isRecibo && sqlQueryModel->data(sqlQueryModel->index(rowCnt, TABLE_IS_PAYED)).toString() == "SI") {
            //    formatGarm.setFontSize(10);
            //    excel.write(row, 2, "Pagad.: " + sqlQueryModel->data(sqlQueryModel->index(rowCnt, TABLE_DATE_PAY)).toString().replace("-","/"), formatGarm);
            //    formatGarm.setFontSize(11);
            //    row++;
            //    if (sqlQueryModel->data(sqlQueryModel->index(rowCnt, TABLE_STATE)).toString() == "Recogido") {
            //        formatGarm.setFontSize(10);
            //        excel.write(row, 2, "Recog.: " + sqlQueryModel->data(sqlQueryModel->index(rowCnt, TABLE_DATE_PKU)).toString().replace("-","/"), formatGarm);
            //        formatGarm.setFontSize(11);
            //        row++;
            //    }
            //}
            ticketTotalF = ticketTotalF + sqlQueryModel->data(sqlQueryModel->index(rowCnt, TABLE_PRICE)).toFloat();
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
    // Insert Verifactu QR code at the bottom
    QPixmap qr = resolveQrCode();
    if (!qr.isNull()) {
        QImage qrImg = qr.scaled(140, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation).toImage();
        excel.insertImage(row, 1, qrImg);
        row = row + 8;
    }
    // Insert receipt information
    if (addPayedInfo) {
        QXlsx::Format formatPayed;
        formatPayed.setHorizontalAlignment(QXlsx::Format::AlignRight);
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("IMPORTE PAGADO"), formatPayed);
        row++;
    } else
        row++; // Add empty row
    if (isRecibo && copyForClient) {
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("(Copia para el cliente)"));
        row += 2;
    } else if (isRecibo && !copyForClient) {
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("(Copia para el establecimiento)"));
        row += 2;
    }
    // Insert policy - only on the client copy; the establishment copy omits it
    if (copyForClient) {
        QXlsx::Format formatPolicy;
        formatPolicy.setFontSize(10);
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("CONDICIONES GENERALES."), formatPolicy);
        row++;
        formatPolicy.setFontSize(9);
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("(RD 1453/1987)"), formatPolicy);
        row++;
        // Clause 1: receipt presentation / identity verification
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("- El recibo deberá presentarse al retirar la"), formatPolicy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("prenda. Su pérdida no impide la recogida;"), formatPolicy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("el cliente acreditará su identidad."), formatPolicy);
        row++;
        // Clause 2: storage obligation (3 months free, 6 months limit - Art. 6 RD 1453/1987)
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("- Las prendas no recogidas en 3 MESES"), formatPolicy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("desde la entrega podrán devengar gastos"), formatPolicy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("de custodia. Transcurridos 6 MESES el"), formatPolicy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("establecimiento queda liberado de la"), formatPolicy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("obligación de custodia."), formatPolicy);
        row++;
        // Clause 3: accessories - disclaimer applies when noted on this receipt
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("- No se responde de botones, adornos ni"), formatPolicy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("accesorios salvo anotación en este recibo."), formatPolicy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("Se recomienda retirarlos antes de entregar."), formatPolicy);
        row++;
        // Clause 4: pre-cleaning advisory obligation (Art. 6 RD 1453/1987)
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("- Si el estado de la prenda implica riesgo de"), formatPolicy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("daño o resultado incierto, se informará al"), formatPolicy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("cliente antes de proceder al tratamiento."), formatPolicy);
        row++;
        // RGPD first-layer notice (LOPDGDD Art. 11 / RGPD Art. 13)
        QString rgpdContact = settings->businessPhone().isEmpty()
                              ? QString("ver cartel en tienda")
                              : settings->businessPhone();
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("- Sus datos son tratados por "), formatPolicy);
        row++;
        excel.write(row, 1, settings->businessName(), formatPolicy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("para gestionar el servicio (RGPD)."), formatPolicy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("Derechos arts.15-22: ") + rgpdContact + ".", formatPolicy);
        row++;
    }
    // Insert timestamp
    QXlsx::Format formatStamp;
    formatStamp.setFontSize(9);
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
    // Call the batch script to print the excel
    QProcess process;
    QString batchPath = AppSettings::instance()->printScriptPath();
    if (!QFile::exists(batchPath)) {
        qCritical() << "Imprimir::printTicket: print script not found at path" << batchPath;
        QMessageBox::critical(this, "Imprimir",
                              "No se puede encontrar el archivo batch para imprimir el ticket.",
                              QMessageBox::Ok, QMessageBox::Ok);
    } else {
        // Change the cursor to a loading icon
        QApplication::setOverrideCursor(Qt::WaitCursor);
        // Start printing process
        process.start(batchPath);
        process.waitForFinished();
        // Restore the cursor to default
        QApplication::restoreOverrideCursor();
    }
}

void Imprimir::on_bb_ok_cancel_accepted()
{
    if (le_n_ticket->text() == selectFromWhereLike(db, "n_recibo", "ingresos", "n_recibo", le_n_ticket->text(), true, true)) {
        getTicketInfo();
        if (isRecibo || !isRecibo && checkAnyItemPaid()) {
            createTicketExcel(true, false);
            printTicket();
            if (isRecibo) {
                int resp = QMessageBox::question(this, "Copia establecimiento",
                                                 "¿Desea copia para el establecimiento?",
                                                 QMessageBox::Yes | QMessageBox::No,
                                                 QMessageBox::Yes);
                if (resp == QMessageBox::Yes) {
                    createTicketExcel(false, false);
                    printTicket();
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
