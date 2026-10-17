// Decodes a file of raw ESC/POS bytes (what the UI smoke test's FILE-port printer
// queue captured from laideal.exe) with the same reader as test_ticket_preview:
//
//   escpos_preview <bytes.prn> <out prefix>     ->  <prefix>.txt (ASCII mock) + <prefix>.png
//
// In the .txt a raster image (the QR) shows as "[QR <w>x<h>]" and a cut as a dashed line.

#include <QFile>
#include <QGuiApplication>

#include "escpospreview.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() != 3) {
        qCritical("usage: escpos_preview <bytes.prn> <out prefix>");
        return 2;
    }
    QFile in(args.at(1));
    if (!in.open(QIODevice::ReadOnly)) {
        qCritical("escpos_preview: cannot read %s", qPrintable(args.at(1)));
        return 1;
    }
    const QVector<EscPosPreview::PreviewBlock> blocks = EscPosPreview::interpret(in.readAll());
    const int paperDots = 576;

    QFile txt(args.at(2) + ".txt");
    if (!txt.open(QIODevice::WriteOnly) || txt.write(EscPosPreview::toAscii(blocks, paperDots).toUtf8()) < 0)
        return 1;
    if (!EscPosPreview::toImage(blocks, paperDots).save(args.at(2) + ".png"))
        return 1;
    return 0;
}
