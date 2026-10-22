#include "ui/TitleBar.h"

#include "app/Theme.h"
#include "ui/IconProvider.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>

namespace Em {

namespace {

// 应用图标。素材是 resources/icons/app-logo.png（200×200，带透明通道），
// 显示时按目标尺寸和屏幕缩放比降采样 —— 缩得比较狠（200 → 20 多），
// 所以必须用 SmoothTransformation，否则边缘会有锯齿。
QPixmap logoPixmap(int size)
{
    const QPixmap source(QStringLiteral(":/icons/app-logo.png"));
    if (source.isNull()) {
        qWarning("EmShell: 应用图标 :/icons/app-logo.png 没加载出来");
        return {};
    }

    const qreal dpr = qApp->devicePixelRatio();
    QPixmap scaled = source.scaled(QSize(qRound(size * dpr), qRound(size * dpr)),
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    return scaled;
}

} // namespace

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("TitleBar"));
    setFixedHeight(42);   // 普通的一行，不要高

    // 纯 QWidget 必须显式打开，否则 QSS 里 #TitleBar 的 background 和
    // border-bottom 一个都不画。之前背景能对上纯属巧合 —— 它一直是透明的，
    // 露出来的是 #Root 的底色。
    setAttribute(Qt::WA_StyledBackground, true);

    // ---- 左：应用图标 + 名字 ----
    auto *mark = new QLabel(this);
    mark->setFixedSize(20, 20);
    mark->setPixmap(logoPixmap(20));

    auto *brandName = new QLabel(QStringLiteral("EmShell"), this);
    brandName->setObjectName(QStringLiteral("BrandName"));

    // 用等宽字体。中文字体（微软雅黑）的拉丁字形是为中文顺带做的，做英文
    // 应用名明显不如专门的编程字体利落；而且等宽也贴合「这是个终端工具」。
    // 字号字重在这里定，QSS 只管颜色，免得两头都定义打架。
    brandName->setFont(Theme::monoFont(13, QFont::DemiBold));

    // ---- 右：窗口按钮 ----
    auto *minButton = new WindowButton(Icon::Minimize, QStringLiteral("min"), this);
    m_maxButton = new WindowButton(Icon::Maximize, QStringLiteral("max"), this);
    auto *closeButton = new WindowButton(Icon::Close, QStringLiteral("close"), this);

    connect(minButton, &QPushButton::clicked, this, &TitleBar::minimizeRequested);
    connect(m_maxButton, &QPushButton::clicked, this, &TitleBar::maximizeRestoreRequested);
    connect(closeButton, &QPushButton::clicked, this, &TitleBar::closeRequested);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 8, 0);
    layout->setSpacing(9);
    layout->addWidget(mark);
    layout->addWidget(brandName);
    layout->addStretch();
    layout->addWidget(minButton);
    layout->addWidget(m_maxButton);
    layout->addWidget(closeButton);
}

void TitleBar::setMaximized(bool maximized)
{
    m_maxButton->setIcon(Icons::icon(maximized ? Icon::Restore : Icon::Maximize,
                                     Theme::color(QStringLiteral("brand-sub")), 15));
}

void TitleBar::changeEvent(QEvent *event)
{
    // 最大化 / 还原图标是烘好的，配色变了要按新 token 重画。
    // 另外两个窗口按钮的图标由 WindowButton::changeEvent 自己处理。
    if (event->type() == QEvent::ThemeChange && m_maxButton)
        setMaximized(window() && window()->isMaximized());
    QWidget::changeEvent(event);
}

} // namespace Em
