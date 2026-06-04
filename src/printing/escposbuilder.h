#ifndef ESCPOSBUILDER_H
#define ESCPOSBUILDER_H

#include <QByteArray>
#include <QString>

class QImage;

// Accumulates an Epson ESC/POS byte stream for the TM-T20III (and any ESC/POS-
// compatible thermal printer). Pure by design: no Qt event loop, no GUI, no
// I/O - it only appends to a QByteArray, so it is fully unit-testable by
// asserting the exact bytes produced.
//
// Text is transcoded to code page PC858 (CP850 + the euro sign) on the way in,
// because ESC/POS prints single-byte code-page bytes, not UTF-8. Selecting the
// page (ESC t 19) and transcoding the QString are two halves of the same step;
// init() does the former, text() the latter. See
// docs/modules/printer/03_escpos_command_reference.md.
class EscPosBuilder
{
public:
    enum class Align { Left = 0, Center = 1, Right = 2 };
    // Font A: 12x24 dots (48 cols @80mm / 35 @58mm). Font B: 9x17 (64 / 46).
    enum class Font  { A = 0, B = 1 };

    // paperDots = printable width in dots: 576 @80mm, 420 @58mm.
    explicit EscPosBuilder(int paperDots = 576);

    EscPosBuilder &init();                  // ESC @  +  ESC t 19 (PC858)
    EscPosBuilder &align(Align a);          // ESC a n
    EscPosBuilder &bold(bool on);           // ESC E n
    EscPosBuilder &font(Font f);            // ESC M n  (also updates columns())
    EscPosBuilder &doubleSize(bool on);     // GS ! 0x11 / 0x00
    EscPosBuilder &text(const QString &s);  // transcode to PC858, no line feed
    EscPosBuilder &line(const QString &s = QString());   // text + LF
    EscPosBuilder &feed(int n = 1);         // ESC d n  (n blank lines)
    EscPosBuilder &rule(char c = '-');      // a full-width row of c + LF

    // Word-wrap s to the current column width, one printed line per wrapped row.
    EscPosBuilder &paragraph(const QString &s);
    // Two fields filling one line: left-justified label + right-justified value,
    // space-padded between. Truncates the label if the pair does not fit.
    EscPosBuilder &leftRight(const QString &left, const QString &right);
    // Garment-table row: qty (qtyWidth, left) | name (remaining width, wraps) |
    // amount (amountWidth, right). Continuation lines (a wrapped name) carry
    // blank qty/amount columns.
    EscPosBuilder &garmentRow(const QString &qty, const QString &name,
                              const QString &amount, int qtyWidth = 4, int amountWidth = 9);

    EscPosBuilder &rasterImage(const QImage &img);   // GS v 0 (1bpp, MSB-first)
    EscPosBuilder &cut(int feedBefore = 3);          // ESC d n + GS V 66 0
    EscPosBuilder &raw(const QByteArray &bytes);     // escape hatch

    int columns() const { return m_cols; }           // cols at current font + width
    const QByteArray &bytes() const { return m_buf; }

    // Transcode a QString to PC858 bytes. Exposed static so the most error-prone
    // part of the rewrite is independently testable; chars with no PC858 glyph
    // become '?'.
    static QByteArray toCp858(const QString &s);

    // Greedy word-wrap helper (pure). Hard-splits a single word longer than width.
    static QStringList wrapText(const QString &s, int width);

private:
    void recalcCols();

    QByteArray m_buf;
    int  m_paperDots;
    Font m_font = Font::A;
    int  m_cols = 48;
};

#endif // ESCPOSBUILDER_H
