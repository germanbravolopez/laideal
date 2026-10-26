#include "ticketrenderer.h"
#include "escposbuilder.h"
#include "verifactutypes.h"   // splitGross (header-only)

#include <QPainter>

namespace {
// Printed with the decimal comma whatever the language; this library has no sql_lite.
QString money(double v) { return QString::number(v, 'f', 2).replace(QLatin1Char('.'), QLatin1Char(',')); }
} // namespace

QByteArray TicketRenderer::render(const TicketData &d, int paperDots)
{
    EscPosBuilder b(paperDots);
    b.init();

    // --- Header: centered, bold; shop name printed double-size ---
    b.align(EscPosBuilder::Align::Center).bold(true);
    // A long line is wrapped here: the printer's own wrap is uncentered and may cut a word.
    const auto wrapped = [&b](const QString &text, int width) {
        for (const QString &row : EscPosBuilder::wrapText(text, width))
            b.line(row);
    };
    b.doubleSize(true);
    wrapped(d.businessName, b.columns() / 2);
    b.doubleSize(false);
    wrapped(d.legalName, b.columns());
    wrapped("NIF: " + d.nif, b.columns());
    wrapped(d.address, b.columns());
    wrapped(d.city, b.columns());
    wrapped("Tlf: " + d.phone, b.columns());
    b.bold(false);
    b.rule('=');

    // --- Document identity ---
    b.align(EscPosBuilder::Align::Left);
    b.line(d.isRecibo ? QStringLiteral("Recibo") : QStringLiteral("FACTURA SIMPLIFICADA"));
    const bool voided = !d.isRecibo && !d.voidedMarker.isEmpty();
    if (voided) {
        b.align(EscPosBuilder::Align::Center).bold(true);
        b.line("*** FACTURA " + d.voidedMarker + " ***");
        b.bold(false).align(EscPosBuilder::Align::Left);
    }
    b.align(EscPosBuilder::Align::Right).bold(true);
    b.line("Nº: " + d.invoiceId);
    b.bold(false).align(EscPosBuilder::Align::Left);
    // Art. 7 RD 1619/2012: the date of issue, the one the QR and AEAT carry.
    if (!d.isRecibo && !d.issueDate.isEmpty())
        b.line("Fecha de expedición: " + d.issueDate);
    b.line("Cliente: " + d.clientName);
    b.line("Recepción: " + d.receptionDate);
    if (d.isCompleteInvoice) {
        b.paragraph("Dirección: " + d.clientAddress);
        if (!d.clientDni.isEmpty())
            b.line("DNI: " + d.clientDni);
    }
    if (voided)
        b.paragraph("Factura " + d.voidedMarker.toLower()
                    + (d.voidedDate.isEmpty() ? QString() : " el " + d.voidedDate)
                    + ". Esta copia no es una factura en vigor.");
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
        // The split of the Verifactu record (splitGross): base in cents, IVA the rest.
        const GrossSplit split = splitGross(d.total, d.ivaRate);
        b.leftRight(QStringLiteral("Base Imponible:"), money(split.base));
        b.leftRight(QStringLiteral("IVA (%1%):").arg(QString::number(d.ivaRate, 'g', 6).replace('.', ',')),
                    money(split.tax));
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

    // --- Verifactu QR (facturas only; Imprimir redraws it through printableQr) ---
    if (!d.qr.isNull() && !voided) {
        // No extra feed: the payment marker above separates the QR, which has its own quiet zone.
        b.align(EscPosBuilder::Align::Center);
        b.rasterImage(d.qr);
        b.feed(1);
        if (d.verifactuVerifiable) {
            // Art. 20.1.b: a well-visible font, like the rest of the invoice (Font A),
            // split so neither paper width breaks it mid-word.
            b.line(QStringLiteral("Factura verificable en la sede"));
            b.line(QString::fromUtf8("electrónica de la AEAT"));
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

QImage TicketRenderer::printableQr(const QImage &qr, int paperDots)
{
    if (qr.isNull())
        return QImage();
    // Flattened onto white: a transparent pixel would otherwise read as black.
    QImage flat(qr.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    QPainter(&flat).drawImage(0, 0, qr);
    const QImage g = flat.convertToFormat(QImage::Format_Grayscale8);
    const auto dark = [&g](int x, int y) { return qGray(g.pixel(x, y)) < 128; };
    int left = g.width(), top = g.height(), right = -1, bottom = -1;
    for (int y = 0; y < g.height(); ++y)
        for (int x = 0; x < g.width(); ++x)
            if (dark(x, y)) {
                left = qMin(left, x); right = qMax(right, x);
                top = qMin(top, y); bottom = qMax(bottom, y);
            }
    if (right < 0)
        return QImage();
    // The top-left finder pattern starts with a 7-module dark row: one module's size.
    // Its longest run in the top band (a module is at most 1/21 of the symbol), so a
    // blurred corner of a scaled image does not shorten it.
    const double width = right - left + 1, height = bottom - top + 1;
    int run = 0;
    for (int y = top; y <= qMin(bottom, top + int(height / 21)); ++y) {
        int x = left;
        while (x <= qMin(right, left + 3) && !dark(x, y))
            ++x;
        int length = 0;
        while (x + length <= right && dark(x + length, y))
            ++length;
        run = qMax(run, length);
    }
    if (run < 7)
        return QImage();
    // A symbol has 21 + 4k modules: the size closest to the measured one (a blurred
    // edge widens the run a little), and only when it is close.
    const double measured = run / 7.0;
    int modules = 0;
    for (int n = 21; n <= 177; n += 4)
        if (modules == 0 || qAbs(width / n - measured) < qAbs(width / modules - measured))
            modules = n;
    if (qAbs(width / modules - measured) > 0.25 * measured || qAbs(width - height) > width / modules)
        return QImage();
    // 30-40 mm = 240-320 dots: the largest whole module size that fits.
    const int dots = qMax(1, qMin(320 / modules, paperDots / (modules + 8)));
    const int quiet = 4 * dots;
    QImage out((modules * dots) + 2 * quiet, (modules * dots) + 2 * quiet, QImage::Format_Grayscale8);
    out.fill(255);
    for (int r = 0; r < modules; ++r)
        for (int c = 0; c < modules; ++c) {
            const int x = left + int((c + 0.5) * width / modules);
            const int y = top + int((r + 0.5) * height / modules);
            if (!dark(qMin(x, right), qMin(y, bottom)))
                continue;
            for (int dy = 0; dy < dots; ++dy)
                for (int dx = 0; dx < dots; ++dx)
                    out.setPixel(quiet + c * dots + dx, quiet + r * dots + dy, qRgb(0, 0, 0));
        }
    return out;
}
