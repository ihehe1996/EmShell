#pragma once

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QVector>

namespace Em {

// ---------------------------------------------------------------------------
// 颜色用 32 位打包，别在 Cell 里塞 QColor —— 回滚缓冲动辄几万行，
// 每格一个 QColor 内存会炸。
//
//   0xFF000000            默认前景/背景
//   0x01____II            调色板索引（0-255）
//   0x00RRGGBB            真彩色
// ---------------------------------------------------------------------------
using PackedColor = quint32;

constexpr PackedColor kColorDefault = 0xFF000000u;

inline PackedColor packIndexed(int index)
{
    return 0x01000000u | quint32(index & 0xFF);
}
inline PackedColor packRgb(int r, int g, int b)
{
    return quint32((r & 0xFF) << 16) | quint32((g & 0xFF) << 8) | quint32(b & 0xFF);
}
inline bool isDefaultColor(PackedColor c) { return c == kColorDefault; }
inline bool isIndexedColor(PackedColor c) { return (c >> 24) == 0x01u; }
inline int  indexedValue(PackedColor c)   { return int(c & 0xFFu); }

enum CellFlags : quint8 {
    CellBold      = 1 << 0,
    CellDim       = 1 << 1,
    CellItalic    = 1 << 2,
    CellUnderline = 1 << 3,
    CellBlink     = 1 << 4,
    CellInverse   = 1 << 5,
    CellStrike    = 1 << 6,
    CellWide      = 1 << 7,   // 全角字符占两格，这是左边那格
};

struct Cell
{
    char32_t    ch    = U' ';
    PackedColor fg    = kColorDefault;
    PackedColor bg    = kColorDefault;
    quint8      flags = 0;

    bool isBlank() const { return ch == U' ' && fg == kColorDefault && bg == kColorDefault
                                  && flags == 0; }
};

// 宽字符判定（东亚全角 + emoji）。用查表而不是量字体宽度，
// 这样屏幕逻辑和渲染完全解耦。
bool isWideChar(char32_t c);
// 组合字符（音标、变音符），宽度算 0
bool isZeroWidthChar(char32_t c);

// ---------------------------------------------------------------------------
// VT/xterm 屏幕缓冲区。
//
// 纯逻辑，不依赖任何 Widget —— 输入字节流，输出一个字符网格。
// TerminalView 负责把它画出来，LocalSession/SshSession 负责喂字节。
// ---------------------------------------------------------------------------
class TerminalScreen
{
public:
    TerminalScreen(int cols = 80, int rows = 24);

    void resize(int cols, int rows);
    void reset();                       // RIS，全部清空回到初始状态

    // 增量喂字节。转义序列、UTF-8 多字节字符都可能在两次调用之间被切断，
    // 内部状态会接上，不用调用方操心分片。
    void feed(const QByteArray &data);

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }

    // 统一行索引：0 .. scrollbackCount()-1 是历史，之后是当前屏幕
    int  scrollbackCount() const { return m_scrollback.size(); }
    int  totalLines() const { return m_scrollback.size() + m_rows; }
    const QVector<Cell> &lineAt(int index) const;

    int  cursorX() const { return m_cursorX; }
    int  cursorY() const { return m_cursorY; }
    bool cursorVisible() const { return m_cursorVisible; }

    // 用户往回滚了多少行（0 = 停在最新输出）
    int  viewOffset() const { return m_viewOffset; }
    void scrollView(int lines);
    void scrollToBottom();

    bool applicationCursorKeys() const { return m_appCursorKeys; }
    bool bracketedPaste() const { return m_bracketedPaste; }
    bool isAlternateScreen() const { return m_usingAlt; }

    // OSC 0/2 设的窗口标题，连接会话时用来命名标签页
    QString title() const { return m_title; }

    // 屏幕内容变了没有。TerminalView 每次 feed 之后看一眼，变了才重绘。
    bool dirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }

    // 解析过程中产生的、需要回写给远端/子进程的应答（比如光标位置查询）
    QByteArray takeResponse();

    // 取某段范围的纯文本，用于复制。坐标是「统一行索引 + 列」
    QString textInRange(int startLine, int startCol, int endLine, int endCol) const;
    // 一整行去掉尾部空白
    QString lineText(int index) const;

    // 光标当前所在行的文本（复制「当前命令行」用）
    QString currentLineText() const;

private:
    // ---- 底层操作 ----
    QVector<Cell> &screenLine(int y);
    const QVector<Cell> &screenLine(int y) const;
    void clearCells(QVector<Cell> &line, int from, int to, PackedColor bg);
    void setCell(int x, int y, char32_t ch, int width, quint8 extraFlags = 0);
    void scrollUp(int count);
    void scrollDown(int count);
    void pushScrollback(const QVector<Cell> &line);
    void lineFeed();
    void reverseIndex();
    void carriageReturn();
    void backspace();
    void tabForward(int count = 1);
    void eraseInDisplay(int mode);
    void eraseInLine(int mode);
    void insertChars(int count);
    void deleteChars(int count);
    void insertLines(int count);
    void deleteLines(int count);
    void eraseChars(int count);
    void setCursor(int x, int y);
    void saveCursor();
    void restoreCursor();
    void enterAltScreen();
    void leaveAltScreen();
    void respond(const QByteArray &s);

    // ---- 解析 ----
    void parseByte(quint8 byte);
    void handleEscape(quint8 byte);
    void handleCsiByte(quint8 byte);
    void handleCsi(quint8 finalByte);
    void handleOsc();
    void applySgr();
    void resetAttributes();
    void putChar(char32_t ch);
    void flushUtf8();

    // ---- 状态 ----
    int m_cols;
    int m_rows;

    QVector<QVector<Cell>> m_screen;      // 尺寸恒等于 m_rows
    QVector<QVector<Cell>> m_scrollback;  // 只有上限，没有下限

    int m_cursorX = 0;
    int m_cursorY = 0;
    bool m_cursorVisible = true;
    bool m_wrapPending = false;           // 「延迟换行」：写到最后一列先不换，再来一个字符才换

    // 滚动区域
    int m_scrollTop = 0;
    int m_scrollBottom = 0;

    PackedColor m_curFg = kColorDefault;
    PackedColor m_curBg = kColorDefault;
    quint8 m_curFlags = 0;

    bool m_appCursorKeys = false;
    bool m_bracketedPaste = false;
    bool m_autoWrap = true;
    bool m_insertMode = false;

    struct SavedCursor {
        int x = 0, y = 0;
        PackedColor fg = kColorDefault;
        PackedColor bg = kColorDefault;
        quint8 flags = 0;
    };
    SavedCursor m_saved;

    // 备用屏幕（vim / top 这类全屏程序用的）
    bool m_usingAlt = false;
    QVector<QVector<Cell>> m_altScreen;
    SavedCursor m_altSaved;

    QString m_title;
    QByteArray m_response;
    bool m_dirty = false;
    int m_viewOffset = 0;

    // 解析器状态机
    enum class State { Ground, Escape, CsiEntry, CsiParam, CsiIntermediate, OscString, Charset };
    State m_state = State::Ground;
    QVector<int> m_params;
    int m_curParam = -1;                  // -1 表示这一段还没出现过数字
    bool m_paramHasDigits = false;
    bool m_privateMarker = false;         // CSI 后面的 ? 或 > 或 !
    QByteArray m_oscBuffer;
    bool m_oscEscPending = false;

    // UTF-8 增量解码
    QByteArray m_utf8Pending;

    static constexpr int kMaxScrollback = 20000;
};

} // namespace Em
