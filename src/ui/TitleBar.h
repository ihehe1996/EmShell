#pragma once

#include <QWidget>

namespace Em {

class WindowButton;

// 页头（自绘标题栏）。
//
// 窗口是无边框的（见 MainWindow::nativeEvent 里的 WM_NCHITTEST 处理），
// 拖动、双击最大化、贴边吸附这些系统行为由 Windows 负责，这里只放控件和装饰。
//
// 刻意做得普通：白底、一条浅分隔线、左边小图标加应用名、右边标准窗口按钮。
// 配色只用到 Theme 里的 brand-bg / brand-line / brand-text / brand-sub / win-* 这几个 token。
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    // 最大化状态变了，右下角那个按钮要在「最大化 / 还原」之间切图标
    void setMaximized(bool maximized);

signals:
    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();

private:
    WindowButton *m_maxButton = nullptr;
};

} // namespace Em
