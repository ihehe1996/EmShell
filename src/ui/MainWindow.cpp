#include "ui/MainWindow.h"

#include "app/AppConfig.h"
#include "app/Theme.h"
#include "model/ConnectionStore.h"
#include "model/KeyStore.h"
#include "ui/ConnectionsPage.h"
#include "ui/HistoryPage.h"
#include "ui/IconProvider.h"
#include "ui/KeysPage.h"
#include "ui/SettingsPage.h"
#include "ui/SideBar.h"
#include "ui/TerminalPage.h"
#include "ui/TitleBar.h"
#include "ui/UpdateDialog.h"
#include "ui/UpdatePage.h"
#include "ui/Widgets.h"
#include "update/UpdateChecker.h"

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QSystemTrayIcon>
#include <QKeySequence>
#include <QLineEdit>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

namespace Em {

namespace {

// 鼠标离窗口边缘多少像素以内算「抓边框」
constexpr int kResizeBorder = 7;

// 页头用系统原生的，还是用自绘的 TitleBar。
//
// 现在是 false（原生）。自绘那套（TitleBar 类 + WM_NCHITTEST 处理 + #Root
// 的描边）都还在代码里，改回 true 就能整体切回去，不用重写。
constexpr bool kUseCustomTitleBar = true;

// DwmSetWindowAttribute 的 dwAttribute 是 DWORD，而 dwmapi.h 里那套 DWMWA_*
// 是枚举 —— 没法用 #ifndef 探测。MinGW 的头文件版本不一定带 Win11 这几个值，
// 所以这里用自己的一套常量。数值以 Windows SDK 为准。
constexpr DWORD kDwmUseImmersiveDarkMode = 20;   // 1903+ (旧版是 19)
constexpr DWORD kDwmBorderColor          = 34;   // Win11 22000+
constexpr DWORD kDwmCaptionColor         = 35;   // Win11 22000+
constexpr DWORD kDwmTextColor            = 36;   // Win11 22000+
constexpr DWORD kDwmWindowCornerPreference = 33; // Win11 22000+


// 标签栏左侧那 12px 的补条。
//
// 内容区和侧栏之间夹着一条固定 12px 宽的 EdgeShadow。终端页的标签栏底色
// 铺到内容区左边缘就断了，于是标签栏和左侧菜单之间会留出一条很扎眼的缝。
// 这个补条盖在阴影的顶部（和标签栏等高），把轨道底色和底部发丝线接过去，
// 视觉上标签栏就一直连到侧栏边上。它只在终端页并且确实有标签时显示。
class TabBandFill : public QWidget
{
public:
    explicit TabBandFill(QWidget *parent = nullptr) : QWidget(parent)
    {
        // 它只补一块颜色，不接鼠标事件
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(Qt::NoPen);
        p.fillRect(rect(), Theme::color(QStringLiteral("term-rail-bg")));
        p.fillRect(QRect(0, height() - 1, width(), 1),
                   Theme::color(QStringLiteral("term-rail-line")));
    }
};


} // namespace

// 右侧内容区的底：用户设了背景图就画图 + 一层深色蒙层，没设就什么都不画，
// 让底下 #Root 的 bg-app 透上来。
//
// 它只包住 QStackedWidget，不包侧栏和标题栏 —— 背景图是「内容区」的装饰，
// 侧栏那条有色轨道要保持自己的底（玻璃态时单独按 nav-bg-glass 半透明），
// 不能直接让图透上来，否则导航项会浮在照片上。
class BackgroundHost : public QWidget
{
public:
    explicit BackgroundHost(QWidget *parent = nullptr) : QWidget(parent) {}

    void setBackground(const QString &path, int dimPercent)
    {
        m_source = path.isEmpty() ? QPixmap() : QPixmap(path);
        m_dim = qBound(0, dimPercent, 90);
        rebuildScaled();
        update();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        rebuildScaled();
    }

    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);

        // 没设背景图时这里就是整个窗口的底色（#Root 的 QSS background
        // 故意设成 transparent，见 app.qss）
        painter.fillRect(rect(), Theme::color(QStringLiteral("bg-app")));

        if (!m_scaled.isNull()) {
            // 图在 resize 时已经缩放 / 裁切好了，这里只是一次 blit ——
            // 终端每刷一帧都会带着父控件重绘，不能在这里做平滑缩放。
            painter.drawPixmap(QPoint(0, 0), m_scaled);

            // 蒙层。默认 75%，用户可以在设置里调；0 就是原图。
            QColor overlay = Theme::color(QStringLiteral("bg-app"));
            overlay.setAlphaF(m_dim / 100.0);
            painter.fillRect(rect(), overlay);
        }

        // 无边框窗口的 1px 描边。**不能用 QSS 的 #Root border**：
        // QSS 的边框是在 paintEvent 之前由 Qt 画的，这里铺满整块图会把它盖掉。
        if (property("frameless").toBool()) {
            QPen pen(Theme::color(QStringLiteral("border-strong")));
            pen.setWidth(1);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(rect().adjusted(0, 0, -1, -1));
        }

        Q_UNUSED(event);
    }

private:
    // 把源图按「铺满 + 居中裁切」预缩放到窗口大小。
    // 用物理像素缩放再把 dpr 写回 pixmap，125% 缩放下才不糊。
    void rebuildScaled()
    {
        m_scaled = QPixmap();
        if (m_source.isNull() || size().isEmpty())
            return;

        const qreal dpr = devicePixelRatioF();
        const QSize target(qRound(width() * dpr), qRound(height() * dpr));
        if (target.isEmpty())
            return;

        const qreal scale = qMax(qreal(target.width()) / m_source.width(),
                                 qreal(target.height()) / m_source.height());
        const QSize scaledSize(qCeil(m_source.width() * scale),
                               qCeil(m_source.height() * scale));
        const QPixmap scaled =
            m_source.scaled(scaledSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

        const int x = (scaled.width() - target.width()) / 2;
        const int y = (scaled.height() - target.height()) / 2;
        m_scaled = scaled.copy(x, y, target.width(), target.height());
        m_scaled.setDevicePixelRatio(dpr);
    }

    QPixmap m_source;
    QPixmap m_scaled;
    int m_dim = 75;
};

MainWindow::MainWindow(ConnectionStore *store, KeyStore *keys, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_keys(keys)
{
    // 版本号挂在标题最后。以前是放在侧栏左下角，那儿没人看还占地方。
    setWindowTitle(QStringLiteral("EmShell - 内部专用服务器终端工具 - v%1")
                       .arg(QCoreApplication::applicationVersion()));

    // 任务栏、Alt+Tab、以及原生标题栏左侧都用这个图标
    setWindowIcon(QIcon(QStringLiteral(":/icons/app-logo.png")));

    if (kUseCustomTitleBar)
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint);
    setMinimumSize(1080, 680);
    // 默认打开的尺寸。注意别写成比 setMinimumSize() 还小的值 —— 那样会被 Qt 悄悄
    // 抬到最小值，改了等于没改（原来这里写的 888x666 就是这个情况，实际一直开在
    // 1080x680）。
    resize(1120, 720);

    buildUi();
    wireSignals();
    reloadBackground();
    switchPage(AppPage::Connections);
    createTrayIcon();

    if (!kUseCustomTitleBar) {
        applyNativeTitleBarTheme();
    } else {
        // 无边框窗口（自绘标题栏）在 Win11 上默认会丢掉圆角，这里显式要回来
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        const DWORD corner = 2;   // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, kDwmWindowCornerPreference, &corner, sizeof(corner));
    }
}

void MainWindow::buildUi()
{
    // Root 本身就是背景图的宿主：整个窗口（标题栏、侧栏、内容、状态条）
    // 都画在它上面。它的 QSS 底色是 transparent，由 BackgroundHost::paintEvent
    // 负责铺 @bg-app 和背景图。
    //
    // 无边框时 Root 还要自己补一圈描边；用原生窗口时系统已经有边框了，
    // 再画一圈会变成双线。
    m_backgroundHost = new BackgroundHost(this);
    auto *root = m_backgroundHost;
    root->setObjectName(QStringLiteral("Root"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    root->setProperty("frameless", kUseCustomTitleBar);

    if (kUseCustomTitleBar)
        m_titleBar = new TitleBar(root);

    m_sideBar = new SideBar(m_store, root);

    m_stack = new QStackedWidget;
    m_stack->addWidget(m_connections = new ConnectionsPage(m_store, m_keys));
    // 分组页是一个常驻的实例：点哪个分组就重新 configure 一次再切过去。
    // 不要每个分组 new 一个页面 —— 分组会删会改，那样得额外管理生命周期
    m_stack->addWidget(m_groupPage   = new ConnectionsPage(m_store, m_keys));
    m_stack->addWidget(m_history     = new HistoryPage(m_store));
    m_stack->addWidget(m_favorites   = new ConnectionsPage(m_store, m_keys));
    m_stack->addWidget(m_terminal    = new TerminalPage(m_store, m_keys));
    m_stack->addWidget(m_keysPage    = new KeysPage(m_keys));
    m_stack->addWidget(m_settings    = new SettingsPage(m_store, m_keys));
    m_stack->addWidget(m_updatePage  = new UpdatePage());
    m_favorites->configureAsFavorites();

    // 内容行（侧栏 + 页面）和标题栏一样，都是填充在 Root 上的。
    // 侧栏自己会把背景压暗（见 #SideBar[glass="true"]），否则导航文字会浮在照片上。
    auto *content = new QWidget(root);
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(m_sideBar);
    // 夹在侧栏和内容区中间的那道投影。两块区域底色太接近，光靠 1px 描边
    // 分不出层次，加一层阴影才看得出侧栏是「浮」在内容区上面的
    m_edgeShadow = new EdgeShadow(Qt::RightEdge, content);
    contentLayout->addWidget(m_edgeShadow);
    contentLayout->addWidget(m_stack, 1);

    // 标签栏的补条：盖住阴影顶部，让标签栏底色连到侧栏边上。
    // 不在布局里 —— 位置跟着标签栏高度走，见 updateTabBandFill()。
    m_tabBandFill = new TabBandFill(content);
    m_tabBandFill->hide();

    auto *body = new QWidget(root);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    if (m_titleBar) {
        bodyLayout->addWidget(m_titleBar);
    } else {
        // 原生标题栏：在它下面垫一条同色窄条，把标题栏在视觉上拉高一点，
        // 底边就是那条分隔线（系统标题栏的高度改不了，见 QSS 里的说明）
        auto *captionStrip = new QFrame(body);
        captionStrip->setObjectName(QStringLiteral("CaptionStrip"));
        captionStrip->setFixedHeight(8);
        bodyLayout->addWidget(captionStrip);
    }
    bodyLayout->addWidget(content, 1);

    auto *outer = new QVBoxLayout(root);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(body);

    auto *shell = new QVBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->addWidget(root);
}

void MainWindow::wireSignals()
{
    // ---- 标题栏（只有用自绘页头时才有）----
    if (m_titleBar) {
        connect(m_titleBar, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);
        connect(m_titleBar, &TitleBar::maximizeRestoreRequested, this, [this] {
            isMaximized() ? showNormal() : showMaximized();
        });
        connect(m_titleBar, &TitleBar::closeRequested, this, &QWidget::close);
    }

    // ---- 侧边栏 ----
    connect(m_sideBar, &SideBar::pageRequested, this, &MainWindow::switchPage);
    // 顶上那个「+ 新建连接」：**就地弹**，不切页（所以直接叫「所有连接」页把对话框
    // 拉出来，落库逻辑和刷新都在它那儿，不用在 MainWindow 里再写一份）
    connect(m_sideBar, &SideBar::newConnectionRequested, this,
            [this] { m_connections->openNewConnection(); });

    // ---- 所有连接 ----
    connect(m_connections, &ConnectionsPage::connectRequested, this, &MainWindow::openConnection);

    // ---- 收藏夹 ----
    connect(m_favorites, &ConnectionsPage::connectRequested, this, &MainWindow::openConnection);

    // ---- 分组 ----
    // 侧栏自己负责新建/编辑/排序/删除（它就拿着 store），这里只接「点开了哪个分组」
    connect(m_sideBar, &SideBar::groupRequested, this, &MainWindow::openGroup);

    connect(m_groupPage, &ConnectionsPage::connectRequested, this, &MainWindow::openConnection);
    // 当前正开着的分组被改名 / 改色 / 删掉了
    connect(m_store, &ConnectionStore::groupsChanged, this, &MainWindow::handleGroupsChanged);

    // ---- 历史 ----
    connect(m_history, &HistoryPage::reconnectRequested, this, [this](const QString &id) {
        if (m_store) {
            if (const Connection *c = m_store->find(id))
                openConnection(*c);
        }
    });

    // ---- 终端 ----
    // 终端页空状态里的「查看所有连接」：切页交给这里
    connect(m_terminal, &TerminalPage::browseConnectionsRequested, this,
            [this] { switchPage(AppPage::Connections); });
    // 标签栏左侧的补条跟着「切到终端页 / 标签数变化」显隐
    connect(m_stack, &QStackedWidget::currentChanged, this, [this] { updateTabBandFill(); });
    connect(m_terminal, &TerminalPage::tabsChanged, this, &MainWindow::updateTabBandFill);

    // ---- 软件更新 ----
    connect(m_updatePage, &UpdatePage::checkUpdateRequested, this, &MainWindow::checkForUpdates);

    // ---- 设置 ----
    connect(m_settings, &SettingsPage::backgroundChanged, this, &MainWindow::reloadBackground);
    connect(m_settings, &SettingsPage::terminalFontSizeChanged, this, [this](int) {
        // 已开的终端下次打开时生效，这里只提示，不逐个去改
    });

    // ---- 快捷键 ----
    auto shortcut = [this](const QKeySequence &keys, auto slot) {
        auto *s = new QShortcut(keys, this);
        s->setContext(Qt::WindowShortcut);
        connect(s, &QShortcut::activated, this, slot);
    };

    shortcut(QKeySequence(QStringLiteral("Ctrl+N")), [this] { newConnection(); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+T")), [this] {
        switchPage(AppPage::Terminal);
        m_terminal->openLocalSession(SettingsPage::preferredShell());
    });
    shortcut(QKeySequence(QStringLiteral("Ctrl+2")), [this] { switchPage(AppPage::Connections); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+3")), [this] { switchPage(AppPage::Terminal); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+4")), [this] { switchPage(AppPage::Keys); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+5")), [this] { switchPage(AppPage::Settings); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+6")), [this] { switchPage(AppPage::Updates); });
}

void MainWindow::switchPage(AppPage page)
{
    m_currentGroupId.clear();
    m_sideBar->setCurrentPage(page);

    switch (page) {
    case AppPage::Connections: m_stack->setCurrentWidget(m_connections); break;
    case AppPage::History:     m_stack->setCurrentWidget(m_history);     break;
    case AppPage::Favorites:   m_stack->setCurrentWidget(m_favorites);   break;
    case AppPage::Terminal:    m_stack->setCurrentWidget(m_terminal);    break;
    case AppPage::Keys:        m_stack->setCurrentWidget(m_keysPage);    break;
    case AppPage::Settings:    m_stack->setCurrentWidget(m_settings);    break;
    case AppPage::Updates:     m_stack->setCurrentWidget(m_updatePage);  break;
    }

    // 终端页一个标签都没有时，TerminalPage 自己会显示空状态页，
    // 这里**不要**顺手开一个本地终端兜底 —— 那样会连带出两个毛病：
    // Ctrl+T 先被这里开一个、再显式开一个（一次按出两个标签），
    // 以及点连接时先蹦出一个本地终端、再开 SSH 标签。
}

void MainWindow::updateTabBandFill()
{
    if (!m_tabBandFill || !m_edgeShadow || !m_stack || !m_terminal)
        return;

    // 只在「当前是终端页」并且「确实有标签」时显示 —— 空状态没有标签栏，
    // 补一条颜色反而像凭空多出来一小块。
    const bool visible = m_stack->currentWidget() == m_terminal
                         && m_terminal->tabCount() > 0;
    const int h = visible ? m_terminal->tabBarHeight() : 0;
    if (!visible || h <= 0) {
        m_tabBandFill->hide();
        return;
    }

    // 阴影是 content 的直接子控件，补条也是，所以坐标可以直接照抄。
    const QRect shadow = m_edgeShadow->geometry();
    m_tabBandFill->setGeometry(shadow.x(), m_stack->y(), shadow.width(), h);
    m_tabBandFill->raise();   // 必须压在 EdgeShadow 上面才盖得住
    m_tabBandFill->show();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateTabBandFill();
}

void MainWindow::reloadBackground()
{
    if (!m_backgroundHost)
        return;

    // 开关关掉时图片和蒙层设置都留着，只是不显示 —— 侧栏也跟着回到实心轨道
    const QString path = SettingsPage::backgroundEnabled()
                             ? SettingsPage::backgroundImagePath()
                             : QString();
    const int dim = SettingsPage::backgroundDim();
    m_backgroundHost->setBackground(path, dim);

    // 「新建连接」和搜索框的底色透明度直接跟背景配置里的蒙层浓度走：
    // 用户在设置里调多少，这两个控件就是多少（没有背景图时恢复主题实色）。
    if (m_sideBar)
        m_sideBar->setGlassOpacity(path.isEmpty() ? -1 : dim);
    for (ConnectionsPage *page : { m_connections, m_favorites, m_groupPage }) {
        if (page)
            page->setGlassOpacity(path.isEmpty() ? -1 : dim);
    }

    // 有背景图时侧栏换成半透明轨道，让图也「占」到菜单底下。
    // 动态属性改了必须 repolish，否则 QSS 不会重新匹配。
    if (m_sideBar) {
        const bool glass = !path.isEmpty();
        if (m_sideBar->property("glass").toBool() != glass) {
            m_sideBar->setProperty("glass", glass);
            repolish(m_sideBar);
        }
    }

    // 补条画的是半透明色，底下的背景图换了得重画一遍才对得上
    if (m_tabBandFill)
        m_tabBandFill->update();
}

void MainWindow::checkForUpdates()
{
    UpdateDialog dialog(this);

    // 查完把结果同步到侧栏：有新版本就挂红色版本徽标，已经是最新就摘掉。
    // 查询失败不发这个信号，上一次的徽标保持不变。
    connect(&dialog, &UpdateDialog::checkCompleted, this,
            [this](bool available, const Version &latest) {
                m_sideBar->setUpdateAvailable(available ? latest.toString() : QString());
            });

    dialog.startCheck();
    dialog.exec();
}

void MainWindow::startSilentUpdateCheck()
{
    // 没配更新源就别发请求了。手动点「检查更新」时会给出明确提示，
    // 启动时没必要用一句错误说明拦住用户。
    if (updateRepo().isEmpty())
        return;

    if (!m_silentChecker) {
        m_silentChecker = new UpdateChecker(this);
        connect(m_silentChecker, &UpdateChecker::checkFinished, this,
                [this](const UpdateChecker::Result &result) {
                    // 只有真查到新版本才弹窗：查失败、已是最新都不该无端拦人。
                    //
                    // 侧栏的红色版本徽标是常驻提示：对话框关掉之后，
                    // 用户还能看到「有新版本」这件事。
                    if (!result.ok)
                        return;
                    m_sideBar->setUpdateAvailable(result.updateAvailable
                                                      ? result.latest.toString()
                                                      : QString());
                    if (!result.updateAvailable)
                        return;
                    UpdateDialog dialog(this);
                    dialog.showResult(result);   // 结果现成的，别再查一遍
                    dialog.exec();
                });
    }

    m_silentChecker->check();
}

void MainWindow::openGroup(const QString &groupId)
{
    if (!m_store || !m_store->findGroup(groupId))
        return;

    m_currentGroupId = groupId;
    m_groupPage->showGroup(groupId);
    m_stack->setCurrentWidget(m_groupPage);
    // 侧栏这边不是 AppPage，单独告诉它高亮哪一项
    m_sideBar->setCurrentGroup(groupId);
}

void MainWindow::handleGroupsChanged()
{
    if (m_currentGroupId.isEmpty() || !m_store)
        return;

    if (const ConnectionGroup *group = m_store->findGroup(m_currentGroupId)) {
        // 还开着这一页：改名 / 改色后把标题和色点刷新一下
        m_groupPage->showGroup(group->id);
        return;
    }

    // 正在看的分组被删了 —— 别把用户晾在一个已经不存在（列表必然为空）的页上
    m_currentGroupId.clear();
    switchPage(AppPage::Connections);
}

void MainWindow::openConnection(const Connection &connection)
{
    switchPage(AppPage::Terminal);
    m_terminal->openConnection(connection);

    if (m_store && !connection.id.isEmpty())
        m_store->recordUse(connection.id, true);
}

// 新建 / 编辑都走「所有连接」页右侧那个抽屉，不再弹模态对话框。
// 所以这里先切到那一页再让页面把抽屉拉出来。
void MainWindow::newConnection()
{
    switchPage(AppPage::Connections);
    m_connections->openNewConnection();
}

void MainWindow::editConnection(const QString &id)
{
    switchPage(AppPage::Connections);
    m_connections->openEditConnection(id);
}

// 原生标题栏改色。Win11 的 DWM 支持直接设标题栏底色 / 文字色，不需要自绘；
// Win10 上这几个属性不存在，调用会失败（返回错误码，不崩），标题栏保持系统默认。
//
// 注意 COLORREF 的字节序是 0x00BBGGRR，和 QColor 的 RGB 正好相反，
// 必须用 RGB() 宏转，别直接塞 c.rgb()。
void MainWindow::applyNativeTitleBarTheme()
{
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;

    // 先声明「深色窗口」：标题文字和右上角那三个按钮会跟着变浅。
    // 只设底色不设这条，按钮还是深灰的，压在深底上几乎看不见。
    // 跟着当前配色走，将来浅色主题下标题文字才不会变成白底白字。
    const BOOL dark = (Theme::paletteKey() == QLatin1String("dark")) ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));

    auto toColorRef = [](const QColor &c) {
        return COLORREF(RGB(c.red(), c.green(), c.blue()));
    };

    // 底色 = 页面底色，和右侧内容区严丝合缝
    const COLORREF caption = toColorRef(Theme::color(QStringLiteral("bg-app")));
    DwmSetWindowAttribute(hwnd, kDwmCaptionColor, &caption, sizeof(caption));

    const COLORREF text = toColorRef(Theme::color(QStringLiteral("text")));
    DwmSetWindowAttribute(hwnd, kDwmTextColor, &text, sizeof(text));

    // 外圈那 1px 边框也统一，不然暗色标题栏下面会留一条系统默认的浅色线
    const COLORREF border = toColorRef(Theme::color(QStringLiteral("bg-app")));
    DwmSetWindowAttribute(hwnd, kDwmBorderColor, &border, sizeof(border));
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange && m_titleBar) {
        m_titleBar->setMaximized(isMaximized());
    } else if (event->type() == QEvent::ThemeChange) {
        // 背景图的蒙层色、「新建连接」/ 搜索框的玻璃透明度、
        // 侧栏的 glass 属性都跟主题色挂钩，换配色后按新 token 重算一遍。
        reloadBackground();
    }
    QWidget::changeEvent(event);
}

void MainWindow::createTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_tray = new QSystemTrayIcon(windowIcon(), this);
    m_tray->setToolTip(QStringLiteral("EmShell"));

    // 左键单击 / 双击恢复窗口；右键弹原生菜单（见 showTrayMenu）。
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Context) {
            showTrayMenu();
        } else if (reason == QSystemTrayIcon::Trigger
                   || reason == QSystemTrayIcon::DoubleClick) {
            showFromTray();
        }
    });

    m_tray->show();
}

// 托盘右键菜单：直接弹 Win32 原生菜单。
//
// 为什么不用 QMenu：app.qss 给 QMenu / QMenu::item 写了整套自定义样式，
// 托盘菜单会被一起套上，和系统里其它托盘菜单完全不是一个样子。
// 这里 CreatePopupMenu + TrackPopupMenu(TPM_RETURNCMD)，字体、颜色、
// 深色/浅色、圆角全交给 Windows，程序这边只认返回的命令 ID。
void MainWindow::showTrayMenu()
{
    HMENU menu = ::CreatePopupMenu();
    if (!menu)
        return;

    constexpr UINT kShowCommand = 1;
    constexpr UINT kQuitCommand = 2;

    const std::wstring showText = QStringLiteral("显示主窗口").toStdWString();
    const std::wstring quitText = QStringLiteral("退出 EmShell").toStdWString();
    ::AppendMenuW(menu, MF_STRING, kShowCommand, showText.c_str());
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kQuitCommand, quitText.c_str());
    ::SetMenuDefaultItem(menu, kShowCommand, FALSE);

    POINT pt{};
    ::GetCursorPos(&pt);

    // 菜单 owner **不能用主窗口**。
    // 主窗口最小化在任务栏里时，SetForegroundWindow(主窗口) 会把它还原 ——
    // 表现就是「右键托盘图标，主窗口直接弹出来了」。
    // 这里用一个屏幕外的 1x1 工具窗口当 owner：它能被设成前台（保证点菜单
    // 外面时菜单会收起），但不会出现在任务栏 / Alt+Tab，也不碰主窗口状态。
    static QWidget *menuOwner = nullptr;
    if (!menuOwner) {
        menuOwner = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint);
        menuOwner->setAttribute(Qt::WA_ShowWithoutActivating);
        menuOwner->setAttribute(Qt::WA_TranslucentBackground);
    }
    menuOwner->setGeometry(-10000, -10000, 1, 1);
    menuOwner->show();
    const HWND ownerHwnd = reinterpret_cast<HWND>(menuOwner->winId());

    const HWND mainHwnd = reinterpret_cast<HWND>(winId());
    const HWND previous = ::GetForegroundWindow();

    ::SetForegroundWindow(ownerHwnd);
    const int cmd = ::TrackPopupMenu(menu,
                                     TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                     pt.x, pt.y, 0, ownerHwnd, nullptr);
    ::DestroyMenu(menu);
    ::PostMessage(ownerHwnd, WM_NULL, 0, 0);

    // 把前台还给原来的窗口。如果原来就是最小化的主窗口（或句柄已失效），
    // 就还给任务栏 —— 否则收菜单时系统可能把主窗口带出来。
    HWND restore = previous;
    if (!restore || !::IsWindow(restore)
        || (restore == mainHwnd && isMinimized())) {
        restore = ::FindWindowW(L"Shell_TrayWnd", nullptr);
    }
    if (restore)
        ::SetForegroundWindow(restore);
    menuOwner->hide();

    if (cmd == kShowCommand) {
        showFromTray();
    } else if (cmd == kQuitCommand) {
        m_quitRequested = true;
        if (m_tray)
            m_tray->hide();
        qApp->quit();
    }
}

void MainWindow::showFromTray()
{
    show();
    if (isMinimized())
        showNormal();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 有托盘时，点标题栏的 ✕（以及 Alt+F4）只是把窗口藏到托盘；
    // 真正退出只走托盘菜单里的「退出 EmShell」。
    if (m_tray && !m_quitRequested) {
        event->ignore();
        hide();

        if (!m_trayHintShown) {
            m_trayHintShown = true;
            if (m_tray->supportsMessages()) {
                m_tray->showMessage(
                    QStringLiteral("EmShell"),
                    QStringLiteral("程序已最小化到托盘，双击托盘图标可以重新打开。"),
                    QSystemTrayIcon::Information, 2500);
            }
        }
        return;
    }

    // 真正退出：把所有终端会话停干净，否则子进程会留成孤儿。
    // TerminalPage 在析构里做这件事；这里先藏窗口让它看起来是即时关闭的，
    // 再让主事件循环退出（setQuitOnLastWindowClosed(false) 时不会自动退出）。
    hide();
    event->accept();
    QWidget::closeEvent(event);
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

// ---------------------------------------------------------------------------
// 无边框窗口的原生行为
//
// Qt 的 FramelessWindowHint 只是把系统边框去掉了，拖动和缩放也一并没了。
// WM_NCHITTEST 是 Windows 用来问「这个点算什么区域」的消息，我们按位置回答
// 边框 / 标题栏 / 客户区，系统就会自动帮我们做完剩下的：拖动、双击最大化、
// 边缘缩放、贴边吸附、多显示器 DPI 处理。
// ---------------------------------------------------------------------------

namespace {

// SC_DRAGMOVE：让系统进入「按住鼠标移动窗口」的循环。SDK 里没有这个宏，
// 数值见 WinUser.h 的 SC_ 系列（0xF012）。
constexpr WPARAM kScDragMove = 0xF012;

// 最大化窗口被按住标题栏拖动时，DefWindowProc 会先把窗口还原、再跟着光标走 ——
// 但这套逻辑只对带 WS_CAPTION 的普通窗口生效。我们是 WS_POPUP 的无边框窗口，
// 结果就是「最大化时按标题栏完全拖不动」（还原成普通大小以后又正常，所以看起来
// 时好时坏）。这里在光标确实开始移动之后手动补上这一步：按光标在最大化窗口里的
// 横向比例还原、把标题栏贴到光标下面，再把后续拖动交还给系统（贴边吸附、
// 多显示器这些行为都不会丢）。
void beginMoveFromMaximized(HWND hwnd, const QPoint &cursor, int captionHeight)
{
    RECT maximizedRect{};
    ::GetWindowRect(hwnd, &maximizedRect);
    const int maximizedWidth = qMax(1, int(maximizedRect.right - maximizedRect.left));
    const qreal fx = qreal(cursor.x() - maximizedRect.left) / maximizedWidth;

    ::ShowWindow(hwnd, SW_RESTORE);

    RECT normalRect{};
    ::GetWindowRect(hwnd, &normalRect);
    const int normalWidth = qMax(1, int(normalRect.right - normalRect.left));

    int x = cursor.x() - qRound(fx * normalWidth);
    int y = cursor.y() - captionHeight / 2;

    // 从屏幕边缘按下去时，别把还原后的窗口甩到工作区外面
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    POINT point{};
    point.x = cursor.x();
    point.y = cursor.y();
    if (::GetMonitorInfoW(::MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &info)) {
        x = qBound(int(info.rcWork.left) - normalWidth + 160, x, int(info.rcWork.right) - 160);
        y = qBound(int(info.rcWork.top), y, int(info.rcWork.bottom) - captionHeight);
    }

    ::SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ::SendMessageW(hwnd, WM_SYSCOMMAND, kScDragMove, 0);
}

} // namespace

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    // 这整套只在无边框模式下需要。用原生窗口时系统自己会处理边框和标题栏，
    // 我们再插手反而会把拖动和缩放搞坏。
    if (kUseCustomTitleBar && eventType == QByteArrayLiteral("windows_generic_MSG")) {
        auto *msg = static_cast<MSG *>(message);

        // 最大化时按住标题栏拖动：WS_POPUP 的无边框窗口不会自己「先还原再移动」，
        // 这里手动补上（原因见 beginMoveFromMaximized 上面那段）。先捕获鼠标、
        // 记住按下的位置；光标真的动了才还原 —— 纯单击标题栏不应该把窗口还原掉。
        if (msg->message == WM_NCLBUTTONDOWN && int(msg->wParam) == HTCAPTION
            && ::IsZoomed(msg->hwnd)) {
            m_captionPressPending = true;
            m_captionPressPos = QPoint(int(short(LOWORD(msg->lParam))),
                                       int(short(HIWORD(msg->lParam))));
            ::SetCapture(msg->hwnd);
            *result = 0;
            return true;
        }

        if (m_captionPressPending) {
            if (msg->message == WM_MOUSEMOVE || msg->message == WM_NCMOUSEMOVE) {
                POINT cursor{};
                ::GetCursorPos(&cursor);
                // 用系统自己的拖拽阈值，手抖一两个像素不算拖动
                if (qAbs(int(cursor.x) - m_captionPressPos.x()) > ::GetSystemMetrics(SM_CXDRAG)
                    || qAbs(int(cursor.y) - m_captionPressPos.y())
                           > ::GetSystemMetrics(SM_CYDRAG)) {
                    m_captionPressPending = false;
                    ::ReleaseCapture();
                    beginMoveFromMaximized(
                        msg->hwnd, QPoint(int(cursor.x), int(cursor.y)),
                        qRound((m_titleBar ? m_titleBar->height() : 42) * devicePixelRatioF()));
                    *result = 0;
                    return true;
                }
            } else if (msg->message == WM_LBUTTONUP || msg->message == WM_NCLBUTTONUP) {
                // 单击松手：什么都不做，窗口保持最大化
                m_captionPressPending = false;
                ::ReleaseCapture();
                *result = 0;
                return true;
            } else if (msg->message == WM_CAPTURECHANGED) {
                m_captionPressPending = false;
            }
        }

        if (msg->message == WM_NCHITTEST) {
            // lParam 里是屏幕坐标，用 short 截断才能正确处理多显示器的负坐标。
            // **lParam 是物理像素**（DPI 感知进程），而 mapFromGlobal 要的是逻辑坐标：
            // 125% 缩放下不除以 dpr，算出来的 local 会整体偏大 —— 标题栏、四边、
            // 角落全部错位，表现就是窗口拖不动、右上角按钮点不着。
            const int screenX = int(short(LOWORD(msg->lParam)));
            const int screenY = int(short(HIWORD(msg->lParam)));
            const qreal dpr = devicePixelRatioF();
            const QPoint local = mapFromGlobal(QPoint(qRound(screenX / dpr),
                                                      qRound(screenY / dpr)));

            LRESULT hit = 0;

            // 1) 四条边和四个角 —— 最大化时不应该还能拖边框
            if (!isMaximized() && !isFullScreen()) {
                const bool left   = local.x() < kResizeBorder;
                const bool right  = local.x() > width() - kResizeBorder;
                const bool top    = local.y() < kResizeBorder;
                const bool bottom = local.y() > height() - kResizeBorder;

                if (top && left)          hit = HTTOPLEFT;
                else if (top && right)    hit = HTTOPRIGHT;
                else if (bottom && left)  hit = HTBOTTOMLEFT;
                else if (bottom && right) hit = HTBOTTOMRIGHT;
                else if (left)            hit = HTLEFT;
                else if (right)           hit = HTRIGHT;
                else if (top)             hit = HTTOP;
                else if (bottom)          hit = HTBOTTOM;
            }

            // 2) 标题栏区域：返回 HTCAPTION 让系统负责拖动和双击最大化。
            //    但如果鼠标正悬在搜索框或窗口按钮上，必须还给 Qt，
            //    不然这些控件点不动。
            if (hit == 0 && m_titleBar && local.y() < m_titleBar->height() && local.y() >= 0) {
                bool overInteractiveChild = false;
                for (QWidget *w = childAt(local); w && w != m_titleBar; w = w->parentWidget()) {
                    if (qobject_cast<QPushButton *>(w) || qobject_cast<QLineEdit *>(w)) {
                        overInteractiveChild = true;
                        break;
                    }
                }
                hit = overInteractiveChild ? HTCLIENT : HTCAPTION;
            }

            if (hit != 0) {
                *result = hit;
                return true;
            }
        }
    }

    return QWidget::nativeEvent(eventType, message, result);
}

} // namespace Em
