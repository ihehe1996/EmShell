#include "ui/Drawer.h"

#include "ui/Widgets.h"

#include <QEasingCurve>
#include <QEvent>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QScrollArea>

namespace Em {

namespace {

// 左侧那道阴影的宽度，和 EdgeShadow 自己写死的宽度是同一个值
constexpr int kShadowWidth = 12;
constexpr int kSlideMs = 180;

} // namespace

Drawer::Drawer(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("Drawer"));

    // 面板就是一块白底容器，不滚动 —— 要不要滚由里面的内容自己决定。
    // 纯 QWidget 必须显式打开 WA_StyledBackground，否则 QSS 给的 background
    // 是不画的（Qt 只对 QFrame/QLabel 之流默认开背景绘制）。
    m_panel = new QWidget(this);
    m_panel->setObjectName(QStringLiteral("DrawerPanel"));   // QSS 靠这个名字上白底
    m_panel->setAttribute(Qt::WA_StyledBackground, true);
    m_panel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    m_panelLayout = new QVBoxLayout(m_panel);
    m_panelLayout->setContentsMargins(0, 0, 0, 0);
    m_panelLayout->setSpacing(0);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new EdgeShadow(Qt::LeftEdge, this));
    layout->addWidget(m_panel);

    m_anim = new QPropertyAnimation(this, "pos", this);
    m_anim->setDuration(kSlideMs);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QPropertyAnimation::finished, this, [this] {
        if (!m_open)
            hide();   // 完全滑出父控件之后再真的藏起来，省得白画
    });

    if (parent)
        parent->installEventFilter(this);

    hide();
}

void Drawer::setContent(QWidget *content, int panelWidth)
{
    m_panel->setFixedWidth(panelWidth);
    m_panelLayout->addWidget(content);
    m_width = kShadowWidth + panelWidth;
    setFixedWidth(m_width);

    // 尺寸定下来先挪到看不见的地方，免得构造完闪一下
    repin();
}

void Drawer::toggle()
{
    m_open ? close() : open();
}

void Drawer::open()
{
    if (m_open)
        return;

    m_open = true;
    repin();
    show();
    raise();   // 得压在页面内容上面
    slideTo(parentWidget()->width() - m_width);
    emit opened();
}

void Drawer::close()
{
    if (!m_open)
        return;

    m_open = false;
    slideTo(parentWidget()->width());
    emit closed();
}

void Drawer::slideTo(int x)
{
    m_anim->stop();
    m_anim->setStartValue(pos());
    m_anim->setEndValue(QPoint(x, 0));
    m_anim->start();
}

void Drawer::repin()
{
    QWidget *host = parentWidget();
    if (!host)
        return;

    // 动画还在跑的话直接掐掉：它算的是按旧尺寸得到的目标位置，
    // 继续跑会滑到一个错的地方。
    m_anim->stop();
    resize(m_width, host->height());
    move(m_open ? host->width() - m_width : host->width(), 0);
}

bool Drawer::eventFilter(QObject *watched, QEvent *event)
{
    // 抽屉不在布局里，父控件变大变小它不会跟着动，得自己盯着
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        repin();
    return QWidget::eventFilter(watched, event);
}

} // namespace Em
