#ifndef TICKETRENDERER_H
#define TICKETRENDERER_H

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>

// One garment line of a ticket: the three printed columns of the body table.
struct TicketGarmentLine {
    QString quantity;   // "Uds." value (cantidad)
    QString name;       // garment name, already with " - <size>" appended when relevant
    QString amount;     // importe, formatted to 2 decimals
};

// Everything the renderer needs to lay out one ticket copy. Plain data only - no
// DB handle, no widgets - so it can be assembled by Imprimir from the query
// model and fed to TicketRenderer in tests with synthetic rows.
struct TicketData {
    // Header (centered block)
    QString businessName;   // shop name, printed large
    QString legalName;      // verifactuName (company legal name)
    QString nif;
    QString address;
    QString city;
    QString phone;

    // Document identity
    bool    isRecibo = true;            // false => FACTURA SIMPLIFICADA
    bool    isCompleteInvoice = false;  // adds Direccion / DNI lines
    QString invoiceId;                  // displayInvoiceId()
    QString clientName;
    QString receptionDate;              // already normalised ('-' -> '/')
    QString clientAddress;              // complete-invoice only
    QString clientDni;                  // complete-invoice only

    // Body
    QList<TicketGarmentLine> garments;
    double  total = 0.0;                // gross total (IVA included)
    double  ivaRate = 21.0;             // percentage

    // Footer
    bool    addPayedInfo = false;       // print "IMPORTE PAGADO"
    bool    copyForClient = true;       // recibo copy marker + legal policy
    QImage  qr;                         // null => no QR (recibos, or no CSV)
    bool    verifactuVerifiable = false;// print the AEAT verification legend
    QString timestamp;                  // "dd/MM/yyyy - hh:mm:ss" (caller-supplied)
};

// Maps a TicketData onto an EscPosBuilder, reproducing the historical Excel
// receipt/invoice layout (docs/modules/printer/04_current_printing_flow.md)
// within the printer's column width. Pure: same input -> same bytes.
class TicketRenderer
{
public:
    // paperDots = printable width in dots (576 @80mm, 420 @58mm).
    static QByteArray render(const TicketData &d, int paperDots = 576);
};

#endif // TICKETRENDERER_H
