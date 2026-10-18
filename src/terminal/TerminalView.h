#pragma once

#include "terminal/TerminalScreen.h"

#include <QVariant>
#include <QWidget>

#include <memory>

class QInputMethodEvent;
class QTimer;

namespace Em {

class TerminalSession;

// 终端显示控件：把 TerminalScreen 的字符网格画出来，并把手上的键盘输入
// 翻译成终端能懂的字节流。
//
// 它自己不做网络/进程，只跟 TerminalSession 打交道 —— 所以同一个控件
// 既能显示本地 shell，以后也能显示 SSH 会话。
class TerminalView : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalView(QWidget *parent = nullptr);
    ~TerminalView() override;

    // 挂上一个会话。之前挂着的会话不会被销毁，由调用方管理生命周期。
    void setSession(TerminalSession *session);
    TerminalSession *session() const { return m_session; }

    TerminalScreen *screen() const { return m_screen.get(); }

    // 当前会话结束了没有（用来显示「已退出」遮罩）
    bool sessionEnded() const { return m_sessionEnded; }

    // 字号加减
    void zoomIn();
    void zoomOut();
    void resetZoom();
    int  fontPointSize() const { return m_fontSize; }
    void setFontSize(int pixelSize);

    void copySelection();
    void pasteFromClipboard();
    void selectAll();

    // 把当前屏幕内容全部导出成文本
    QString dumpAllText() const;

    // 在终端里写一行提示（连接过程中用，比如「正在连接主机 …」）。
    // 走终端自己的 SGR 序列而不是另外浮一层文字：这行会真的进缓冲区，
    // 后面远端输出接在它下面，滚动、复制、选中都和普通输出一致。
    //
    // defer = true 时等一轮事件循环再写：系统 ssh 客户端启动时会先发一串
    // 「隐藏光标 + 清屏 + 光标归位」，那些数据已经排在事件队列里了，当场写会被
    // 整屏清掉。原生 SSH 在 started 之前不可能有任何远端输出，传 false 当场写，
    // 提示之间的先后顺序才有保证。
    //
    // blankLineBefore 默认在提示上面留一个空行，给连接开始时状态条出现、终端
    // 矮一行留出垫背。后续的提示（比如「连接主机成功」）要紧接着上一条写，
    // 传 false，否则两条之间会多出一行空白。
    void showNotice(const QString &text, bool defer = true, bool blankLineBefore = true);

    // 输入法会来问这些。QWidget 里它本来就是 public，别往 protected 里挪 ——
    // 收窄了访问权限，外面（包括测试）就调不到了。
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

signals:
    void titleChanged(const QString &title);
    void sessionFinished(int exitCode);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    // 右键菜单：复制 / 粘贴 / 全选
    void contextMenuEvent(QContextMenuEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

    // 输入法提交/组字的事件。中文不是按键事件，走的是这条路径，
    // 光有 keyPressEvent 的话一个字都进不来。
    void inputMethodEvent(QInputMethodEvent *event) override;

    // 终端里 Tab 是补全键，不能被 Qt 拿去做焦点切换。理由见 .cpp 里的实现。
    bool focusNextPrevChild(bool next) override;

private slots:
    void onSessionData(const QByteArray &data);
    void onSessionExited(int code);
    void blinkCursor();

private:
    // 一帧之内固定不变的那两个颜色。绘制前解析一次就够，别在字符格循环里
    // 反复调 Theme::color() —— 一屏上万次，具体见 Theme.cpp 里的 PaletteCache。
    struct TermColors
    {
        QColor fg;
        QColor bg;
    };

    void   applyFont();
    void   updateGridSize();
    int    topLine() const;
    QRectF cursorCellRect() const;
    QColor resolveColor(const TermColors &colors, PackedColor packed,
                        bool isForeground, quint8 flags) const;
    void   ensureCursorVisible();
    void   clearSelection();
    bool   hasSelection() const;
    void   selectionRange(int &lineA, int &colA, int &lineB, int &colB) const;
    void   cellAt(const QPoint &pos, int &line, int &col) const;
    void   selectWordAt(int line, int col);

    std::unique_ptr<TerminalScreen> m_screen;
    TerminalSession *m_session = nullptr;

    QFont  m_font;
    int    m_fontSize = 14;
    qreal  m_cellWidth = 8.0;
    qreal  m_cellHeight = 16.0;
    qreal  m_ascent = 12.0;

    bool   m_sessionEnded = false;
    QString m_lastTitle;

    // 输入法正在组的那串字（拼音串或还没选定的候选）。提交之后就空了，
    // 所以它同时也是「现在键盘归输入法管」的标志。
    QString m_preedit;

    // 光标闪烁
    QTimer *m_blinkTimer = nullptr;
    bool    m_cursorOn = true;

    // 选区，坐标是「统一行索引 + 列」
    bool m_selActive = false;
    bool m_selDragging = false;
    int  m_selAnchorLine = 0, m_selAnchorCol = 0;
    int  m_selHeadLine = 0,   m_selHeadCol = 0;

    static constexpr int kPadX = 10;
    static constexpr int kPadY = 8;
};

} // namespace Em
