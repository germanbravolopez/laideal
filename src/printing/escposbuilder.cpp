#include "escposbuilder.h"

#include <QHash>
#include <QImage>
#include <QStringList>

namespace {
// ESC/POS marker bytes.
const char ESC = 0x1B;
const char GS  = 0x1D;
const char LF  = 0x0A;

// Unicode -> PC858 (code page 858 = CP850 + euro) for the glyphs a Spanish
// laundry ticket actually prints. ASCII (<= 0x7F) passes through unchanged; any
// non-ASCII char absent from this map is emitted as '?'. PC858 is selected on
// the device with `ESC t 19` in init().
const QHash<ushort, uchar> &cp858Map()
{
    static const QHash<ushort, uchar> m = {
        {0x00C7, 0x80}, {0x00FC, 0x81}, {0x00E9, 0x82}, {0x00E2, 0x83},
        {0x00E4, 0x84}, {0x00E0, 0x85}, {0x00E7, 0x87}, {0x00EA, 0x88},
        {0x00EB, 0x89}, {0x00E8, 0x8A}, {0x00EF, 0x8B}, {0x00EE, 0x8C},
        {0x00EC, 0x8D}, {0x00C4, 0x8E}, {0x00C9, 0x90}, {0x00F4, 0x93},
        {0x00F6, 0x94}, {0x00F2, 0x95}, {0x00FB, 0x96}, {0x00F9, 0x97},
        {0x00D6, 0x99}, {0x00DC, 0x9A}, {0x00A3, 0x9C}, {0x00E1, 0xA0},
        {0x00ED, 0xA1}, {0x00F3, 0xA2}, {0x00FA, 0xA3}, {0x00F1, 0xA4},
        {0x00D1, 0xA5}, {0x00AA, 0xA6}, {0x00BA, 0xA7}, {0x00BF, 0xA8},
        {0x00AC, 0xAA}, {0x00BD, 0xAB}, {0x00BC, 0xAC}, {0x00A1, 0xAD},
        {0x00AB, 0xAE}, {0x00BB, 0xAF}, {0x00C1, 0xB5}, {0x00C2, 0xB6},
        {0x00C0, 0xB7}, {0x00A9, 0xB8}, {0x00E3, 0xC6}, {0x00C3, 0xC7},
        {0x00CA, 0xD2}, {0x00CB, 0xD3}, {0x00C8, 0xD4}, {0x20AC, 0xD5}, // euro
        {0x00CD, 0xD6}, {0x00CE, 0xD7}, {0x00CF, 0xD8}, {0x00CC, 0xDD},
        {0x00D3, 0xE0}, {0x00D4, 0xE2}, {0x00D2, 0xE3}, {0x00D5, 0xE5},
        {0x00DA, 0xE9}, {0x00DB, 0xEA}, {0x00D9, 0xEB}, {0x00DD, 0xED},
        {0x00B4, 0xEF}, {0x00B0, 0xF8}, {0x00B7, 0xFA},
    };
    return m;
}
} // namespace

EscPosBuilder::EscPosBuilder(int paperDots)
    : m_paperDots(paperDots)
{
    recalcCols();
}

void EscPosBuilder::recalcCols()
{
    // Font A is 12 dots wide, Font B 9 dots; columns = printable dots / char width.
    m_cols = m_paperDots / (m_font == Font::A ? 12 : 9);
    if (m_cols < 1) m_cols = 1;
}

QByteArray EscPosBuilder::toCp858(const QString &s)
{
    QByteArray out;
    out.reserve(s.size());
    const QHash<ushort, uchar> &map = cp858Map();
    for (const QChar ch : s) {
        const ushort u = ch.unicode();
        if (u <= 0x7F) {
            out.append(static_cast<char>(u));
        } else {
            const auto it = map.constFind(u);
            out.append(static_cast<char>(it != map.constEnd() ? it.value() : uchar('?')));
        }
    }
    return out;
}

QStringList EscPosBuilder::wrapText(const QString &s, int width)
{
    if (width < 1) width = 1;
    QStringList out;
    const QStringList words = s.split(' ', Qt::KeepEmptyParts);
    QString cur;
    auto flush = [&]() { out << cur; cur.clear(); };
    for (const QString &word : words) {
        QString w = word;
        // A single word longer than the line is hard-split across rows.
        while (w.size() > width) {
            if (!cur.isEmpty()) flush();
            out << w.left(width);
            w = w.mid(width);
        }
        if (cur.isEmpty())
            cur = w;
        else if (cur.size() + 1 + w.size() <= width)
            cur += ' ' + w;
        else { flush(); cur = w; }
    }
    flush();
    if (out.isEmpty()) out << QString();
    return out;
}

EscPosBuilder &EscPosBuilder::init()
{
    m_font = Font::A;
    recalcCols();
    static const char seq[] = { ESC, '@', ESC, 't', 19 };   // reset + PC858
    m_buf.append(seq, sizeof(seq));
    return *this;
}

EscPosBuilder &EscPosBuilder::align(Align a)
{
    const char seq[] = { ESC, 'a', static_cast<char>(a) };
    m_buf.append(seq, sizeof(seq));
    return *this;
}

EscPosBuilder &EscPosBuilder::bold(bool on)
{
    const char seq[] = { ESC, 'E', static_cast<char>(on ? 1 : 0) };
    m_buf.append(seq, sizeof(seq));
    return *this;
}

EscPosBuilder &EscPosBuilder::font(Font f)
{
    m_font = f;
    recalcCols();
    const char seq[] = { ESC, 'M', static_cast<char>(f) };
    m_buf.append(seq, sizeof(seq));
    return *this;
}

EscPosBuilder &EscPosBuilder::doubleSize(bool on)
{
    const char seq[] = { GS, '!', static_cast<char>(on ? 0x11 : 0x00) };
    m_buf.append(seq, sizeof(seq));
    return *this;
}

EscPosBuilder &EscPosBuilder::text(const QString &s)
{
    m_buf.append(toCp858(s));
    return *this;
}

EscPosBuilder &EscPosBuilder::line(const QString &s)
{
    text(s);
    m_buf.append(LF);
    return *this;
}

EscPosBuilder &EscPosBuilder::feed(int n)
{
    if (n <= 0) return *this;
    const char seq[] = { ESC, 'd', static_cast<char>(n) };
    m_buf.append(seq, sizeof(seq));
    return *this;
}

EscPosBuilder &EscPosBuilder::rule(char c)
{
    return line(QString(m_cols, QChar(c)));
}

EscPosBuilder &EscPosBuilder::paragraph(const QString &s)
{
    const QStringList rows = wrapText(s, m_cols);
    for (const QString &r : rows)
        line(r);
    return *this;
}

EscPosBuilder &EscPosBuilder::leftRight(const QString &left, const QString &right)
{
    const int width = m_cols;
    QString l = left;
    QString r = right;
    if (r.size() >= width) {
        r = r.right(width);
        l.clear();
    } else if (l.size() + 1 + r.size() > width) {
        l = l.left(width - r.size() - 1);   // keep at least one separating space
    }
    int pad = width - l.size() - r.size();
    if (pad < 0) pad = 0;
    return line(l + QString(pad, ' ') + r);
}

EscPosBuilder &EscPosBuilder::garmentRow(const QString &qty, const QString &name,
                                         const QString &amount, int qtyWidth, int amountWidth)
{
    int nameWidth = m_cols - qtyWidth - amountWidth;
    if (nameWidth < 1) nameWidth = 1;
    const QStringList nameRows = wrapText(name, nameWidth);
    for (int i = 0; i < nameRows.size(); ++i) {
        const QString q = (i == 0) ? qty.left(qtyWidth) : QString();
        const QString a = (i == 0) ? amount.right(amountWidth) : QString();
        QString qField = q.leftJustified(qtyWidth, ' ', true);
        QString nField = nameRows[i].leftJustified(nameWidth, ' ', true);
        QString aField = a.rightJustified(amountWidth, ' ', true);
        line(qField + nField + aField);
    }
    return *this;
}

EscPosBuilder &EscPosBuilder::rasterImage(const QImage &img)
{
    if (img.isNull())
        return *this;
    const QImage mono = img.convertToFormat(QImage::Format_Mono);
    const int w = mono.width();
    const int h = mono.height();
    const int widthBytes = (w + 7) / 8;

    // GS v 0 m xL xH yL yH  <data>   (m=0 normal)
    const char hdr[] = {
        GS, 'v', '0', 0,
        static_cast<char>(widthBytes & 0xFF), static_cast<char>((widthBytes >> 8) & 0xFF),
        static_cast<char>(h & 0xFF),          static_cast<char>((h >> 8) & 0xFF)
    };
    m_buf.append(hdr, sizeof(hdr));

    QByteArray data(widthBytes * h, '\0');
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // 1 = black dot. Threshold on luminance so the source can be mono or grey.
            if (qGray(mono.pixel(x, y)) < 128)
                data[y * widthBytes + (x >> 3)] |= static_cast<char>(0x80 >> (x & 7));
        }
    }
    m_buf.append(data);
    return *this;
}

EscPosBuilder &EscPosBuilder::cut(int feedBefore)
{
    feed(feedBefore);
    static const char seq[] = { GS, 'V', 66, 0 };   // feed (n=0) + partial cut
    m_buf.append(seq, sizeof(seq));
    return *this;
}

EscPosBuilder &EscPosBuilder::raw(const QByteArray &bytes)
{
    m_buf.append(bytes);
    return *this;
}
