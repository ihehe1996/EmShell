#pragma once

#include <QWidget>

class QPropertyAnimation;
class QVBoxLayout;

namespace Em {

// 从右侧滑出的抽屉。
//
// **它不参与父控件的布局** —— 自己定位、自己贴边，收起时整个挪到父控件右边界
// 之外（子控件会被父控件裁掉，所以挪出去就等于看不见）。放进布局的话它会一直
// 占着面板的宽度，那就成了「常驻右栏」而不是抽屉了。
//
// 代价是父控件尺寸一变就得重新贴边，所以这里装了事件过滤器盯着父控件。
//
// 抽屉自己**不滚动**：内容想固定头部尾部、只滚中间，得自己在内容里安排
// （见 ConnectionFormPanel）。抽屉只负责滑和那块白底。
class Drawer : public QWidget
{
    Q_OBJECT

public:
    explicit Drawer(QWidget *parent);

    // 抽屉里的内容。panelWidth 是面板宽度（不含左侧那一道阴影的宽度）
    void setContent(QWidget *content, int panelWidth);

    bool isOpen() const { return m_open; }

public slots:
    void open();
    void close();
    void toggle();

signals:
    void opened();
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void slideTo(int x);
    void repin();          // 按父控件当前的尺寸重新贴边

    QWidget *m_panel = nullptr;
    QVBoxLayout *m_panelLayout = nullptr;
    QPropertyAnimation *m_anim = nullptr;
    int m_width = 0;       // 阴影 + 面板
    bool m_open = false;
};

} // namespace Em
