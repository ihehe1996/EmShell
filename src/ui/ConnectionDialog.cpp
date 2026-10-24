#include "ui/ConnectionDialog.h"

#include "app/Theme.h"
#include "ui/ConnectionFormPanel.h"
#include "ui/IconProvider.h"
#include "ui/SettingsPage.h"
#include "ui/Widgets.h"

#include <QDesktopServices>
#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QUrl>
#include <QEasingCurve>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QStringList>
#include <QVBoxLayout>
#include <QWindow>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

namespace Em {

namespace {

// DwmSetWindowAttribute 的 DWMWA_WINDOW_CORNER_PREFERENCE，
// 数值以 Windows SDK 为准（Win11 22000+）。和 MainWindow 里是同一个值。
constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmCornerRound = 2;   // DWMWCP_ROUND

// 左栏标题前那颗图标的边长。跟 18px 的标题放一行，比标题再大一点点就够，
// 用原来 32px 那版会高出一截，把标题压成一个小字。
constexpr int kRailIconSize = 24;

// 左栏底部的推荐链接。
//
// QSS 没有 text-decoration，QLabel 自己也不认 hover —— 想「平时无下划线、
// 悬浮时出现下划线」只能自己在进出事件里改字体，和侧栏底部的 SideBarLink
// 是同一个路子。链接色走 app.qss 的 role="railAd"，会跟着配色变。
class RailAdLink : public QLabel
{
public:
    explicit RailAdLink(const QString &text, const QUrl &url, QWidget *parent = nullptr)
        : QLabel(text, parent)
        , m_url(url)
    {
        setProperty("role", QStringLiteral("railAd"));
        setCursor(Qt::PointingHandCursor);
        setAlignment(Qt::AlignCenter);
        // 字号在这里也定一遍：QSS 要等 polish 之后才生效，而布局在构造时
        // 就按 width() 量过一次，14px 的默认字体放不下会被换成两行。
        setFont(Theme::uiFont(11));
        setWordWrap(false);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
            QDesktopServices::openUrl(m_url);
        QLabel::mouseReleaseEvent(event);
    }

    void enterEvent(QEnterEvent *event) override
    {
        setHovered(true);
        QLabel::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        setHovered(false);
        QLabel::leaveEvent(event);
    }

private:
    void setHovered(bool hovered)
    {
        QFont f = font();
        f.setUnderline(hovered);
        setFont(f);
        setProperty("hover", hovered);
        repolish(this);
    }

    QUrl m_url;
};

// 弹窗自己的背景宿主：和主窗口同一个思路，读同一份背景图设置，铺满整窗
// 再压一层同浓度蒙层。左栏用 nav-bg-glass、右栏用带 alpha 的 bg-panel，
// 图就会从两栏底下透上来，和主窗口侧栏 / 卡片的规律一致。
//
// 弹窗只在显示 / 改尺寸时重绘几次，这里直接按目标矩形平滑缩放，不做预缓存。
// 描边也在这里画：QSS 的 border 会被铺满整窗的图盖掉（和主窗口 #Root 同理）。
class DialogBackground : public QWidget
{
public:
    explicit DialogBackground(QWidget *parent = nullptr) : QWidget(parent) {}

    void setBackground(const QString &path, int dimPercent)
    {
        m_source = path.isEmpty() ? QPixmap() : QPixmap(path);
        m_dim = qBound(0, dimPercent, 90);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Theme::color(QStringLiteral("bg-app")));

        if (!m_source.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            const QSizeF imageSize = m_source.deviceIndependentSize();
            if (!imageSize.isEmpty()) {
                // 铺满 + 居中裁切
                const qreal scale = qMax(width() / imageSize.width(),
                                         height() / imageSize.height());
                const QSizeF drawn = imageSize * scale;
                const QRectF target((width() - drawn.width()) / 2,
                                    (height() - drawn.height()) / 2,
                                    drawn.width(), drawn.height());
                painter.drawPixmap(target, m_source, QRectF(m_source.rect()));
            }

            QColor overlay = Theme::color(QStringLiteral("bg-app"));
            overlay.setAlphaF(m_dim / 100.0);
            painter.fillRect(rect(), overlay);
        }

        QPen pen(Theme::color(QStringLiteral("border-strong")));
        pen.setWidth(1);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

private:
    QPixmap m_source;
    int m_dim = 75;
};

} // namespace

ConnectionDialog::ConnectionDialog(ConnectionStore *store, KeyStore *keys, QWidget *parent)
    : QDialog(parent)
{
    // 无边框 + 自绘。系统标题栏的颜色跟 App 的深色主题对不上，一块浅色横在
    // 顶上很突兀；整个弹窗做成一张两栏卡片。
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setSizeGripEnabled(false);
    setMinimumWidth(640);

    // 无边框窗口的圆角在 Win11 上默认会丢，要显式设 DwmSetWindowAttribute。
    // **但不能在这个构造函数里调 winId()** —— 此时对话框还没显示，QWidget 树里
    // 又挂在某个页面控件底下，先建原生句柄会让 Qt 沿着控件树给兄弟控件一路补建
    // 原生窗口，主窗口被切成一堆原生子窗口之后 WM_NCHITTEST 就到不了 MainWindow
    // 了 —— 表现是「开过一次对话框再关掉，标题栏就再也拖不动」。
    // 所以放到 showEvent 里，那时窗口已经由正常的 show 流程建好（见 showEvent）。

    m_form = new ConnectionFormPanel(store, keys, this);
    // 面板自己那套「标题 + 收起按钮 + 取消/创建」是给抽屉用的；弹窗里表单
    // 只有中间那一块，头和尾都换成弹窗自己的。
    m_form->setHeaderVisible(false);
    m_form->setFooterVisible(false);
    m_form->setContentMargins(QMargins(0, 0, 0, 0));

    connect(m_form, &ConnectionFormPanel::saved, this, [this](const Connection &c) {
        m_result = c;
        accept();
    });

    // ---- 左栏：图标 / 标题 / 说明 / 要点 / 底部提示 ----
    auto *rail = new QWidget(this);
    rail->setObjectName(QStringLiteral("DialogRail"));
    rail->setAttribute(Qt::WA_StyledBackground, true);
    rail->setFixedWidth(240);
    auto *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(24, 24, 24, 24);
    railLayout->setSpacing(0);

    // 图标和标题同一行：图标在左、标题在右，垂直居中对齐。
    auto *titleRow = new QWidget(rail);
    auto *titleLayout = new QHBoxLayout(titleRow);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(10);

    m_railIcon = new QLabel(titleRow);
    m_railIcon->setPixmap(Icons::pixmap(Icon::Server,
                                        Theme::color(QStringLiteral("nav-cta-bg")),
                                        kRailIconSize));
    m_railIcon->setFixedSize(kRailIconSize, kRailIconSize);
    titleLayout->addWidget(m_railIcon, 0, Qt::AlignVCenter);

    m_railTitle = makeLabel(QStringLiteral("新建连接"), "railTitle", titleRow);
    titleLayout->addWidget(m_railTitle, 0, Qt::AlignVCenter);
    titleLayout->addStretch();

    railLayout->addWidget(titleRow);
    railLayout->addSpacing(14);

    m_railText = makeLabel(QString(), "railText", rail);
    m_railText->setWordWrap(true);
    railLayout->addWidget(m_railText);
    railLayout->addSpacing(24);

    const auto addBullet = [&](const QString &text) {
        auto *row = new QWidget(rail);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(9);

        // 空心强调色小圆点，对应参考页的 fa-circle-o
        auto *dot = new QFrame(row);
        dot->setObjectName(QStringLiteral("RailBulletDot"));
        dot->setFixedSize(7, 7);
        auto *dotWrap = new QWidget(row);
        auto *dotLayout = new QVBoxLayout(dotWrap);
        dotLayout->setContentsMargins(0, 4, 0, 0);
        dotLayout->setSpacing(0);
        dotLayout->addWidget(dot);
        dotLayout->addStretch();

        auto *label = makeLabel(text, "railBullet", row);
        label->setWordWrap(true);
        m_railBullets << label;

        h->addWidget(dotWrap, 0, Qt::AlignTop);
        h->addWidget(label, 1);
        railLayout->addWidget(row);
        railLayout->addSpacing(12);
    };
    addBullet(QStringLiteral("默认 SSH 端口为 22"));
    addBullet(QStringLiteral("生产服务器建议关闭密码登录"));
    addBullet(QStringLiteral("支持分组、标签与备注"));

    // 底部提示贴到最低端，上面留出弹性空白
    railLayout->addStretch(1);

    auto *noteLine = new QFrame(rail);
    noteLine->setObjectName(QStringLiteral("RailNoteLine"));
    noteLine->setFixedHeight(1);
    railLayout->addWidget(noteLine);
    railLayout->addSpacing(12);

    // 左栏底部不放存储方式的提示，改成推荐位：图标 + 文字作为一整组居中，
    // 悬浮时文字出现下划线。
    auto *noteRow = new QWidget(rail);
    auto *noteLayout = new QHBoxLayout(noteRow);
    noteLayout->setContentsMargins(0, 0, 0, 0);
    noteLayout->setSpacing(0);
    auto *globeIcon = new QLabel(noteRow);
    globeIcon->setPixmap(Icons::pixmap(Icon::Globe,
                                       Theme::color(QStringLiteral("nav-text")), 13));
    auto *adLink = new RailAdLink(QStringLiteral("服务器 + 域名 一站式提供"),
                                  QUrl(QStringLiteral("https://idc.empass6.com/")),
                                  noteRow);
    noteLayout->addStretch();
    noteLayout->addWidget(globeIcon, 0, Qt::AlignVCenter);
    noteLayout->addSpacing(6);
    noteLayout->addWidget(adLink);
    noteLayout->addStretch();
    railLayout->addWidget(noteRow);

    // ---- 右栏：卡片表单 ----
    auto *pane = new QWidget(this);
    pane->setObjectName(QStringLiteral("DialogPane"));
    pane->setAttribute(Qt::WA_StyledBackground, true);
    auto *paneLayout = new QVBoxLayout(pane);
    paneLayout->setContentsMargins(0, 0, 0, 0);
    paneLayout->setSpacing(0);

    auto *topBar = new QWidget(pane);
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(24, 12, 16, 0);
    topLayout->setSpacing(0);

    // 窗口最上面的连接类型选项卡。复用设置页那套 segment 样式（两个可勾选
    // 按钮 + QSS），不单独造一套页签。
    auto *protocolTabs = new QWidget(topBar);
    protocolTabs->setObjectName(QStringLiteral("ProtocolTabs"));
    protocolTabs->setAttribute(Qt::WA_StyledBackground, true);
    auto *protocolLayout = new QHBoxLayout(protocolTabs);
    protocolLayout->setContentsMargins(3, 3, 3, 3);
    protocolLayout->setSpacing(3);

    const auto makeProtocolTab = [&](const QString &text, const QString &name) {
        auto *button = new QPushButton(text, protocolTabs);
        button->setObjectName(name);
        button->setProperty("role", QStringLiteral("segment"));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    m_sshTab = makeProtocolTab(QStringLiteral("SSH连接(Linux)"),
                               QStringLiteral("ProtocolSshTab"));
    m_rdpTab = makeProtocolTab(QStringLiteral("远程桌面连接(Windows)"),
                               QStringLiteral("ProtocolRdpTab"));
    m_sshTab->setChecked(true);
    protocolLayout->addWidget(m_sshTab);
    protocolLayout->addWidget(m_rdpTab);
    topLayout->addWidget(protocolTabs);
    topLayout->addStretch();

    // setCheckable 的按钮点自己会取消勾选，这里手动把互斥状态钉住
    connect(m_sshTab, &QPushButton::clicked, this, [this] {
        m_form->setProtocol(Protocol::Ssh);
        m_sshTab->setChecked(true);
        m_rdpTab->setChecked(false);
    });
    connect(m_rdpTab, &QPushButton::clicked, this, [this] {
        m_form->setProtocol(Protocol::Rdp);
        m_sshTab->setChecked(false);
        m_rdpTab->setChecked(true);
    });

    // 表单预填（编辑已有连接）时协议是代码设的，选项卡要跟着走
    connect(m_form, &ConnectionFormPanel::protocolChanged, this, [this](Protocol protocol) {
        const bool ssh = (protocol == Protocol::Ssh);
        m_sshTab->setChecked(ssh);
        m_rdpTab->setChecked(!ssh);
        updateRailForProtocol(protocol);
    });

    auto *closeButton = new QPushButton(topBar);
    closeButton->setProperty("role", "icon");
    closeButton->setFixedSize(30, 30);
    closeButton->setIcon(Icons::icon(Icon::Close,
                                     Theme::color(QStringLiteral("text-mute")), 14));
    closeButton->setIconSize(QSize(14, 14));
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setToolTip(QStringLiteral("关闭"));
    closeButton->setAutoDefault(false);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    topLayout->addWidget(closeButton);
    paneLayout->addWidget(topBar);

    auto *formArea = new QWidget(pane);
    auto *formAreaLayout = new QVBoxLayout(formArea);
    formAreaLayout->setContentsMargins(24, 10, 24, 0);
    formAreaLayout->setSpacing(0);
    formAreaLayout->addWidget(m_form);
    paneLayout->addWidget(formArea, 1);

    auto *footer = new QWidget(pane);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(24, 10, 24, 14);
    footerLayout->setSpacing(10);

    auto *cancel = new PillButton(QStringLiteral("取消"), PillButton::Ghost, footer);
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    m_submit = new PillButton(QStringLiteral("创建"), PillButton::Primary, footer);
    m_submit->setLeadingIcon(Icon::Check, 15);
    m_submit->setDefault(true);
    m_submit->setAutoDefault(true);
    connect(m_submit, &QPushButton::clicked, m_form, &ConnectionFormPanel::submit);

    footerLayout->addStretch();
    footerLayout->addWidget(cancel);
    footerLayout->addWidget(m_submit);
    paneLayout->addWidget(footer);

    // Tab：表单最后一个字段 → 取消 → 创建
    QWidget::setTabOrder(m_form->lastField(), cancel);
    QWidget::setTabOrder(cancel, m_submit);

    // ---- 组装 ----
    // 根容器负责画背景图 / 蒙层 / 那圈 1px 描边（无边框窗口得自己补）。
    auto *root = new DialogBackground(this);
    root->setObjectName(QStringLiteral("DialogRoot"));

    // 和主窗口读同一份设置：启用开关、图片路径、蒙层浓度。
    // 有图时左栏切到半透明轨道，图从深色底下透上来（和主窗口侧栏一致）。
    const QString background = SettingsPage::backgroundEnabled()
                                   ? SettingsPage::backgroundImagePath()
                                   : QString();
    root->setBackground(background, SettingsPage::backgroundDim());
    rail->setProperty("glass", !background.isEmpty());
    repolish(rail);

    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(1, 1, 1, 1);   // 给描边留位置
    rootLayout->setSpacing(0);
    rootLayout->addWidget(rail);
    rootLayout->addWidget(pane, 1);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(root);

    setHeader(QStringLiteral("新建连接"),
              QStringLiteral("配置服务器的连接信息，\n支持密码与密钥认证。"));

    // 高度按右栏内容来；左栏内容比它矮，跟着拉满整高。
    // 不能用 adjustSize()：那用的是 QScrollArea 被 Qt 削过的 sizeHint，
    // 出来的窗口会矮一截，最后两个字段要滚才看得见（见 formContentHeight）。
    const int height = topBar->sizeHint().height()
                       + formAreaLayout->contentsMargins().top()
                       + m_form->formContentHeight()
                       + footer->sizeHint().height() + 2;  // 上下各 1px 描边
    resize(680, height);

    // 屏幕矮的时候别把窗口顶出去 —— 中间那块本来就会滚
    const QScreen *targetScreen = parent ? parent->screen() : screen();
    const QRect room = targetScreen->availableGeometry();
    resize(qMin(width(), room.width() - 60), qMin(height, room.height() - 60));
}

void ConnectionDialog::setHeader(const QString &title, const QString &text)
{
    m_railTitle->setText(title);
    m_railText->setText(text);
}

// 左栏是 SSH 还是 RDP，说的完全是两件事：端口、认证方式、打开方式都不一样。
// 切换类型时整块换掉，别让 RDP 对话框左边还写着「默认 SSH 端口为 22」。
void ConnectionDialog::updateRailForProtocol(Protocol protocol)
{
    const bool rdp = (protocol == Protocol::Rdp);

    if (m_editing) {
        m_railTitle->setText(QStringLiteral("编辑连接"));
        m_railText->setText(QStringLiteral("修改这条连接的信息，\n保存后立即生效。"));
        // 编辑时的铅笔图标由 ConnectionDialog::edit 设好，这里不要覆盖
    } else {
        m_railTitle->setText(rdp ? QStringLiteral("新建远程桌面")
                                 : QStringLiteral("新建连接"));
        m_railText->setText(
            rdp ? QStringLiteral("配置远程桌面的连接信息，\n"
                                 "将调用系统自带的 mstsc 打开。")
                : QStringLiteral("配置服务器的连接信息，\n支持密码与密钥认证。"));
        m_railIcon->setPixmap(Icons::pixmap(rdp ? Icon::Monitor : Icon::Server,
                                            Theme::color(QStringLiteral("nav-cta-bg")),
                                            kRailIconSize));
    }

    const QStringList bullets =
        rdp ? QStringList{QStringLiteral("默认 RDP 端口为 3389"),
                          QStringLiteral("密码会以明文保存在本机"),
                          QStringLiteral("支持全屏、多显示器与管理会话")}
            : QStringList{QStringLiteral("默认 SSH 端口为 22"),
                          QStringLiteral("生产服务器建议关闭密码登录"),
                          QStringLiteral("支持分组、标签与备注")};

    for (int i = 0; i < m_railBullets.size() && i < bullets.size(); ++i)
        m_railBullets.at(i)->setText(bullets.at(i));
}

void ConnectionDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    // 主按钮的字由表单决定：新建是「创建」，编辑是「保存」，
    // 终端页传进来的是「保存并连接」。
    m_submit->setText(m_form->submitText());

    // 主窗口整体压暗。遮罩是主窗口的子控件，弹窗本身是独立顶层窗口，
    // 所以它永远在遮罩上面。
    if (!m_overlay) {
        QWidget *host = parentWidget() ? parentWidget()->window() : nullptr;
        if (host) {
            m_overlay = new QWidget(host);
            m_overlay->setObjectName(QStringLiteral("DialogMask"));
            m_overlay->setAttribute(Qt::WA_StyledBackground, true);
            m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            m_overlay->setGeometry(host->rect());
            m_overlay->show();
            m_overlay->raise();
        }
    }

    // 摆在主窗口中间。无边框窗口没有系统标题栏，Qt 自动居中时用的是
    // 页面控件的几何，位置会偏，这里显式算一次。
    if (QWidget *owner = parentWidget() ? parentWidget()->window() : nullptr) {
        const QRect ownerFrame = owner->frameGeometry();
        move(ownerFrame.center() - QPoint(frameGeometry().width() / 2,
                                          frameGeometry().height() / 2));
    }

    // 和参考页一样的入场：卡片淡入，遮罩已经在后面了。
    if (!m_fadeStarted) {
        m_fadeStarted = true;
        setWindowOpacity(0.0);
        auto *fade = new QPropertyAnimation(this, "windowOpacity", this);
        fade->setDuration(140);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // Win11 的无边框窗口默认没有圆角，这里要回来（原因见构造函数）。
    // 放在这里而不是构造函数里，是为了避开「提前 winId() 把主窗口整棵树
    // 原生化」那个坑 —— 此刻句柄已经由 show 流程建好了，winId() 只是取回它。
    if (!m_dwmApplied) {
        m_dwmApplied = true;
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        DwmSetWindowAttribute(hwnd, kDwmWindowCornerPreference,
                              &kDwmCornerRound, sizeof(kDwmCornerRound));
    }
}

void ConnectionDialog::done(int result)
{
    if (m_overlay) {
        m_overlay->hide();
        m_overlay->deleteLater();
        m_overlay = nullptr;
    }
    QDialog::done(result);
}

void ConnectionDialog::mousePressEvent(QMouseEvent *event)
{
    // 自绘标题栏之后窗口没有系统标题栏可拖，这里补回来。
    // startSystemMove() 比自己算 delta 再 move() 可靠：多显示器、贴边都交给系统。
    if (event->button() == Qt::LeftButton && windowHandle()) {
        windowHandle()->startSystemMove();
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

bool ConnectionDialog::createNew(ConnectionStore *store, KeyStore *keys, QWidget *parent,
                                 Connection *out, const QString &submitText)
{
    ConnectionDialog dialog(store, keys, parent);
    dialog.setWindowTitle(QStringLiteral("新建连接"));
    dialog.setHeader(QStringLiteral("新建连接"),
                     QStringLiteral("配置服务器的连接信息，\n支持密码与密钥认证。"));
    dialog.m_form->startNew();
    if (!submitText.isEmpty())
        dialog.m_form->setSubmitText(submitText);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (out)
        *out = dialog.m_result;
    return true;
}

bool ConnectionDialog::createFrom(const Connection &seed, ConnectionStore *store, KeyStore *keys,
                                  QWidget *parent, Connection *out, const QString &submitText)
{
    ConnectionDialog dialog(store, keys, parent);
    dialog.setWindowTitle(QStringLiteral("新建连接"));
    dialog.setHeader(QStringLiteral("新建连接"),
                     QStringLiteral("配置服务器的连接信息，\n支持密码与密钥认证。"));
    dialog.m_form->startNewWith(seed);
    if (!submitText.isEmpty())
        dialog.m_form->setSubmitText(submitText);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (out)
        *out = dialog.m_result;
    return true;
}

bool ConnectionDialog::edit(const Connection &existing, ConnectionStore *store, KeyStore *keys,
                            QWidget *parent, Connection *out)
{
    ConnectionDialog dialog(store, keys, parent);
    dialog.m_editing = true;
    dialog.setWindowTitle(QStringLiteral("编辑连接"));
    dialog.setHeader(QStringLiteral("编辑连接"),
                     QStringLiteral("修改这条连接的信息，\n保存后立即生效。"));
    dialog.m_railIcon->setPixmap(Icons::pixmap(Icon::Edit,
                                              Theme::color(QStringLiteral("nav-cta-bg")),
                                              kRailIconSize));
    dialog.m_form->startEdit(existing);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (out)
        *out = dialog.m_result;
    return true;
}

} // namespace Em
