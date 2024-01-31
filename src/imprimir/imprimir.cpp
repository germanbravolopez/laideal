#include "imprimir.h"
#include "sql_lite.h"

#include "xlsxdocument.h"
#include "xlsxcellrange.h"
using namespace QXlsx;

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

void Imprimir::get_ticket_info()
{
    // Search ticket
    sql_query_model = new QSqlQueryModel;
    db.open();
    sql_query_model->setQuery("SELECT * FROM ingresos WHERE n_recibo = '" + le_n_ticket->text() + "'");
    db.close();
}

bool Imprimir::check_ticket_paid(int row)
{
    if (sql_query_model->data(sql_query_model->index(row, TABLE_IS_PAYED)).toString() == "NO")
        return false;
    return true;
}

bool Imprimir::check_any_item_paid()
{
    for (int row = 0; row < sql_query_model->rowCount(); row++) {
        if (check_ticket_paid(row))
            return true;
    }
    return false;
}

QString Imprimir::add_extra_info_to_invoice(QString title, QString request)
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

void Imprimir::create_ticket_excel(bool copy_for_client, bool add_payed_info)
{
    // Check that excel is accessible
    QString excel_path = "C:/Users/Usuario/work/tintoreria/NO_TOCAR_ticket_imprimir/ImprimirTicket.xlsx";
    if (!QFile::exists(excel_path)) {
        QMessageBox::critical(this, "Imprimir",
                              "No se puede encontrar el archivo excel para generar el ticket.",
                              QMessageBox::Ok, QMessageBox::Ok);
    } else {
        int row = 6;
        QXlsx::Document excel(excel_path);

        // First clear current content
        QXlsx::Format format_clear;
        format_clear.setBorderStyle(QXlsx::Format::BorderNone);
        format_clear.setFontSize(11);
        format_clear.setFontBold(false);
        format_clear.setTextWrap(false);
        format_clear.setVerticalAlignment(QXlsx::Format::AlignBottom);
        format_clear.setHorizontalAlignment(QXlsx::Format::AlignLeft);
        for (int row_cnt = row; row_cnt < 100; row_cnt++) {
            for (int col_cnt = 1; col_cnt < 4; col_cnt++) {
                excel.write(row_cnt, col_cnt, "", format_clear);
            }
            excel.unmergeCells("A" + QString::number(row_cnt) + ":B" + QString::number(row_cnt));
            excel.unmergeCells("A" + QString::number(row_cnt) + ":C" + QString::number(row_cnt));
        }

        // Start filling with current data
        if (is_recibo) {
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
        QXlsx::Format format_bold_right_align;
        format_bold_right_align.setFontBold(true);
        format_bold_right_align.setHorizontalAlignment(QXlsx::Format::AlignRight);
        excel.write(row, 1, QString("Nº: " + le_n_ticket->text()), format_bold_right_align);
        row++;
        // Client data
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("Cliente: " + sql_query_model->data(sql_query_model->index(0 , TABLE_CLIENT)).toString()));
        row++;
        // Ticket dates
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("Recepción: " + sql_query_model->data(sql_query_model->index(0 , TABLE_DATE_RCP)).toString().replace("-","/")));
        row++;
        // Extra information in case of complete invoice: address and DNI
        if (is_complete_invoice) {
            QString client_address;
            client_address = search_item_from_client(db, "direccion", sql_query_model->data(sql_query_model->index(0 , TABLE_CLIENT)).toString(), false);
            if (client_address == "")
                client_address = add_extra_info_to_invoice("Añadir dirección de facturación", "Dirección:");
            // Cut address string in case it is too long
            if (client_address.size() >= 42) {
                excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
                excel.write(row, 1, QString("Dirección: " + client_address.left(41)));
                row++;
                excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
                excel.write(row, 1, QString("Dirección: " + client_address.mid(41)));
                row++;
            } else {
                excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
                excel.write(row, 1, QString("Dirección: " + client_address));
                row++;
            }
            excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
            excel.write(row, 1, QString("DNI: " + add_extra_info_to_invoice("Añadir DNI", "DNI:")));
            row++;
        }
        // Add double line empty row
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        QXlsx::Format format_double_line;
        format_double_line.setBottomBorderStyle(QXlsx::Format::BorderDouble);
        excel.write(row, 1, "", format_double_line);
        excel.write(row, 2, "", format_double_line);
        excel.write(row, 3, "", format_double_line);
        row++;
        // Add header for garments table
        QXlsx::Format format_single_line;
        format_single_line.setBottomBorderStyle(QXlsx::Format::BorderThin);
        excel.write(row, 1, "Uds.", format_single_line);
        excel.write(row, 2, "Prendas", format_single_line);
        excel.write(row, 3, "Importe", format_single_line);
        row++;
        // Configure format for each column of garments table
        QXlsx::Format format_uds_cost, format_garm;
        format_uds_cost.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
        format_uds_cost.setVerticalAlignment(QXlsx::Format::AlignTop);
        format_garm.setTextWrap(true);
        // Add garments
        float ticket_total_f = 0.0;
        for (int row_cnt = 0; row_cnt < sql_query_model->rowCount(); row_cnt++) {
            if (is_recibo || !is_recibo && check_ticket_paid(row_cnt)) {
                QString garment_name = sql_query_model->data(sql_query_model->index(row_cnt, TABLE_GARMENT)).toString();
                // Complete garment with size info
                QString size = QString::number(sql_query_model->data(sql_query_model->index(row_cnt, TABLE_SIZE)).toFloat(), 'f', 2);
                if (size != "" && size != "0.00") {
                    garment_name.append(" - " + size);
                }
                //// Complete "Alfombra" getting also the observation value (number)
                //QString left_side = garment_name.left(8);
                //QString obsv = sql_query_model->data(sql_query_model->index(row_cnt, TABLE_OBSERV)).toString();
                //if (left_side == "Alfombra" && obsv != "") {
                //    garment_name.append(" - " + obsv);
                //}
                // Content of each garment in the ticket
                excel.write(row, 1, sql_query_model->data(sql_query_model->index(row_cnt, TABLE_QUANTITY)).toString(), format_uds_cost);
                excel.write(row, 2, garment_name, format_garm);
                excel.write(row, 3, QString::number(sql_query_model->data(sql_query_model->index(row_cnt, TABLE_PRICE)).toFloat(), 'f', 2), format_uds_cost);
                row++;
                //// Add extra information for each garment for facturas simplificadas
                //if (!is_recibo && sql_query_model->data(sql_query_model->index(row_cnt, TABLE_IS_PAYED)).toString() == "SI") {
                //    format_garm.setFontSize(10);
                //    excel.write(row, 2, "Pagad.: " + sql_query_model->data(sql_query_model->index(row_cnt, TABLE_DATE_PAY)).toString().replace("-","/"), format_garm);
                //    format_garm.setFontSize(11);
                //    row++;
                //    if (sql_query_model->data(sql_query_model->index(row_cnt, TABLE_STATE)).toString() == "Recogido") {
                //        format_garm.setFontSize(10);
                //        excel.write(row, 2, "Recog.: " + sql_query_model->data(sql_query_model->index(row_cnt, TABLE_DATE_PKU)).toString().replace("-","/"), format_garm);
                //        format_garm.setFontSize(11);
                //        row++;
                //    }
                //}
                ticket_total_f = ticket_total_f + sql_query_model->data(sql_query_model->index(row_cnt, TABLE_PRICE)).toFloat();
            }
        }
        // Calculate total price and IVA
        QString ticket_total = QString::number(ticket_total_f, 'f', 2);
        QString base_imponible = QString::number(ticket_total_f / 1.21, 'f', 2);
        QString iva = QString::number(ticket_total_f - base_imponible.toFloat(), 'f', 2);
        // Add totals
        QXlsx::Format format_totals;
        format_totals.setFontBold(true);
        format_totals.setTopBorderStyle(QXlsx::Format::BorderThin);
        format_totals.setBottomBorderStyle(QXlsx::Format::BorderDouble);
        format_totals.setHorizontalAlignment(QXlsx::Format::AlignRight);
        if (is_recibo) {
            // Write total text
            excel.mergeCells("A" + QString::number(row) + ":B" + QString::number(row));
            excel.write(row, 1, QString("TOTAL (IVA incl.):"), format_totals);
            excel.write(row, 2, "", format_totals);
            // Write total cost
            format_totals.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
            excel.write(row, 3, ticket_total, format_totals);
            row++;
        } else {
            // Write base text
            format_totals.setHorizontalAlignment(QXlsx::Format::AlignRight);
            format_totals.setBottomBorderStyle(QXlsx::Format::BorderNone);
            excel.mergeCells("A" + QString::number(row) + ":B" + QString::number(row));
            excel.write(row, 1, QString("Base Imponible:"), format_totals);
            excel.write(row, 2, "", format_totals);
            // Write base cost
            format_totals.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
            excel.write(row, 3, base_imponible, format_totals);
            row++;
            // Write iva text
            format_totals.setHorizontalAlignment(QXlsx::Format::AlignRight);
            format_totals.setTopBorderStyle(QXlsx::Format::BorderNone);
            excel.mergeCells("A" + QString::number(row) + ":B" + QString::number(row));
            excel.write(row, 1, QString("IVA (21%):"), format_totals);
            // Write iva cost
            format_totals.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
            excel.write(row, 3, iva, format_totals);
            row++;
            // Write total text
            format_totals.setHorizontalAlignment(QXlsx::Format::AlignRight);
            format_totals.setBottomBorderStyle(QXlsx::Format::BorderDouble);
            excel.mergeCells("A" + QString::number(row) + ":B" + QString::number(row));
            excel.write(row, 1, QString("TOTAL:"), format_totals);
            excel.write(row, 2, "", format_totals);
            // Write total cost
            format_totals.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
            excel.write(row, 3, ticket_total, format_totals);
            row++;
        }
        // Insert receipt information
        if (add_payed_info) {
            QXlsx::Format format_payed;
            format_payed.setHorizontalAlignment(QXlsx::Format::AlignRight);
            excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
            excel.write(row, 1, QString("IMPORTE PAGADO"), format_payed);
            row++;
        } else
            row++; // Add empty row
        if (is_recibo && copy_for_client) {
            excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
            excel.write(row, 1, QString("(Copia para el cliente)"));
            row++;
            row++; // Add empty row
        } else if (is_recibo && !copy_for_client) {
            excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
            excel.write(row, 1, QString("(Copia para el establecimiento)"));
            row++;
            row++; // Add empty row
        }
        // Insert policy
        QXlsx::Format format_policy;
        format_policy.setFontSize(10);
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("CONDICIONES GENERALES."), format_policy);
        row++;
        format_policy.setFontSize(9);
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("- El recibo deberá ser presentado al retirar la"), format_policy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("prenda. En caso de pérdida, el usuario acreditará"), format_policy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("su identidad."), format_policy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("- La obligación de conservar las prendas por el"), format_policy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("establecimiento caduca una vez transcurridos"), format_policy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("SEIS MESES desde la fecha de recepción."), format_policy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("- No se responde de botones y otros adornos "), format_policy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("delicados de las prendas. Se recomienda que"), format_policy);
        row++;
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QString("que sean desmontados por el cliente."), format_policy);
        row++;
        // Insert timestamp
        QXlsx::Format format_stamp;
        format_stamp.setFontSize(9);
        format_stamp.setTopBorderStyle(QXlsx::Format::BorderThin);
        excel.mergeCells("A" + QString::number(row) + ":C" + QString::number(row));
        excel.write(row, 1, QDateTime::currentDateTime().toString("dd/MM/yyyy - hh:mm:ss"), format_stamp);
        excel.write(row, 2, "", format_stamp);
        excel.write(row, 3, "", format_stamp);
        // Save file
        excel.save();
    }
}

void Imprimir::print_ticket()
{
    // Call the batch script to print the excel
    QProcess process;
    QString batch_path = "C:/Users/Usuario/work/tintoreria/NO_TOCAR_ticket_imprimir/print_ticket.bat";
    if (!QFile::exists(batch_path)) {
        QMessageBox::critical(this, "Imprimir",
                              "No se puede encontrar el archivo batch para imprimir el ticket.",
                              QMessageBox::Ok, QMessageBox::Ok);
    } else {
        // Change the cursor to a loading icon
        QApplication::setOverrideCursor(Qt::WaitCursor);
        // Start printing process
        process.start(batch_path);
        process.waitForFinished();
        // Restore the cursor to default
        QApplication::restoreOverrideCursor();
    }
}

void Imprimir::on_bb_ok_cancel_accepted()
{
    if (le_n_ticket->text() == select_from_where_like(db, "n_recibo", "ingresos", "n_recibo", le_n_ticket->text(), true, true)) {
        get_ticket_info();
        if (is_recibo || !is_recibo && check_any_item_paid()) {
            create_ticket_excel(true, false);
            print_ticket();
            if (is_recibo) {
                int resp = QMessageBox::question(this, "Copia establecimiento",
                                                 "¿Desea copia para el establecimiento?",
                                                 QMessageBox::Yes | QMessageBox::No,
                                                 QMessageBox::Yes);
                if (resp == QMessageBox::Yes) {
                    create_ticket_excel(false, false);
                    print_ticket();
                }
            }
        } else if (!is_recibo)
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
