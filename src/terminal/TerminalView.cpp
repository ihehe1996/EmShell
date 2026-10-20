#include "terminal/TerminalView.h"

#include "app/Theme.h"
#include "terminal/TerminalSession.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QKeySequence>
#include <QMenu>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>

namespace Em {

namespace {

// 256 色调色板里 16-231 是 6x6x6 的色立方，232-255 是灰阶
QColor from256(int index)
{
    if (index < 0 || index > 255)
        return QColor(Qt::magenta);

    if (index < 16)
        return Theme::ansiPalette()[index];

    if (index < 232) {
        static const int steps[6] = { 0, 95, 135, 175, 215, 255 };
        const int i = index - 16;
        return QColor(steps[(i / 36) % 6], steps[(i / 6) % 6], steps[i % 6]);
    }

    const int level = 8 + (index - 232) * 10;
    return QColor(level, level, level);
}

// 往一段文字里追加一个码点。等价于 QString::fromUcs4(&ch, 1)，
// 但不用每次新分配一个 QString —— 这行在一屏要跑上万次的循环里。
void appendCodePoint(QString &out, char32_t ch)
{
    if (ch <= 0xFFFF) {
        out.append(QChar(char16_t(ch)));
    } else {
        const char32_t v = ch - 0x10000;
        out.append(QChar(char16_t(0xD800 + (v >> 10))));
        out.append(QChar(char16_t(0xDC00 + (v & 0x3FF))));
    }
}

// 远端 SGR 1（加粗）用什么字重画。
//
// 等宽字体大多没有独立的 Bold 字形（Cascadia Mono / Consolas 只有 Regular 和
// Bold 两档），Qt 遇到 600 之类的中间值会合成「伪粗体」，笔画发糊。这里直接用
// Normal（400）：加粗的文字仍然靠颜色提亮来区分（见 resolveColor 里的 SGR 1
// 处理），只是不再把字形压粗。
constexpr QFont::Weight kTerminalBoldWeight = QFont::Normal;

// 两个格子的「外观」是否一定相同。
//
// 比的是打包值（不是解析后的颜色）：打包值一样，解析出来的颜色和字形必然
// 一样，所以可以放心把这两格合成一次绘制，省掉两次颜色解析。
// 反过来说不成立 —— 默认色和索引色可能解析成同一个 QColor，那种情况会
// 少合并一次，只是多画几笔，不会画错。
bool sameLook(const Cell &a, const Cell &b)
{
    constexpr quint8 kMask = CellBold | CellItalic | CellUnderline | CellStrike;
    return a.fg == b.fg && a.bg == b.bg && (a.flags & kMask) == (b.flags & kMask);
}

// 同一个穿行批次里需要完全一致的绘制属性
struct RunStyle
{
    QColor fg;
    QColor bg;
    quint8 style = 0;   // 只留会影响字形本身的位

    bool operator==(const RunStyle &o) const
    {
        return fg == o.fg && bg == o.bg && style == o.style;
    }
    bool operator!=(const RunStyle &o) const { return !(*this == o); }
};

} // namespace

TerminalView::TerminalView(QWidget *parent)
    : QWidget(parent)
{
    // **不要开 WA_OpaquePaintEvent**：term-bg 现在带一点透明度（背景图要能透上来，
    // 见 Theme.cpp）。开了这个属性 Qt 会假设整块都被画满，半透明的部分会露出
    // 没初始化的底色，而且父控件的背景不会被合成进来。
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::IBeamCursor);
    setMouseTracking(true);
    setAutoFillBackground(false);

    m_screen = std::make_unique<TerminalScreen>(80, 24);
    applyFont();

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(530);
    connect(m_blinkTimer, &QTimer::timeout, this, &TerminalView::blinkCursor);
    m_blinkTimer->start();
}

TerminalView::~TerminalView() = default;

// ---------------------------------------------------------------- 字号 ---

void TerminalView::applyFont()
{
    // 正文用 Consolas + 关 hinting，比 Cascadia Mono 细，观感更接近界面其他
    // 文字。加粗（SGR 1）靠颜色提亮区分，字重仍然是 Normal（见 kTerminalBoldWeight）。
    m_font = Theme::terminalFont(m_fontSize);
    const QFontMetricsF fm(m_font);
    m_cellWidth  = fm.horizontalAdvance(QLatin1Char('M'));
    m_cellHeight = fm.height();
    m_ascent     = fm.ascent();

    if (m_cellWidth < 1.0)
        m_cellWidth = 8.0;
    if (m_cellHeight < 1.0)
        m_cellHeight = 16.0;

    updateGridSize();
    update();
}

void TerminalView::setFontSize(int pixelSize)
{
    pixelSize = qBound(8, pixelSize, 40);
    if (pixelSize == m_fontSize)
        return;
    m_fontSize = pixelSize;
    applyFont();
}

void TerminalView::zoomIn()    { setFontSize(m_fontSize + 1); }
void TerminalView::zoomOut()   { setFontSize(m_fontSize - 1); }
void TerminalView::resetZoom() { setFontSize(14); }

// -------------------------------------------------------------- 网格 ---

void TerminalView::updateGridSize()
{
    if (!m_screen)
        return;

    const int cols = qMax(2, int((width()  - 2 * kPadX) / m_cellWidth));
    const int rows = qMax(2, int((height() - 2 * kPadY) / m_cellHeight));

    if (cols == m_screen->cols() && rows == m_screen->rows())
        return;

    m_screen->resize(cols, rows);
    if (m_session)
        m_session->resize(cols, rows);

    clearSelection();
}

void TerminalView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateGridSize();
}

// -------------------------------------------------------------- 会话 ---

void TerminalView::setSession(TerminalSession *session)
{
    if (m_session == session)
        return;

    if (m_session)
        m_session->disconnect(this);

    m_session = session;
    m_sessionEnded = false;

    if (m_session) {
        connect(m_session, &TerminalSession::dataReceived, this, &TerminalView::onSessionData);
        connect(m_session, &TerminalSession::exited, this, &TerminalView::onSessionExited);
        m_session->resize(m_screen->cols(), m_screen->rows());
    }
    update();
}

void TerminalView::onSessionData(const QByteArray &data)
{
    if (!m_screen)
        return;

    m_screen->feed(data);

    // DSR 之类的查询需要回写，不然程序会一直等
    const QByteArray reply = m_screen->takeResponse();
    if (!reply.isEmpty() && m_session)
        m_session->write(reply);

    if (m_screen->title() != m_lastTitle) {
        m_lastTitle = m_screen->title();
        emit titleChanged(m_lastTitle);
    }

    if (m_screen->dirty()) {
        m_screen->clearDirty();
        update();
    }
}

void TerminalView::onSessionExited(int code)
{
    m_sessionEnded = true;
    update();
    emit sessionFinished(code);
}

void TerminalView::blinkCursor()
{
    // 只有真的看得见光标时才需要重绘，省点 CPU
    if (!isVisible() || !hasFocus() || m_sessionEnded)
        return;
    if (m_screen && !m_screen->cursorVisible())
        return;
    if (m_screen && m_screen->viewOffset() != 0)
        return;

    m_cursorOn = !m_cursorOn;
    update();
}

// -------------------------------------------------------------- 绘制 ---

QColor TerminalView::resolveColor(const TermColors &colors, PackedColor packed,
                                  bool isForeground, quint8 flags) const
{
    if (isDefaultColor(packed))
        return isForeground ? colors.fg : colors.bg;

    if (isIndexedColor(packed)) {
        int idx = indexedValue(packed);
        // 粗体 + 前 8 个基色 = 对应的亮色，这是终端的老惯例
        if (isForeground && (flags & CellBold) && idx < 8)
            idx += 8;
        return from256(idx);
    }

    return QColor(int((packed >> 16) & 0xFF),
                  int((packed >> 8) & 0xFF),
                  int(packed & 0xFF));
}

void TerminalView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    // 默认前景/背景色先解析出来，下面逐格取色都从这儿拿。
    // 这两个值在一帧里不会变，别放到字符格循环里去（那是上万个格子）。
    const TermColors colors{ Theme::color(QStringLiteral("term-fg")),
                             Theme::color(QStringLiteral("term-bg")) };
    const QColor termBg = colors.bg;
    // term-bg 现在是全透明（文字直接画在页面背景 / 背景图上），透明就不要再铺一层。
    if (termBg.alpha() > 0)
        p.fillRect(rect(), termBg);

    if (!m_screen) {
        p.setFont(m_font);
        p.setPen(Theme::color(QStringLiteral("text-mute")));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("没有活动的会话"));
        return;
    }

    const int cols = m_screen->cols();
    const int rows = m_screen->rows();
    const int top  = topLine();

    m_font.setWeight(QFont::Normal);
    m_font.setItalic(false);
    m_font.setUnderline(false);

    for (int row = 0; row < rows; ++row) {
        const int lineIndex = top + row;
        const QVector<Cell> &line = m_screen->lineAt(lineIndex);
        if (line.isEmpty())
            continue;

        const qreal y = kPadY + row * m_cellHeight;
        int col = 0;

        while (col < cols && col < line.size()) {
            const Cell &c = line[col];

            // 全角字符的右半格已经由左边那格画过了
            if (c.ch == U'\0') {
                ++col;
                continue;
            }

            const quint8 styleBits = quint8(c.flags & (CellBold | CellItalic | CellUnderline | CellStrike));
            const bool wide = (c.flags & CellWide) != 0;

            RunStyle rs;
            rs.fg = resolveColor(colors, c.fg, true, c.flags);
            rs.bg = resolveColor(colors, c.bg, false, c.flags);
            if (c.flags & CellInverse)
                std::swap(rs.fg, rs.bg);
            if (c.flags & CellDim)
                rs.fg = rs.fg.darker(150);
            rs.style = styleBits;

            if (wide) {
                // 全角单独画：它占两格，直接铺成一条会被窄字符的等宽假设带偏
                const QRectF cellRect(kPadX + col * m_cellWidth, y,
                                      m_cellWidth * 2, m_cellHeight);
                if (rs.bg.alpha() > 0)
                    p.fillRect(cellRect, rs.bg);
                if (c.ch != U' ') {
                    m_font.setWeight((styleBits & CellBold) ? kTerminalBoldWeight
                                                            : QFont::Normal);
                    m_font.setItalic(styleBits & CellItalic);
                    m_font.setUnderline(styleBits & CellUnderline);
                    m_font.setStrikeOut(styleBits & CellStrike);
                    p.setFont(m_font);
                    p.setPen(rs.fg);
                    QString wideText;
                    appendCodePoint(wideText, c.ch);
                    p.drawText(cellRect, Qt::AlignHCenter | Qt::AlignVCenter, wideText);
                }
                col += 2;
                continue;
            }

            // 窄字符：把属性完全一致的连续格子合成一次绘制
            int runEnd = col;
            QString runText;
            runText.reserve(64);

            while (runEnd < cols && runEnd < line.size() && runEnd - col < 240) {
                const Cell &n = line[runEnd];
                if (n.ch == U'\0' || (n.flags & CellWide))
                    break;

                // 绝大多数情况走这条：打包值一样就直接延长这一段，不用解析颜色。
                if (!sameLook(c, n)) {
                    // 打包值不同不代表画出来一定不同（比如默认色和索引色落到了
                    // 同一个 QColor 上），真解析一遍再决定要不要断开
                    RunStyle nrs;
                    nrs.fg = resolveColor(colors, n.fg, true, n.flags);
                    nrs.bg = resolveColor(colors, n.bg, false, n.flags);
                    if (n.flags & CellInverse)
                        std::swap(nrs.fg, nrs.bg);
                    if (n.flags & CellDim)
                        nrs.fg = nrs.fg.darker(150);
                    nrs.style = quint8(n.flags & (CellBold | CellItalic | CellUnderline | CellStrike));

                    if (nrs != rs)
                        break;
                }

                appendCodePoint(runText, n.ch);
                ++runEnd;
            }

            if (runEnd == col) {   // 理论上不会发生，兜底防死循环
                runEnd = col + 1;
                runText.clear();
                appendCodePoint(runText, c.ch);
            }

            const QRectF runRect(kPadX + col * m_cellWidth, y,
                                 (runEnd - col) * m_cellWidth, m_cellHeight);
            if (rs.bg.alpha() > 0)
                p.fillRect(runRect, rs.bg);

            // 空白串不用画字
            if (runText.trimmed().size() > 0) {
                m_font.setWeight((rs.style & CellBold) ? kTerminalBoldWeight
                                                       : QFont::Normal);
                m_font.setItalic(rs.style & CellItalic);
                m_font.setUnderline(rs.style & CellUnderline);
                m_font.setStrikeOut(rs.style & CellStrike);
                p.setFont(m_font);
                p.setPen(rs.fg);

                // 用 baseline 绘制，避免不同字体的垂直居中差异导致行间跳动
                p.drawText(QPointF(runRect.left(), y + m_ascent), runText);
            }

            col = runEnd;
        }
    }

    // ---- 选区高亮 ----
    if (hasSelection()) {
        int lineA, colA, lineB, colB;
        selectionRange(lineA, colA, lineB, colB);

        QColor selColor = Theme::color(QStringLiteral("term-sel"));
        selColor.setAlpha(150);

        for (int li = lineA; li <= lineB; ++li) {
            const int row = li - top;
            if (row < 0 || row >= rows)
                continue;

            const QVector<Cell> &line = m_screen->lineAt(li);
            int from = (li == lineA) ? colA : 0;
            int to   = (li == lineB) ? colB : line.size() - 1;
            from = qBound(0, from, cols - 1);
            to   = qBound(0, to, cols - 1);
            if (to < from)
                continue;

            const QRectF r(kPadX + from * m_cellWidth,
                           kPadY + row * m_cellHeight,
                           (to - from + 1) * m_cellWidth,
                           m_cellHeight);
            p.fillRect(r, selColor);
        }
    }

    // ---- 光标 ----
    // 正在组字的时候不画方块光标，位置让给下面那段预编辑串
    const bool showCursor = hasFocus() && !m_sessionEnded && m_cursorOn
                            && m_preedit.isEmpty()
                            && m_screen->cursorVisible()
                            && m_screen->viewOffset() == 0;
    if (showCursor) {
        const int cx = m_screen->cursorX();
        const int cy = m_screen->cursorY();
        const QRectF cursorRect = cursorCellRect();

        const QColor cursorColor = Theme::color(QStringLiteral("term-cursor"));
        p.fillRect(cursorRect, cursorColor);

        // 把光标底下的字用背景色重画一遍，不然会被盖住
        const QVector<Cell> &line = m_screen->lineAt(top + cy);
        if (cx < line.size() && line[cx].ch != U'\0' && line[cx].ch != U' ') {
            m_font.setWeight(QFont::Normal);
            m_font.setItalic(false);
            m_font.setUnderline(false);
            m_font.setStrikeOut(false);
            p.setFont(m_font);
            // 光标是一块实色；底下那个字要用背景色「抠」出来。
            // 背景全透明时抠不了，改用前景色重画一遍。
            p.setPen(termBg.alpha() == 0 ? colors.fg : termBg);
            p.drawText(QPointF(cursorRect.left(), cursorRect.top() + m_ascent),
                       QString::fromUcs4(&line[cx].ch, 1));
        }
    }

    // ---- 输入法还没提交的那串字 ----
    // 它不属于字符网格，屏幕上没有对应的格子，只能现画在光标处。
    // 不画的话，打字的时候界面上一点反馈都没有，用起来像卡死了。
    if (!m_preedit.isEmpty() && hasFocus() && !m_sessionEnded) {
        const QRectF cursorRect = cursorCellRect();

        // 按**格数**截断，不是按字符数 —— 全角一个字占两格
        const int room = qMax(1, cols - m_screen->cursorX());
        QString shown;
        int used = 0;
        const QList<uint> points = m_preedit.toUcs4();
        for (uint cp : points) {
            const int w = isWideChar(cp) ? 2 : 1;
            if (used + w > room)
                break;
            appendCodePoint(shown, cp);
            used += w;
        }

        if (!shown.isEmpty()) {
            const QRectF preRect(cursorRect.left(), cursorRect.top(),
                                 used * m_cellWidth, m_cellHeight);
            if (colors.bg.alpha() > 0)
                p.fillRect(preRect, colors.bg);

            QFont preFont = m_font;
            preFont.setBold(false);
            preFont.setItalic(false);
            preFont.setStrikeOut(false);
            preFont.setUnderline(true);   // 下划线表示「还没定下来」
            p.setFont(preFont);
            p.setPen(colors.fg);
            p.drawText(QPointF(preRect.left(), preRect.top() + m_ascent), shown);
        }
    }
}

// ------------------------------------------------------------ 坐标换算 ---

int TerminalView::topLine() const
{
    if (!m_screen)
        return 0;
    return m_screen->totalLines() - m_screen->rows() - m_screen->viewOffset();
}

// 光标所在格子的矩形。绘制要用它，输入法也要用它来摆候选词窗口 ——
// 不给的话输入法的候选框会飘到屏幕角上去。
QRectF TerminalView::cursorCellRect() const
{
    if (!m_screen)
        return QRectF();
    return QRectF(kPadX + m_screen->cursorX() * m_cellWidth,
                  kPadY + m_screen->cursorY() * m_cellHeight,
                  m_cellWidth, m_cellHeight);
}

void TerminalView::cellAt(const QPoint &pos, int &line, int &col) const
{
    if (!m_screen) {
        line = col = 0;
        return;
    }
    const int row = qMax(0, int((pos.y() - kPadY) / m_cellHeight));
    col  = qMax(0, int((pos.x() - kPadX) / m_cellWidth));
    col  = qMin(col, m_screen->cols() - 1);
    line = topLine() + qMin(row, m_screen->rows() - 1);
    line = qBound(0, line, m_screen->totalLines() - 1);
}

// -------------------------------------------------------------- 选区 ---

void TerminalView::clearSelection()
{
    m_selActive = false;
    m_selDragging = false;
}

bool TerminalView::hasSelection() const
{
    if (!m_selActive)
        return false;
    return m_selAnchorLine != m_selHeadLine || m_selAnchorCol != m_selHeadCol;
}

void TerminalView::selectionRange(int &lineA, int &colA, int &lineB, int &colB) const
{
    lineA = m_selAnchorLine;
    colA  = m_selAnchorCol;
    lineB = m_selHeadLine;
    colB  = m_selHeadCol;

    if (lineA > lineB || (lineA == lineB && colA > colB)) {
        std::swap(lineA, lineB);
        std::swap(colA, colB);
    }
}

void TerminalView::selectWordAt(int line, int col)
{
    if (!m_screen)
        return;

    const QVector<Cell> &cells = m_screen->lineAt(line);
    auto isWordChar = [](char32_t ch) {
        return (ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z')
               || (ch >= U'0' && ch <= U'9')
               || ch == U'_' || ch == U'-' || ch == U'.' || ch == U'/'
               || ch == U':' || ch == U'@' || ch == U'~' || ch > 0x7F;
    };

    if (col >= cells.size())
        return;

    if (!isWordChar(cells[col].ch)) {
        // 双击空白处就选中这一行
        m_selAnchorLine = m_selHeadLine = line;
        m_selAnchorCol = 0;
        m_selHeadCol = cells.size() - 1;
        m_selActive = true;
        return;
    }

    int from = col;
    int to = col;
    while (from > 0 && isWordChar(cells[from - 1].ch))
        --from;
    while (to + 1 < cells.size() && isWordChar(cells[to + 1].ch))
        ++to;

    m_selAnchorLine = m_selHeadLine = line;
    m_selAnchorCol = from;
    m_selHeadCol = to;
    m_selActive = true;
}

void TerminalView::selectAll()
{
    if (!m_screen)
        return;
    m_selAnchorLine = 0;
    m_selAnchorCol  = 0;
    m_selHeadLine   = m_screen->totalLines() - 1;
    m_selHeadCol    = qMax(0, m_screen->cols() - 1);
    m_selActive = true;
    update();
}

void TerminalView::copySelection()
{
    if (!hasSelection() || !m_screen)
        return;

    int lineA, colA, lineB, colB;
    selectionRange(lineA, colA, lineB, colB);

    QString text;
    for (int li = lineA; li <= lineB; ++li) {
        const QVector<Cell> &line = m_screen->lineAt(li);
        int from = (li == lineA) ? colA : 0;
        int to   = (li == lineB) ? colB : line.size() - 1;
        from = qBound(0, from, line.size() - 1);
        to   = qBound(0, to, line.size() - 1);

        QString rowText;
        for (int x = from; x <= to; ++x) {
            if (line[x].ch == U'\0')
                continue;
            rowText.append(QString::fromUcs4(&line[x].ch, 1));
        }
        text += rowText;
        if (li != lineB)
            text += QLatin1Char('\n');
    }

    QApplication::clipboard()->setText(text);
}

void TerminalView::pasteFromClipboard()
{
    if (!m_session || m_sessionEnded)
        return;

    QString text = QApplication::clipboard()->text();
    if (text.isEmpty())
        return;

    // 终端里换行必须是 \r，直接发 \n 有些程序会只当换行不回车
    const bool multiline = text.contains(QLatin1Char('\n'))
                           || text.contains(QLatin1Char('\r'));
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\n'), QLatin1Char('\r'));

    // **单行不加括号粘贴**：bash/readline 会把括号里的内容用反色高亮，
    // 中文正好是宽字符，连起来就是一条白条，回车前一直亮着。
    // 多行仍然用括号粘贴，避免每一行被立即执行。
    if (m_screen && m_screen->bracketedPaste() && multiline)
        m_session->write("\x1b[200~" + text.toUtf8() + "\x1b[201~");
    else
        m_session->write(text.toUtf8());

    ensureCursorVisible();
}

QString TerminalView::dumpAllText() const
{
    if (!m_screen)
        return {};

    QStringList lines;
    for (int i = 0; i < m_screen->totalLines(); ++i)
        lines << m_screen->lineText(i);
    return lines.join(QLatin1Char('\n'));
}

void TerminalView::showNotice(const QString &text, bool defer, bool blankLineBefore)
{
    if (!m_screen || text.isEmpty())
        return;

    auto write = [this, text, blankLineBefore] {
        if (!m_screen)
            return;

        // 标签刚建出来、还没参与布局时，updateGridSize() 是按子控件的默认尺寸
        // （100×30）算的，屏幕可能只有十来个字符宽、两行高。这个尺寸下写提示会
        // 被折成好几行，前面的部分直接滚进回滚缓冲。先撑到一个正常尺寸；
        // 真实布局发生以后只会把它改大，内容不会丢。
        if (m_screen->cols() < 80 || m_screen->rows() < 24) {
            m_screen->resize(qMax(80, m_screen->cols()),
                             qMax(24, m_screen->rows()));
        }

        // blankLineBefore 控制要不要先换一行：
        //  - 连接失败 / 断开这类异步提示插在远端输出中间，光标可能停在提示符
        //    后面，先换行才不会和上一行粘在一起；
        //  - 连接开始时的那几条是紧接着写的（false）：上一条已经用 \r\n 收尾，
        //    光标就在新行行首，再补一个 \r\n 只会多出一整行空白。
        //
        // SGR 2 = 暗淡。渲染时会拿当前前景色再压暗一档，深色 / 浅色主题都适用，
        // 不用在这里写死颜色。
        const QString line = QStringLiteral("\x1b[2m%1\x1b[0m\r\n").arg(text);
        m_screen->feed(((blankLineBefore ? QStringLiteral("\r\n") : QString()) + line).toUtf8());
        m_screen->clearDirty();
        update();
    };

    // 延后一轮是给系统 ssh 客户端那条路径留的：它一启动就先发一串「隐藏光标 +
    // 清屏 + 光标归位」，那些数据已经排在事件队列里了；当场写的话提示刚落下就被
    // 那一屏清掉，看起来像没写。原生 SSH 在 started 之前不会有远端输出，直接写
    // 才能保证「正在连接 → 连接成功」的先后顺序。
    if (defer)
        QTimer::singleShot(0, this, write);
    else
        write();
}

// -------------------------------------------------------------- 输入 ---

void TerminalView::ensureCursorVisible()
{
    if (m_screen)
        m_screen->scrollToBottom();
}

void TerminalView::keyPressEvent(QKeyEvent *event)
{
    if (!m_session || m_sessionEnded) {
        QWidget::keyPressEvent(event);
        return;
    }

    // 输入法正在组字：这段时间键盘归输入法，敲下去的拼音字母和回车都由它处理。
    // 放过去的话，命令行里会先多出一串拼音，回车还会把半截命令直接提交掉。
    if (!m_preedit.isEmpty()) {
        event->accept();
        return;
    }

    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers();
    const bool ctrl  = mods.testFlag(Qt::ControlModifier);
    const bool shift = mods.testFlag(Qt::ShiftModifier);
    const bool alt   = mods.testFlag(Qt::AltModifier);

    // ---- 先处理程序自己的快捷键 ----

    // Ctrl+V 直接粘贴（Windows Terminal 的习惯）。Ctrl+Shift+V 也保留。
    // 注意不能把 Ctrl+C 也吃掉：那是终端里的中断信号。
    if (ctrl && !alt && key == Qt::Key_V) {
        pasteFromClipboard();
        return;
    }

    if (ctrl && shift) {
        if (key == Qt::Key_C) { copySelection(); return; }
        if (key == Qt::Key_V) { pasteFromClipboard(); return; }
        if (key == Qt::Key_A) { selectAll(); return; }
        if (key == Qt::Key_Plus || key == Qt::Key_Equal) { zoomIn(); return; }
        if (key == Qt::Key_Minus || key == Qt::Key_Underscore) { zoomOut(); return; }
        if (key == Qt::Key_0) { resetZoom(); return; }
    }

    // 翻回滚缓冲
    if (shift && (key == Qt::Key_PageUp || key == Qt::Key_PageDown)) {
        const int page = qMax(1, m_screen ? m_screen->rows() - 1 : 10);
        if (m_screen)
            m_screen->scrollView(key == Qt::Key_PageUp ? page : -page);
        update();
        return;
    }
    if (shift && key == Qt::Key_Home && m_screen) {
        m_screen->scrollView(m_screen->scrollbackCount());
        update();
        return;
    }
    if (shift && key == Qt::Key_End && m_screen) {
        m_screen->scrollToBottom();
        update();
        return;
    }

    ensureCursorVisible();

    const bool appCursor = m_screen && m_screen->applicationCursorKeys();
    QByteArray out;

    auto cursorKey = [appCursor](char letter) {
        // 应用光标模式（vim 之类会用）下用 SS3 形式
        return appCursor ? QByteArray("\x1bO") + letter
                         : QByteArray("\x1b[") + letter;
    };

    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:    out = "\r"; break;
    case Qt::Key_Backspace: out = "\x7f"; break;
    case Qt::Key_Tab:      out = shift ? "\x1b[Z" : "\t"; break;
    // Shift+Tab 送到这儿是 Key_Backtab，不是「Key_Tab 带 Shift」
    case Qt::Key_Backtab:  out = "\x1b[Z"; break;
    case Qt::Key_Escape:   out = "\x1b"; break;
    case Qt::Key_Up:       out = cursorKey('A'); break;
    case Qt::Key_Down:     out = cursorKey('B'); break;
    case Qt::Key_Right:    out = cursorKey('C'); break;
    case Qt::Key_Left:     out = cursorKey('D'); break;
    case Qt::Key_Home:     out = cursorKey('H'); break;
    case Qt::Key_End:      out = cursorKey('F'); break;
    case Qt::Key_Insert:   out = "\x1b[2~"; break;
    case Qt::Key_Delete:   out = "\x1b[3~"; break;
    case Qt::Key_PageUp:   out = "\x1b[5~"; break;
    case Qt::Key_PageDown: out = "\x1b[6~"; break;
    case Qt::Key_F1:       out = "\x1bOP"; break;
    case Qt::Key_F2:       out = "\x1bOQ"; break;
    case Qt::Key_F3:       out = "\x1bOR"; break;
    case Qt::Key_F4:       out = "\x1bOS"; break;
    case Qt::Key_F5:       out = "\x1b[15~"; break;
    case Qt::Key_F6:       out = "\x1b[17~"; break;
    case Qt::Key_F7:       out = "\x1b[18~"; break;
    case Qt::Key_F8:       out = "\x1b[19~"; break;
    case Qt::Key_F9:       out = "\x1b[20~"; break;
    case Qt::Key_F10:      out = "\x1b[21~"; break;
    case Qt::Key_F11:      out = "\x1b[23~"; break;
    case Qt::Key_F12:      out = "\x1b[24~"; break;
    default:
        break;
    }

    if (out.isEmpty()) {
        if (ctrl) {
            // Ctrl+A..Ctrl+Z -> 0x01..0x1A，另外几个符号也有对应控制码
            if (key >= Qt::Key_A && key <= Qt::Key_Z) {
                out = QByteArray(1, char(key - Qt::Key_A + 1));
            } else if (key == Qt::Key_Space) {
                out = QByteArray(1, char(0));
            } else if (key == Qt::Key_BracketLeft) {
                out = "\x1b";
            } else if (key == Qt::Key_Backslash) {
                out = QByteArray(1, char(0x1C));
            } else if (key == Qt::Key_BracketRight) {
                out = QByteArray(1, char(0x1D));
            } else if (key == Qt::Key_AsciiCircum) {
                out = QByteArray(1, char(0x1E));
            } else if (key == Qt::Key_Underscore || key == Qt::Key_Slash) {
                out = QByteArray(1, char(0x1F));
            }
        }

        if (out.isEmpty()) {
            const QString text = event->text();
            if (!text.isEmpty()) {
                const QString encoded = text;
                if (alt)
                    out = "\x1b" + encoded.toUtf8();   // Alt 在终端里就是 ESC 前缀
                else
                    out = encoded.toUtf8();
            }
        }
    }

    if (out.isEmpty()) {
        QWidget::keyPressEvent(event);
        return;
    }

    m_session->write(out);
    m_cursorOn = true;
    update();
    event->accept();
}

// Qt 的 QWidget::event() 在把 KeyPress 交给 keyPressEvent() **之前**，会先把
// Tab / Shift+Tab 截走去切换焦点（走 focusNextPrevChild）。终端里 Tab 是补全键，
// 必须原样发给 shell，所以这里一律不切。
//
// 这个坑很隐蔽：如果窗口里**只有**终端一个可聚焦控件，Qt 没别的地方可切，
// focusNextPrevChild 会返回 false，事件又漏回 keyPressEvent，看着一切正常；
// 真实界面里终端旁边有标签栏和按钮，于是 Tab 全被吃掉，一个都到不了 shell。
bool TerminalView::focusNextPrevChild(bool next)
{
    Q_UNUSED(next);
    return false;
}

// ---------------------------------------------------------- 输入法 ---

// 中文/日文这类输入法不是「按键 → 字符」，中间要经过组字。Qt 把这个过程
// 通过 QInputMethodEvent 送过来，跟 keyPressEvent 是两条独立的路径 ——
// 只处理 keyPressEvent 的话，输入法提交的中文会被整个丢掉，一个字都进不来。
void TerminalView::inputMethodEvent(QInputMethodEvent *event)
{
    const QString commit  = event->commitString();
    const QString preedit = event->preeditString();

    if (preedit != m_preedit) {
        m_preedit = preedit;
        update();
    }

    if (!commit.isEmpty() && m_session && !m_sessionEnded) {
        // ConPTY 和 SSH 两边收的都是 UTF-8，直接发过去
        m_session->write(commit.toUtf8());
        m_cursorOn = true;
        ensureCursorVisible();
    }

    event->accept();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;

    // 终端里不要联想和自动大写，输入法只要老老实实把字送过来就行
    case Qt::ImHints:
        return int(Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase);

    // 候选词窗口就摆在光标那一格上
    case Qt::ImCursorRectangle:
    case Qt::ImAnchorRectangle:
        return cursorCellRect().toRect();

    case Qt::ImAbsolutePosition:
        return mapToGlobal(cursorCellRect().topLeft().toPoint());

    // 光标在「命令行」里的位置。终端的这一行不归 Qt 管，
    // 报 0 就是「在开头」，够输入法用了
    case Qt::ImCursorPosition:
    case Qt::ImAnchorPosition:
        return 0;

    case Qt::ImSurroundingText:
    case Qt::ImCurrentSelection:
        return QString();

    default:
        return QWidget::inputMethodQuery(query);
    }
}

// -------------------------------------------------------------- 鼠标 ---

void TerminalView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    setFocus();

    int line, col;
    cellAt(event->pos(), line, col);
    m_selAnchorLine = m_selHeadLine = line;
    m_selAnchorCol  = m_selHeadCol  = col;
    m_selActive  = true;
    m_selDragging = true;
    update();
}

void TerminalView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_selDragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    int line, col;
    cellAt(event->pos(), line, col);

    // 拖到视图上下边缘外面时自动滚动
    const int row = int((event->pos().y() - kPadY) / m_cellHeight);
    if (m_screen) {
        if (event->pos().y() < kPadY)
            m_screen->scrollView(1);
        else if (event->pos().y() > height() - kPadY)
            m_screen->scrollView(-1);
        else if (row >= m_screen->rows())
            m_screen->scrollView(-1);
    }

    m_selHeadLine = line;
    m_selHeadCol  = col;
    update();
}

void TerminalView::mouseReleaseEvent(QMouseEvent *event)
{
    m_selDragging = false;

    // 只是点一下没拖动，就把选区清掉
    if (!hasSelection())
        clearSelection();

    QWidget::mouseReleaseEvent(event);
}

void TerminalView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    QAction *copy = menu.addAction(QStringLiteral("复制"));
    copy->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    copy->setEnabled(hasSelection());
    connect(copy, &QAction::triggered, this, &TerminalView::copySelection);

    QAction *paste = menu.addAction(QStringLiteral("粘贴"));
    paste->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    paste->setEnabled(m_session && !m_sessionEnded);
    connect(paste, &QAction::triggered, this, &TerminalView::pasteFromClipboard);

    menu.addSeparator();

    QAction *all = menu.addAction(QStringLiteral("全选"));
    all->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    all->setEnabled(m_screen && m_screen->totalLines() > 0);
    connect(all, &QAction::triggered, this, &TerminalView::selectAll);

    menu.exec(event->globalPos());
    event->accept();
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent *event)
{
    int line, col;
    cellAt(event->pos(), line, col);
    selectWordAt(line, col);
    update();
    event->accept();
}

void TerminalView::wheelEvent(QWheelEvent *event)
{
    if (!m_screen) {
        QWidget::wheelEvent(event);
        return;
    }

    const int steps = event->angleDelta().y() / 120;
    if (steps == 0) {
        QWidget::wheelEvent(event);
        return;
    }

    // 一次滚 3 行，跟大部分终端的手感一致
    m_screen->scrollView(steps * 3);
    update();
    event->accept();
}

void TerminalView::focusInEvent(QFocusEvent *event)
{
    m_cursorOn = true;
    update();
    QWidget::focusInEvent(event);
}

void TerminalView::focusOutEvent(QFocusEvent *event)
{
    // 失去焦点时组字就作废了。不主动清掉的话，那把「键盘归输入法」的
    // 标志会一直挂着，之后按什么键都发不出去。
    m_preedit.clear();
    update();
    QWidget::focusOutEvent(event);
}

} // namespace Em
