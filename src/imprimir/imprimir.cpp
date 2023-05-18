#include "imprimir.h"
#include "ui_imprimir.h"
#include "sql_lite.h"
#include "qprinterinfo.h"
#include "qprinter.h"

Imprimir::Imprimir(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Imprimir)
{
    ui->setupUi(this);
}

Imprimir::~Imprimir()
{
    delete ui;
}

void Imprimir::get_ticket_info()
{
    // Search ticket
    sql_query_model = new QSqlQueryModel;
    db.open();
    sql_query_model->setQuery("SELECT * \
                    FROM ingresos \
                    WHERE n_recibo = '" + ui->le_n_ticket->text() + "'");
    db.close();
}

bool Imprimir::check_ticket_paid()
{
    for (int row = 0; row < sql_query_model->rowCount(); row++)
    {
        if (sql_query_model->data(sql_query_model->index(row, TABLE_IS_PAYED)).toString() == "NO")
        {
            return false;
        }
    }
    return true;
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

void Imprimir::create_ticket_and_print(bool copy_for_client)
{
    // Create string with garments for ticket format
    QString ticket_garments;
    float ticket_total_f = 0.0;
    for (int row = 0; row < sql_query_model->rowCount(); row++)
    {
        QString garment_name;
        QString left_side = sql_query_model->data(sql_query_model->index(row, TABLE_GARMENT)).toString().left(8);
        if (left_side == "Alfombra")
        {
            garment_name = "Alfombra - ";
            garment_name.append(sql_query_model->data(sql_query_model->index(row, TABLE_OBSERV)).toString());
        }
        else
        {
            garment_name = sql_query_model->data(sql_query_model->index(row, TABLE_GARMENT)).toString();
        }
        ticket_garments.append("<tr>"
                "<td style='padding:0px; color:black; font-size:15px; font-weight:400; font-style:normal; font-family:Calibri,sans-serif; text-align:left; border:none; height:20px; width:33px;'>"
                    "&nbsp;&nbsp;&nbsp;"+ sql_query_model->data(sql_query_model->index(row, TABLE_QUANTITY)).toString() + "</td>"
                "<td style='padding:0px; color:black; font-size:15px; font-weight:400; font-style:normal; font-family:Calibri,sans-serif; text-align:left; border:none; width:137px;'>"
                    + garment_name + "</td>"
                "<td style='padding:0px; color:black; font-size:15px; font-weight:400; font-style: normal; font-family:Calibri,sans-serif; text-align:right; border:none; width:66px;'>"
                    + QString::number(sql_query_model->data(sql_query_model->index(row, TABLE_PRICE)).toFloat(), 'f', 2) + "&emsp;</td>"
            "</tr>");
        ticket_total_f = ticket_total_f + sql_query_model->data(sql_query_model->index(row, TABLE_PRICE)).toFloat();
    }
    // Calculate total price and IVA
    QString ticket_total = QString::number(ticket_total_f, 'f', 2);
    QString iva = QString::number(ticket_total_f * 0.21, 'f', 2);
    QString base_imponible = QString::number(ticket_total_f - (ticket_total_f * 0.21), 'f', 2);
    /////////////////////////////
    // Create printer for tickets
    QPrinter printer;
    printer.setPrinterName("EPSON TM-T20III");
    printer.setOutputFormat(QPrinter::NativeFormat);
    printer.setColorMode(QPrinter::GrayScale);
    // Create ticket content based on ticket information: payment date and invoice type
    QString ticket_type, ticket_dates, client_address, client_id, receipt_info;
    ticket_dates.append(
        "<tr>"
            "<td colspan='3'>Recepci&oacute;n: "
            + sql_query_model->data(sql_query_model->index(0 , TABLE_DATE_RCP)).toString() + "</td>"
        "</tr>"
    );
    if (is_recibo)
    {
        // Add copy for client or for store info
        if (copy_for_client)
        {
            receipt_info = "<tr><td colspan='3' style='font-size:12px;'>(Copia para el cliente)</td></tr>";
        }
        else
        {
            receipt_info = "<tr><td colspan='3' style='font-size:12px;'>(Copia para el establecimiento)</td></tr>";
        }
    }
    else
    {
        ticket_type = "<tr><td colspan='3'>FACTURA SIMPLIFICADA</td></tr>";
        ticket_dates.append(
            "<tr>"
                "<td colspan='3'>Pago: "
                + sql_query_model->data(sql_query_model->index(0 , TABLE_DATE_PAY)).toString() + "</td>"
            "</tr>"
        );
        // Extra data: address and id
        if (is_complete_invoice)
        {
            client_address = search_item_from_client(db, "direccion", sql_query_model->data(sql_query_model->index(0 , TABLE_CLIENT)).toString());
            if (client_address == "")
            {
                client_address = add_extra_info_to_invoice("Añadir dirección de facturación", "Dirección:");
            }
            client_address = "<tr><td colspan='3'>Dirección: "
                             + client_address + "</td></tr>";
            client_id = "<tr><td colspan='3'>DNI: "
                             + add_extra_info_to_invoice("Añadir DNI", "DNI:") + "</td></tr>";
        }
    }
    // Create full HTML ticket
    QTextDocument ticketContent;
    QString text;
    text.append(
            "<!DOCTYPE html>"
            "<html>"
                "<head>"
                    "<style>"
                        "table, th, td { border-collapse:collapse; border:none; font-size:15px; font-weight:400; font-style:normal; font-family:Calibri,sans-serif; text-align:left; }"
                    "</style>"
                "</head>"
            "<body>"
                "<table style='width:236px;'>"
                    "<tbody>"
                        "<tr>"
                            "<td colspan='3' style='font-size:25px; font-weight:700; text-align:center;'>Tintorer&iacute;a La Ideal"
                                "<div style='text-align:center;'>"
                                    "<span style='font-weight:700;'>Roc&iacute;o L&oacute;pez Dom&iacute;nguez</span><br>"
                                    "<span style='font-weight:700;'>NIF: 24215141-M</span><br>"
                                    "<span style='font-weight:700;'>Pl. San Pantale&oacute;n 1, 18012 Granada</span><br>"
                                    "<span style='font-weight:700;'>Tlf: 958 27 27 83</span>"
                                "</div>"
                            "</td>"
                        "</tr>"
                        "<tr><td colspan='3'><hr></td></tr>"
                        + ticket_type +
                        "<tr>"
                            "<td colspan='3' style='font-weight:700; text-align:right; height:26px;'>Recibo: "
                            + ui->le_n_ticket->text() + "</td>"
                        "</tr>"
                        "<tr>"
                            "<td colspan='3'>Cliente: "
                            + sql_query_model->data(sql_query_model->index(0 , TABLE_CLIENT)).toString() + "</td>"
                        "</tr>"
                        + ticket_dates + client_address + client_id +
                        "<tr></tr>"
                        "<tr>"
                            "<td style='font-weight:700; text-align:left; vertical-align:bottom; border-bottom:1px solid black; height:26px; width:33px;'>Uds.</td>"
                            "<td style='font-weight:700; text-align:left; vertical-align:bottom; border-bottom:1px solid black; height:26px; width:137px;'>Prendas</td>"
                            "<td style='font-weight:700; text-align:right; vertical-align:bottom; border-bottom:1px solid black; height:26px; width:66px;'>Importe</td>"
                        "</tr>"
                        + ticket_garments +
                        "<tr>"
                            "<td colspan='2' style='vertical-align:bottom; border-top:1px solid black; height:26px;'>Base Imponible:</td>"
                            "<td style='text-align:right; vertical-align:bottom; border-top:1px solid black;'>"
                            + base_imponible + "&emsp;</td>"
                        "</tr>"
                        "<tr>"
                            "<td colspan='2' style='vertical-align:bottom;'>IVA (21%):</td>"
                            "<td style='text-align:right; vertical-align:bottom;'>"
                            + iva + "&emsp;</td>"
                        "</tr>"
                        "<tr>"
                            "<td colspan='2' style='font-weight:700; vertical-align:bottom;'>TOTAL:</td>"
                            "<td style='font-weight:700; text-align:right; vertical-align:bottom;'>"
                            + ticket_total + "&emsp;</td>"
                        "</tr>"
                        "<tr><td colspan='3'><hr></td></tr>"
                        "<tr></tr>"
                        "<tr>"
                            "<td colspan='3' style='font-size:13px; vertical-align:bottom;'>CONDICIONES GENERALES.</td>"
                        "</tr>"
                        "<tr>"
                            "<td colspan='3' style='font-size:12px; vertical-align:bottom; height:46px;'>- El recibo deber&aacute; ser presentado al retirar la prenda. En caso de p&eacute;rdida, el usuario acreditar&aacute; su identidad.</td>"
                        "</tr>"
                        "<tr>"
                            "<td colspan='3' style='font-size:12px; vertical-align:bottom; height:46px;'>- La obligaci&oacute;n de conservar las prendas por el establecimiento caduca una vez transcurridos SEIS MESES desde la fecha de recogida.</td>"
                        "</tr>"
                        "<tr>"
                            "<td colspan='3' style='font-size:12px; vertical-align:bottom; height:46px;'>- No se responde de botones y otros adornos delicados de las prendas. Se recomienda que sean desmontados por el cliente.&nbsp;</td>"
                        "</tr>"
                        "<tr><td colspan='3'><hr></td></tr>"
                        "<tr>"
                            "<td colspan='3' style='font-size:12px; vertical-align:bottom;'>"
                            + QDateTime::currentDateTime().toString("dd/MM/yyyy - hh:mm:ss") + "</td>"
                        "</tr>"
                        + receipt_info +
                    "</tbody>"
                "</table>"
            "</body>"
            "</html>");
    ticketContent.setHtml(text);
    //ticketContent.print(&printer);
    // try this:
    QString plainText = ticketContent.toPlainText();
    QTextCursor cursor(&ticketContent);
    cursor.insertText(plainText);
    ticketContent.print(&printer);
    // else try this
    //QWebEngineView view;
    //view.setHtml(text);
    //view.print(&printer);
}

void Imprimir::on_bb_ok_cancel_accepted()
{
    if (ui->le_n_ticket->text() ==
        select_from_where_like(db, "n_recibo", "ingresos", "n_recibo", ui->le_n_ticket->text(), true))
    {
        get_ticket_info();
        if (is_recibo || (!is_recibo && check_ticket_paid()))
        {
            create_ticket_and_print(true);
            if (is_recibo)
            {
                //// Comment-in when no more automatic receipt are needed
                //int resp = QMessageBox::question(this, "Copia establecimiento",
                //                                 "¿Desea copia para el establecimiento?",
                //                                 QMessageBox::Yes | QMessageBox::No,
                //                                 QMessageBox::Yes);
                //if (resp == QMessageBox::Yes)
                //    create_ticket_and_print(false);
            }
        }
        else
        {
            QMessageBox::information(this, "Imprimir",
                                  "El número de recibo no se ha pagado por completo.",
                                  QMessageBox::Ok,
                                  QMessageBox::Ok);
        }
    }
    else
    {
        QMessageBox::information(this, "Imprimir",
                              "El número de recibo no se ha encontrado en la base de datos.\n"
                              "Utilizar otro número o buscarlo en la lista de ingresos.",
                              QMessageBox::Ok,
                              QMessageBox::Ok);
    }
}

void Imprimir::on_bb_ok_cancel_rejected()
{
    this->close();
}
