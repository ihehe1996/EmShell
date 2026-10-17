#include "terminal/TerminalScreen.h"

#include <QStringList>
#include <QtGlobal>

#include <algorithm>

namespace Em {

// ----------------------------------------------------------- 字符宽度 ---

bool isWideChar(char32_t c)
{
    // 东亚全角/宽字符的主要区间。不求穷尽 Unicode 的 East Asian Width 表，
    // 覆盖实际会出现在终端里的就够。
    static const struct { char32_t lo, hi; } ranges[] = {
        { 0x1100,  0x115F  },  // 谚文字母
        { 0x2E80,  0x303E  },  // 部首扩展 / 康熙部首 / 中日韩符号
        { 0x3041,  0x33FF  },  // 平假名 / 片假名 / 注音 / 中日韩兼容
        { 0x3400,  0x4DBF  },  // 中日韩扩展 A
        { 0x4E00,  0x9FFF  },  // 中日韩统一表意文字
        { 0xA000,  0xA4CF  },  // 彝文
        { 0xAC00,  0xD7A3  },  // 谚文音节
        { 0xF900,  0xFAFF  },  // 中日韩兼容表意文字
        { 0xFE10,  0xFE19  },  // 竖排标点
        { 0xFE30,  0xFE6F  },  // 中日韩兼容形式
        { 0xFF00,  0xFF60  },  // 全角 ASCII
        { 0xFFE0,  0xFFE6  },  // 全角符号
        { 0x1F300, 0x1F64F  },  // emoji
        { 0x1F900, 0x1F9FF  },  // emoji 补充
        { 0x20000, 0x3FFFD  },  // 中日韩扩展 B 及以后
    };
    for (const auto &r : ranges) {
        if (c >= r.lo && c <= r.hi)
            return true;
    }
    return false;
}

bool isZeroWidthChar(char32_t c)
{
    if (c == 0x200B || c == 0x200C || c == 0x200D || c == 0xFEFF)
        return true;
    // 组合用变音符
    if (c >= 0x0300 && c <= 0x036F) return true;
    if (c >= 0x1AB0 && c <= 0x1AFF) return true;
    if (c >= 0x1DC0 && c <= 0x1DFF) return true;
    if (c >= 0x20D0 && c <= 0x20FF) return true;
    if (c >= 0xFE00 && c <= 0xFE0F) return true;  // 变体选择符
    return false;
}

// --------------------------------------------------------------- 构造 ---

TerminalScreen::TerminalScreen(int cols, int rows)
    : m_cols(qMax(1, cols))
    , m_rows(qMax(1, rows))
{
    reset();
}

void TerminalScreen::reset()
{
    m_screen.clear();
    m_screen.resize(m_rows);
    for (auto &line : m_screen)
        line.resize(m_cols);

    m_scrollback.clear();
    m_altScreen.clear();

    m_cursorX = 0;
    m_cursorY = 0;
    m_cursorVisible = true;
    m_wrapPending = false;

    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;

    resetAttributes();

    m_appCursorKeys = false;
    m_bracketedPaste = false;
    m_autoWrap = true;
    m_insertMode = false;
    m_usingAlt = false;
    m_viewOffset = 0;

    m_state = State::Ground;
    m_params.clear();
    m_curParam = -1;
    m_paramHasDigits = false;
    m_privateMarker = false;
    m_oscBuffer.clear();
    m_oscEscPending = false;
    m_utf8Pending.clear();

    m_dirty = true;
}

void TerminalScreen::resetAttributes()
{
    m_curFg = kColorDefault;
    m_curBg = kColorDefault;
    m_curFlags = 0;
}

// ----------------------------------------------------------- 行访问 ---

QVector<Cell> &TerminalScreen::screenLine(int y)
{
    static QVector<Cell> dummy;
    if (y < 0 || y >= m_screen.size())
        return dummy;
    return m_screen[y];
}

const QVector<Cell> &TerminalScreen::screenLine(int y) const
{
    static const QVector<Cell> dummy;
    if (y < 0 || y >= m_screen.size())
        return dummy;
    return m_screen[y];
}

const QVector<Cell> &TerminalScreen::lineAt(int index) const
{
    static const QVector<Cell> dummy;
    const int sb = m_scrollback.size();
    if (index < 0)
        return dummy;
    if (index < sb)
        return m_scrollback[index];
    const int y = index - sb;
    if (y >= m_screen.size())
        return dummy;
    return m_screen[y];
}

QString TerminalScreen::lineText(int index) const
{
    const QVector<Cell> &line = lineAt(index);
    QString out;
    out.reserve(line.size());
    for (const Cell &c : line) {
        if (c.ch == U'\0')          // 全角字符的右半格，跳过
            continue;
        out.append(QString::fromUcs4(&c.ch, 1));
    }
    while (out.endsWith(QLatin1Char(' ')))
        out.chop(1);
    return out;
}

QString TerminalScreen::currentLineText() const
{
    return lineText(scrollbackCount() + m_cursorY);
}

QString TerminalScreen::textInRange(int startLine, int startCol, int endLine, int endCol) const
{
    if (startLine > endLine) {
        std::swap(startLine, endLine);
        std::swap(startCol, endCol);
    }

    QStringList parts;
    for (int li = startLine; li <= endLine; ++li) {
        const QVector<Cell> &line = lineAt(li);
        if (line.isEmpty())
            continue;

        int from = 0;
        int to   = line.size() - 1;
        if (li == startLine) from = qBound(0, startCol, line.size());
        if (li == endLine)   to   = qBound(0, endCol, line.size() - 1);

        QString text;
        for (int x = from; x <= to; ++x) {
            const Cell &c = line[x];
            if (c.ch == U'\0')
                continue;
            text.append(QString::fromUcs4(&c.ch, 1));
        }
        if (li != endLine)
            text = text.trimmed().isEmpty() ? QString() : text;
        parts << text;
    }
    return parts.join(QLatin1Char('\n'));
}

// ----------------------------------------------------------- 尺寸调整 ---

void TerminalScreen::resize(int cols, int rows)
{
    cols = qMax(1, cols);
    rows = qMax(1, rows);
    if (cols == m_cols && rows == m_rows)
        return;

    // 行数变少时，把顶部挤出去的行推进回滚缓冲，别把内容弄丢
    if (rows < m_rows) {
        const int drop = m_rows - rows;
        for (int i = 0; i < drop && !m_screen.isEmpty(); ++i) {
            if (!m_usingAlt)
                pushScrollback(m_screen.first());
            m_screen.removeFirst();
        }
        m_cursorY -= drop;
    } else if (rows > m_rows) {
        for (int i = 0; i < rows - m_rows; ++i)
            m_screen.append(QVector<Cell>(cols));
    }

    // 列宽变化：多出来的补空格，超出的截掉
    for (auto &line : m_screen) {
        if (line.size() < cols)
            line.resize(cols);
        else if (line.size() > cols)
            line.resize(cols);
    }

    m_cols = cols;
    m_rows = rows;

    m_cursorX = qBound(0, m_cursorX, m_cols - 1);
    m_cursorY = qBound(0, m_cursorY, m_rows - 1);

    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;

    if (m_usingAlt) {
        m_altScreen.resize(m_rows);
        for (auto &line : m_altScreen)
            line.resize(m_cols);
    }

    m_wrapPending = false;
    m_dirty = true;
}

// ------------------------------------------------------------- 滚动 ---

void TerminalScreen::pushScrollback(const QVector<Cell> &line)
{
    m_scrollback.append(line);
    while (m_scrollback.size() > kMaxScrollback)
        m_scrollback.removeFirst();
}

void TerminalScreen::scrollUp(int count)
{
    if (count <= 0)
        return;

    const int regionHeight = m_scrollBottom - m_scrollTop + 1;
    count = qMin(count, regionHeight);

    // 只有「滚动区覆盖整屏」且不在备用屏幕时，滚出去的行才进历史
    const bool wholeScreen = (m_scrollTop == 0 && m_scrollBottom == m_rows - 1);

    for (int i = 0; i < count; ++i) {
        if (wholeScreen && !m_usingAlt)
            pushScrollback(m_screen.first());

        for (int y = m_scrollTop; y < m_scrollBottom; ++y)
            m_screen[y] = m_screen[y + 1];

        // 新空出来的行用当前背景色填充（BCE），vim 之类的配色依赖这个
        QVector<Cell> blank(m_cols);
        for (Cell &c : blank)
            c.bg = m_curBg;
        m_screen[m_scrollBottom] = blank;
    }
    m_dirty = true;
}

void TerminalScreen::scrollDown(int count)
{
    if (count <= 0)
        return;

    const int regionHeight = m_scrollBottom - m_scrollTop + 1;
    count = qMin(count, regionHeight);

    for (int i = 0; i < count; ++i) {
        for (int y = m_scrollBottom; y > m_scrollTop; --y)
            m_screen[y] = m_screen[y - 1];

        QVector<Cell> blank(m_cols);
        for (Cell &c : blank)
            c.bg = m_curBg;
        m_screen[m_scrollTop] = blank;
    }
    m_dirty = true;
}

void TerminalScreen::lineFeed()
{
    if (m_cursorY == m_scrollBottom)
        scrollUp(1);
    else if (m_cursorY < m_rows - 1)
        ++m_cursorY;
    m_wrapPending = false;
}

void TerminalScreen::reverseIndex()
{
    if (m_cursorY == m_scrollTop)
        scrollDown(1);
    else if (m_cursorY > 0)
        --m_cursorY;
    m_wrapPending = false;
}

void TerminalScreen::carriageReturn()
{
    m_cursorX = 0;
    m_wrapPending = false;
}

void TerminalScreen::backspace()
{
    if (m_cursorX > 0)
        --m_cursorX;
    m_wrapPending = false;
}

void TerminalScreen::tabForward(int count)
{
    for (int i = 0; i < count; ++i) {
        // 每 8 列一个制表位
        int next = ((m_cursorX / 8) + 1) * 8;
        if (next >= m_cols)
            next = m_cols - 1;
        m_cursorX = next;
        if (m_cursorX == m_cols - 1)
            break;
    }
    m_wrapPending = false;
}

// ------------------------------------------------------------- 编辑 ---

void TerminalScreen::clearCells(QVector<Cell> &line, int from, int to, PackedColor bg)
{
    if (line.isEmpty())
        return;
    from = qBound(0, from, line.size() - 1);
    to   = qBound(0, to, line.size() - 1);
    for (int x = from; x <= to; ++x) {
        Cell c;
        c.bg = bg;
        line[x] = c;
    }
}

void TerminalScreen::setCell(int x, int y, char32_t ch, int width, quint8 extraFlags)
{
    if (x < 0 || x >= m_cols || y < 0 || y >= m_rows)
        return;

    QVector<Cell> &line = m_screen[y];

    // 覆盖到全角字符的右半格时，连带把左边那格一起清掉，
    // 否则会剩下半个字
    if (line[x].ch == U'\0' && x > 0)
        line[x - 1] = Cell{};
    if (line[x].flags & CellWide && x + 1 < m_cols)
        line[x + 1] = Cell{};

    Cell c;
    c.ch    = ch;
    c.fg    = m_curFg;
    c.bg    = m_curBg;
    c.flags = quint8(m_curFlags | extraFlags);
    line[x] = c;

    if (width == 2 && x + 1 < m_cols) {
        line[x].flags |= CellWide;
        Cell cont;
        cont.ch    = U'\0';       // 右半格：占位，渲染时跳过
        cont.fg    = m_curFg;
        cont.bg    = m_curBg;
        cont.flags = quint8(m_curFlags | extraFlags);
        line[x + 1] = cont;
    }
}

void TerminalScreen::eraseInDisplay(int mode)
{
    switch (mode) {
    case 0:   // 光标到屏幕末尾
        clearCells(m_screen[m_cursorY], m_cursorX, m_cols - 1, m_curBg);
        for (int y = m_cursorY + 1; y < m_rows; ++y)
            clearCells(m_screen[y], 0, m_cols - 1, m_curBg);
        break;
    case 1:   // 屏幕开头到光标
        for (int y = 0; y < m_cursorY; ++y)
            clearCells(m_screen[y], 0, m_cols - 1, m_curBg);
        clearCells(m_screen[m_cursorY], 0, m_cursorX, m_curBg);
        break;
    case 2:   // 整屏
    case 3:
        for (int y = 0; y < m_rows; ++y)
            clearCells(m_screen[y], 0, m_cols - 1, m_curBg);
        if (mode == 3)
            m_scrollback.clear();
        break;
    default:
        break;
    }
    m_dirty = true;
}

void TerminalScreen::eraseInLine(int mode)
{
    QVector<Cell> &line = m_screen[m_cursorY];
    switch (mode) {
    case 0: clearCells(line, m_cursorX, m_cols - 1, m_curBg); break;
    case 1: clearCells(line, 0, m_cursorX, m_curBg); break;
    case 2: clearCells(line, 0, m_cols - 1, m_curBg); break;
    default: break;
    }
    m_dirty = true;
}

void TerminalScreen::insertChars(int count)
{
    if (count <= 0)
        return;
    QVector<Cell> &line = m_screen[m_cursorY];
    count = qMin(count, m_cols - m_cursorX);
    for (int x = m_cols - 1; x >= m_cursorX + count; --x)
        line[x] = line[x - count];
    clearCells(line, m_cursorX, m_cursorX + count - 1, m_curBg);
    m_dirty = true;
}

void TerminalScreen::deleteChars(int count)
{
    if (count <= 0)
        return;
    QVector<Cell> &line = m_screen[m_cursorY];
    count = qMin(count, m_cols - m_cursorX);
    for (int x = m_cursorX; x < m_cols - count; ++x)
        line[x] = line[x + count];
    clearCells(line, m_cols - count, m_cols - 1, m_curBg);
    m_dirty = true;
}

void TerminalScreen::eraseChars(int count)
{
    if (count <= 0)
        return;
    count = qMin(count, m_cols - m_cursorX);
    clearCells(m_screen[m_cursorY], m_cursorX, m_cursorX + count - 1, m_curBg);
    m_dirty = true;
}

void TerminalScreen::insertLines(int count)
{
    if (m_cursorY < m_scrollTop || m_cursorY > m_scrollBottom)
        return;
    count = qMin(count, m_scrollBottom - m_cursorY + 1);
    for (int i = 0; i < count; ++i) {
        for (int y = m_scrollBottom; y > m_cursorY; --y)
            m_screen[y] = m_screen[y - 1];
        QVector<Cell> blank(m_cols);
        for (Cell &c : blank)
            c.bg = m_curBg;
        m_screen[m_cursorY] = blank;
    }
    m_dirty = true;
}

void TerminalScreen::deleteLines(int count)
{
    if (m_cursorY < m_scrollTop || m_cursorY > m_scrollBottom)
        return;
    count = qMin(count, m_scrollBottom - m_cursorY + 1);
    for (int i = 0; i < count; ++i) {
        for (int y = m_cursorY; y < m_scrollBottom; ++y)
            m_screen[y] = m_screen[y + 1];
        QVector<Cell> blank(m_cols);
        for (Cell &c : blank)
            c.bg = m_curBg;
        m_screen[m_scrollBottom] = blank;
    }
    m_dirty = true;
}

void TerminalScreen::setCursor(int x, int y)
{
    m_cursorX = qBound(0, x, m_cols - 1);
    m_cursorY = qBound(0, y, m_rows - 1);
    m_wrapPending = false;
}

void TerminalScreen::saveCursor()
{
    m_saved.x = m_cursorX;
    m_saved.y = m_cursorY;
    m_saved.fg = m_curFg;
    m_saved.bg = m_curBg;
    m_saved.flags = m_curFlags;
}

void TerminalScreen::restoreCursor()
{
    setCursor(m_saved.x, m_saved.y);
    m_curFg = m_saved.fg;
    m_curBg = m_saved.bg;
    m_curFlags = m_saved.flags;
}

// 进入备用屏幕。
//
// 约定：m_screen 永远是「当前显示并被写入的那块缓冲区」，m_altScreen 只用来
// 寄存主屏的内容。所以这里先把主屏挪到 m_altScreen，再把 m_screen 清空当
// 备用屏用 —— 不能只是置个标志位，否则输出还是会写回主屏。
void TerminalScreen::enterAltScreen()
{
    if (m_usingAlt)
        return;

    m_altSaved.x = m_cursorX;
    m_altSaved.y = m_cursorY;
    m_altSaved.fg = m_curFg;
    m_altSaved.bg = m_curBg;
    m_altSaved.flags = m_curFlags;

    m_altScreen = m_screen;   // 主屏寄存起来

    for (auto &line : m_screen)
        clearCells(line, 0, m_cols - 1, kColorDefault);

    m_usingAlt = true;
    m_cursorX = 0;
    m_cursorY = 0;
    m_wrapPending = false;
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    m_dirty = true;
}

void TerminalScreen::leaveAltScreen()
{
    if (!m_usingAlt)
        return;

    m_usingAlt = false;

    m_screen = m_altScreen;   // 主屏内容换回来，备用屏的内容丢掉
    m_altScreen.clear();

    m_cursorX = qBound(0, m_altSaved.x, m_cols - 1);
    m_cursorY = qBound(0, m_altSaved.y, m_rows - 1);
    m_curFg = m_altSaved.fg;
    m_curBg = m_altSaved.bg;
    m_curFlags = m_altSaved.flags;

    m_wrapPending = false;
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    m_dirty = true;
}

void TerminalScreen::respond(const QByteArray &s)
{
    m_response.append(s);
}

QByteArray TerminalScreen::takeResponse()
{
    QByteArray out;
    out.swap(m_response);
    return out;
}

void TerminalScreen::scrollView(int lines)
{
    const int maxOffset = m_scrollback.size();
    m_viewOffset = qBound(0, m_viewOffset + lines, maxOffset);
    m_dirty = true;
}

void TerminalScreen::scrollToBottom()
{
    if (m_viewOffset != 0) {
        m_viewOffset = 0;
        m_dirty = true;
    }
}

// ----------------------------------------------------------- 输出字符 ---

void TerminalScreen::putChar(char32_t ch)
{
    if (isZeroWidthChar(ch))
        return;   // 组合字符不单独占格，直接丢掉

    const int width = isWideChar(ch) ? 2 : 1;

    // 上一次正好写到最后一列，延迟的换行现在生效
    if (m_wrapPending) {
        carriageReturn();
        lineFeed();
        m_wrapPending = false;
    }

    if (m_cursorX + width > m_cols) {
        if (m_autoWrap) {
            carriageReturn();
            lineFeed();
        } else {
            m_cursorX = qMax(0, m_cols - width);
        }
    }

    if (m_insertMode)
        insertChars(width);

    setCell(m_cursorX, m_cursorY, ch, width);
    m_cursorX += width;

    // 写到最后一列先不换行，等下一个字符来了再换（xterm 的延迟换行）
    if (m_cursorX >= m_cols) {
        m_cursorX = m_cols - 1;
        m_wrapPending = true;
    }

    if (m_viewOffset != 0)
        m_viewOffset = 0;   // 有新输出就跳到最新
    m_dirty = true;
}

// -------------------------------------------------------------- SGR ---

void TerminalScreen::applySgr()
{
    if (m_params.isEmpty()) {
        resetAttributes();
        return;
    }

    for (int i = 0; i < m_params.size(); ++i) {
        const int p = m_params[i];
        switch (p) {
        case 0:  resetAttributes(); break;
        case 1:  m_curFlags |= CellBold;   break;
        case 2:  m_curFlags |= CellDim;    break;
        case 3:  m_curFlags |= CellItalic; break;
        case 4:  m_curFlags |= CellUnderline; break;
        case 5:  m_curFlags |= CellBlink;  break;
        case 7:  m_curFlags |= CellInverse; break;
        case 8:  m_curFlags |= CellBold;   break;   // 隐藏，近似当粗体处理
        case 9:  m_curFlags |= CellStrike; break;
        case 21:
        case 22: m_curFlags &= quint8(~(CellBold | CellDim)); break;
        case 23: m_curFlags &= quint8(~CellItalic); break;
        case 24: m_curFlags &= quint8(~CellUnderline); break;
        case 25: m_curFlags &= quint8(~CellBlink); break;
        case 27: m_curFlags &= quint8(~CellInverse); break;
        case 29: m_curFlags &= quint8(~CellStrike); break;

        case 39: m_curFg = kColorDefault; break;
        case 49: m_curBg = kColorDefault; break;

        case 38:   // 扩展前景色
        case 48: { // 扩展背景色
            const bool isFg = (p == 38);
            if (i + 1 >= m_params.size())
                break;
            const int kind = m_params[++i];
            if (kind == 5 && i + 1 < m_params.size()) {
                const int idx = qBound(0, m_params[++i], 255);
                if (isFg) m_curFg = packIndexed(idx);
                else      m_curBg = packIndexed(idx);
            } else if (kind == 2 && i + 3 < m_params.size()) {
                const int r = qBound(0, m_params[++i], 255);
                const int g = qBound(0, m_params[++i], 255);
                const int b = qBound(0, m_params[++i], 255);
                if (isFg) m_curFg = packRgb(r, g, b);
                else      m_curBg = packRgb(r, g, b);
            }
            break;
        }

        default:
            if (p >= 30 && p <= 37)       m_curFg = packIndexed(p - 30);
            else if (p >= 40 && p <= 47)  m_curBg = packIndexed(p - 40);
            else if (p >= 90 && p <= 97)  m_curFg = packIndexed(p - 90 + 8);
            else if (p >= 100 && p <= 107) m_curBg = packIndexed(p - 100 + 8);
            break;
        }
    }
}

// ---------------------------------------------------------- 解析状态机 ---

void TerminalScreen::feed(const QByteArray &data)
{
    for (int i = 0; i < data.size(); ++i)
        parseByte(quint8(data.at(i)));
}

void TerminalScreen::parseByte(quint8 byte)
{
    // 非 Ground 状态下按各自的状态机走。控制序列里出现的字节不做 UTF-8 解码，
    // 所以必须先分发再落到下面的普通字符处理。
    if (m_state == State::Escape) {
        handleEscape(byte);
        return;
    }
    if (m_state == State::CsiEntry || m_state == State::CsiParam
        || m_state == State::CsiIntermediate) {
        handleCsiByte(byte);
        return;
    }
    if (m_state == State::Charset) {
        m_state = State::Ground;   // 字符集选择只吃一个字节，忽略
        return;
    }

    if (m_state == State::OscString) {
        // OSC 一直收到 BEL (0x07) 或 ST (ESC \) 才算完
        if (m_oscEscPending) {
            m_oscEscPending = false;
            if (byte == '\\') { handleOsc(); return; }
            // 不是 ST，把那个 ESC 丢掉，把当前字节当普通内容继续收
        }
        if (byte == 0x07) { handleOsc(); return; }
        if (byte == 0x1B) { m_oscEscPending = true; return; }
        m_oscBuffer.append(char(byte));
        return;
    }

    // ---- Ground ----
    if (byte == 0x1B) {
        m_state = State::Escape;
        return;
    }

    if (byte < 0x20) {
        switch (byte) {
        case 0x07: /* BEL，先不响铃 */ break;
        case 0x08: backspace();   break;
        case 0x09: tabForward();  break;
        case 0x0A: case 0x0B: case 0x0C: lineFeed(); break;
        case 0x0D: carriageReturn(); break;
        case 0x0E: case 0x0F: break;   // 字符集切换，忽略
        default: break;
        }
        return;
    }

    if (byte < 0x80) {
        putChar(char32_t(byte));
        return;
    }

    // ---- UTF-8 多字节 ----
    m_utf8Pending.append(char(byte));
    for (;;) {
        if (m_utf8Pending.isEmpty())
            break;
        const uchar *p = reinterpret_cast<const uchar *>(m_utf8Pending.constData());
        const int n = m_utf8Pending.size();
        const uchar b0 = p[0];

        int need = 0;
        char32_t cp = 0;
        if ((b0 & 0xE0) == 0xC0)      { need = 2; cp = b0 & 0x1F; }
        else if ((b0 & 0xF0) == 0xE0) { need = 3; cp = b0 & 0x0F; }
        else if ((b0 & 0xF8) == 0xF0) { need = 4; cp = b0 & 0x07; }
        else { m_utf8Pending.remove(0, 1); continue; }   // 非法起始字节

        if (n < need)
            break;   // 还没收齐，等下一批数据

        bool ok = true;
        for (int k = 1; k < need; ++k) {
            if ((p[k] & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (p[k] & 0x3F);
        }
        if (!ok) {
            m_utf8Pending.remove(0, 1);
            continue;
        }

        m_utf8Pending.remove(0, need);
        putChar(cp);
    }
}

void TerminalScreen::handleEscape(quint8 byte)
{
    m_state = State::Ground;

    switch (byte) {
    case '[':
        m_state = State::CsiEntry;
        m_params.clear();
        m_curParam = -1;
        m_paramHasDigits = false;
        m_privateMarker = false;
        return;
    case ']':
        m_state = State::OscString;
        m_oscBuffer.clear();
        m_oscEscPending = false;
        return;
    case '(': case ')': case '*': case '+': case '-': case '.':
        m_state = State::Charset;
        return;
    case '7': saveCursor(); return;
    case '8': restoreCursor(); return;
    case 'D': lineFeed(); return;                       // IND
    case 'E': carriageReturn(); lineFeed(); return;     // NEL
    case 'M': reverseIndex(); return;                   // RI
    case 'c': reset(); return;                          // RIS
    case '=': case '>': return;                         // 键盘模式，忽略
    case '\\': return;                                  // ST
    default:
        return;
    }
}

// CSI 序列内部的字节流转：参数字节 -> 中间字节 -> 结束字节
void TerminalScreen::handleCsiByte(quint8 byte)
{
    if (byte >= '0' && byte <= '9') {
        if (m_curParam < 0)
            m_curParam = 0;
        m_curParam = qMin(m_curParam * 10 + (byte - '0'), 65535);
        m_paramHasDigits = true;
        m_state = State::CsiParam;
        return;
    }

    if (byte == ';') {
        m_params.append(qMax(0, m_curParam));
        m_curParam = -1;
        m_paramHasDigits = true;
        m_state = State::CsiParam;
        return;
    }

    // 私有标记（?25h 这种），只能出现在第一个字节的位置
    if (byte == '?' || byte == '>' || byte == '!' || byte == '=') {
        if (m_state == State::CsiEntry)
            m_privateMarker = true;
        m_state = State::CsiParam;
        return;
    }

    // 0x20-0x2F 是中间字节（如 CSI ! p），本实现用不上，吃掉就行
    if (byte >= 0x20 && byte <= 0x2F) {
        m_state = State::CsiIntermediate;
        return;
    }

    // 0x40-0x7E 是结束字节，到这里这个序列就完整了
    if (byte >= 0x40 && byte <= 0x7E) {
        handleCsi(byte);
        m_state = State::Ground;
        return;
    }

    // 其它字节（含 C0 控制符）按规范忽略
}

void TerminalScreen::handleCsi(quint8 finalByte)
{
    // 收尾：把最后一段参数补进去
    if (m_curParam >= 0 || m_paramHasDigits || !m_params.isEmpty())
        m_params.append(qMax(0, m_curParam));

    auto arg = [this](int index, int def) -> int {
        if (index < 0 || index >= m_params.size())
            return def;
        return m_params[index];
    };

    if (m_privateMarker) {
        // 私有模式：?25h 显示光标、?1049h 切换备用屏幕 之类
        switch (finalByte) {
        case 'h':
        case 'l': {
            const bool set = (finalByte == 'h');
            for (int p : m_params) {
                switch (p) {
                case 1:   m_appCursorKeys = set; break;    // DECCKM
                case 7:   m_autoWrap = set; break;         // DECAWM
                case 25:  m_cursorVisible = set; break;    // DECTCEM
                case 1000: case 1002: case 1003: case 1006: break;  // 鼠标，暂不支持
                case 2004: m_bracketedPaste = set; break;  // 括号粘贴
                case 47: case 1047:
                    if (set) enterAltScreen(); else leaveAltScreen();
                    break;
                case 1049:
                    if (set) { saveCursor(); enterAltScreen(); }
                    else     { leaveAltScreen(); restoreCursor(); }
                    break;
                default: break;
                }
            }
            m_dirty = true;
            return;
        }
        case 'c':   // DA，应答一个 VT100 带 AVO
            respond("\x1b[?1;2c");
            return;
        default:
            return;
        }
    }

    switch (finalByte) {
    case 'A': m_cursorY = qMax(m_scrollTop, m_cursorY - qMax(1, arg(0, 1))); m_wrapPending = false; break;
    case 'B': m_cursorY = qMin(m_scrollBottom, m_cursorY + qMax(1, arg(0, 1))); m_wrapPending = false; break;
    case 'C': m_cursorX = qMin(m_cols - 1, m_cursorX + qMax(1, arg(0, 1))); m_wrapPending = false; break;
    case 'D': m_cursorX = qMax(0, m_cursorX - qMax(1, arg(0, 1))); m_wrapPending = false; break;
    case 'E':  // 下移并回到行首
        m_cursorY = qMin(m_scrollBottom, m_cursorY + qMax(1, arg(0, 1)));
        carriageReturn();
        break;
    case 'F':
        m_cursorY = qMax(m_scrollTop, m_cursorY - qMax(1, arg(0, 1)));
        carriageReturn();
        break;
    case 'G': case '`':
        m_cursorX = qBound(0, qMax(1, arg(0, 1)) - 1, m_cols - 1);
        m_wrapPending = false;
        break;
    case 'H': case 'f':
        // 注意顺序：CUP 是「行;列」，参数 0 是行、参数 1 是列，
        // 而 setCursor 收的是 (x, y)，所以这里要倒过来传
        setCursor(qMax(1, arg(1, 1)) - 1, qMax(1, arg(0, 1)) - 1);
        break;
    case 'I': tabForward(qMax(1, arg(0, 1))); break;
    case 'J': eraseInDisplay(arg(0, 0)); break;
    case 'K': eraseInLine(arg(0, 0)); break;
    case 'L': insertLines(qMax(1, arg(0, 1))); break;
    case 'M': deleteLines(qMax(1, arg(0, 1))); break;
    case 'P': deleteChars(qMax(1, arg(0, 1))); break;
    case 'X': eraseChars(qMax(1, arg(0, 1))); break;
    case '@': insertChars(qMax(1, arg(0, 1))); break;
    case 'S': scrollUp(qMax(1, arg(0, 1))); break;
    case 'T': scrollDown(qMax(1, arg(0, 1))); break;
    case 'Z': {   // 反向制表
        int count = qMax(1, arg(0, 1));
        while (count-- > 0) {
            int prev = ((m_cursorX - 1) / 8) * 8;
            if (prev < 0) prev = 0;
            m_cursorX = prev;
            if (m_cursorX == 0) break;
        }
        break;
    }
    case 'd':  // VPA，只动行
        m_cursorY = qBound(0, qMax(1, arg(0, 1)) - 1, m_rows - 1);
        break;
    case 'e':
        m_cursorY = qMin(m_rows - 1, m_cursorY + qMax(1, arg(0, 1)));
        break;
    case 'a':
        m_cursorX = qMin(m_cols - 1, m_cursorX + qMax(1, arg(0, 1)));
        m_wrapPending = false;
        break;
    case 'b': {   // REP，重复上一个字符
        const int count = qMax(1, arg(0, 1));
        if (m_cursorY >= 0 && m_cursorY < m_rows && m_cursorX > 0) {
            const char32_t last = m_screen[m_cursorY][m_cursorX - 1].ch;
            if (last != U'\0' && last != U' ')
                for (int i = 0; i < count; ++i) putChar(last);
        }
        break;
    }
    case 'm': applySgr(); break;
    case 'r': {   // DECSTBM，设置滚动区
        const int top = qMax(1, arg(0, 1)) - 1;
        const int bottom = arg(1, m_rows) - 1;
        if (top < bottom && bottom < m_rows) {
            m_scrollTop = top;
            m_scrollBottom = bottom;
            setCursor(0, m_scrollTop);
        }
        break;
    }
    case 's': saveCursor(); break;
    case 'u': restoreCursor(); break;
    case 'g': break;   // 制表位清除，本实现固定 8 列，忽略
    case 'n':   // DSR
        if (arg(0, 0) == 6) {
            respond("\x1b[" + QByteArray::number(m_cursorY + 1) + ";"
                    + QByteArray::number(m_cursorX + 1) + "R");
        } else if (arg(0, 0) == 5) {
            respond("\x1b[0n");
        }
        break;
    case 'c':   // DA（非私有）
        respond("\x1b[?1;2c");
        break;
    case 'h': case 'l': {
        // 非私有模式：4 = 插入模式
        const bool set = (finalByte == 'h');
        for (int p : m_params) {
            if (p == 4)
                m_insertMode = set;
        }
        break;
    }
    case 't':   // 窗口操作，忽略
    default:
        break;
    }

    m_dirty = true;
}

void TerminalScreen::handleOsc()
{
    // 形如 "0;标题" / "2;标题"
    const QByteArray body = m_oscBuffer;
    m_oscBuffer.clear();
    m_state = State::Ground;

    const int sep = body.indexOf(';');
    if (sep <= 0)
        return;

    const QByteArray code = body.left(sep);
    if (code == "0" || code == "2") {
        const QString newTitle = QString::fromUtf8(body.mid(sep + 1)).trimmed();
        if (!newTitle.isEmpty() && newTitle != m_title) {
            m_title = newTitle;
            m_dirty = true;
        }
    }
    // 其它 OSC（改颜色、剪贴板之类）暂不处理
}

} // namespace Em
