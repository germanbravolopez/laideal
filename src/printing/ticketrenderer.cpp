#include "ticketrenderer.h"
#include "escposbuilder.h"

namespace {
QString money(double v) { return QString::number(v, 'f', 2); }
} // namespace

QByteArray TicketRenderer::render(const TicketData &d, int paperDots)
{
    EscPosBuilder b(paperDots);
    b.init();

    // --- Header: centered, bold; shop name printed double-size ---
    b.align(EscPosBuilder::Align::Center).bold(true);
    b.doubleSize(true).line(d.businessName).doubleSize(false);
    b.line(d.legalName);
    b.line("NIF: " + d.nif);
    b.line(d.address);
    b.line(d.city);
    b.line("Tlf: " + d.phone);
    b.bold(false);
    b.rule('=');

    // --- Document identity ---
    b.align(EscPosBuilder::Align::Left);
    b.line(d.isRecibo ? QStringLiteral("Recibo") : QStringLiteral("FACTURA SIMPLIFICADA"));
    b.align(EscPosBuilder::Align::Right).bold(true);
    b.line("Nº: " + d.invoiceId);
    b.bold(false).align(EscPosBuilder::Align::Left);
    b.line("Cliente: " + d.clientName);
    b.line("Recepción: " + d.receptionDate);
    if (d.isCompleteInvoice) {
        b.paragraph("Dirección: " + d.clientAddress);
        b.line("DNI: " + d.clientDni);
    }
    b.rule('=');

    // --- Garment table ---
    b.garmentRow(QStringLiteral("Uds."), QStringLiteral("Prendas"), QStringLiteral("Importe"));
    b.rule('-');
    for (const TicketGarmentLine &g : d.garments)
        b.garmentRow(g.quantity, g.name, g.amount);
    b.rule('-');

    // --- Totals ---
    b.bold(true);
    if (d.isRecibo) {
        b.leftRight(QStringLiteral("TOTAL (IVA incl.):"), money(d.total));
    } else {
        // Reproduce the legacy rounding: IVA is derived from the rounded base
        // string, not the raw base, so the printed base + IVA add back to total.
        const double base = d.total / (1.0 + d.ivaRate / 100.0);
        const QString baseStr = money(base);
        b.leftRight(QStringLiteral("Base Imponible:"), baseStr);
        b.leftRight(QStringLiteral("IVA (%1%):").arg(d.ivaRate, 0, 'f', 0),
                    money(d.total - baseStr.toDouble()));
        b.leftRight(QStringLiteral("TOTAL:"), money(d.total));
    }
    b.bold(false);
    b.rule('=');

    // --- Payment marker ---
    if (d.addPayedInfo) {
        b.align(EscPosBuilder::Align::Right).line(QStringLiteral("IMPORTE PAGADO"));
        b.align(EscPosBuilder::Align::Left);
    } else {
        b.feed(1);
    }

    // --- Copy marker (recibos only) ---
    if (d.isRecibo) {
        b.line(d.copyForClient ? QStringLiteral("(Copia para el cliente)")
                               : QStringLiteral("(Copia para el establecimiento)"));
    }

    // --- Verifactu QR (facturas only; raster the AEAT-issued pixmap verbatim) ---
    if (!d.qr.isNull()) {
        // No extra feed here: the payment marker above already separates the QR,
        // and the AEAT pixmap carries its own quiet-zone margin - an added blank
        // line left too much empty space before the code.
        b.align(EscPosBuilder::Align::Center);
        b.rasterImage(d.qr);
        b.feed(1);
        if (d.verifactuVerifiable) {
            b.font(EscPosBuilder::Font::B);
            // Plain literal (implicit QString::fromUtf8) for accented text, matching
            // the codebase convention - QStringLiteral with accents is charset-fragile.
            b.line(QString::fromUtf8("Factura verificable en la sede electrónica de la AEAT"));
            b.font(EscPosBuilder::Font::A);
        }
        b.align(EscPosBuilder::Align::Left);
    }

    // --- Legal policy (client copy only) - RD 1453/1987 + RGPD first layer ---
    if (d.copyForClient) {
        // Font B is already the smallest built-in font; tighten the line pitch
        // too so the legal block reads as compact fine print, not body text.
        b.feed(1).font(EscPosBuilder::Font::B).lineSpacing(24);
        // Plain literals (implicit QString::fromUtf8) for the accented legal text.
        b.line("CONDICIONES GENERALES (RD 1453/1987)");
        b.paragraph(QString::fromUtf8(
            "- El recibo deberá presentarse al retirar la prenda. Su pérdida no impide "
            "la recogida, pero el cliente acreditará su identidad."));
        b.paragraph(QString::fromUtf8(
            "- Las prendas no recogidas en 3 MESES desde la entrega podrán devengar gastos "
            "de custodia. Transcurridos 6 MESES el establecimiento queda liberado de la "
            "obligación de custodia."));
        b.paragraph(QString::fromUtf8(
            "- No se responde de botones, adornos, ni accesorios salvo anotación en este "
            "recibo. Se recomienda retirarlos antes de entregar."));
        b.paragraph(QString::fromUtf8(
            "- Si el estado de la prenda implica riesgo de daño o resultado incierto, se "
            "informará al cliente antes de proceder al tratamiento."));
        const QString rgpdContact = d.phone.isEmpty() ? QStringLiteral("ver cartel en tienda")
                                                       : d.phone;
        b.paragraph("- Sus datos son tratados por " + d.businessName +
                    " para gestionar el servicio. RGPD - Derechos arts.15-22: " +
                    rgpdContact + ".");
        b.defaultLineSpacing().font(EscPosBuilder::Font::A);
    }

    // --- Timestamp + cut ---
    b.rule('-');
    b.font(EscPosBuilder::Font::B).line(d.timestamp).font(EscPosBuilder::Font::A);
    b.cut();

    return b.bytes();
}
