#include "ui/TerminalPage.h"

#include "app/Theme.h"
#include "model/ConnectionStore.h"
#include "model/KeyStore.h"
#include "terminal/LocalSession.h"
#include "terminal/TerminalView.h"
#include "ui/IconProvider.h"
#include "ui/SettingsPage.h"
#include "ui/Widgets.h"

#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>

#include <memory>

namespace Em {

namespace {

// Windows 命令行里带空格的路径要加引号
QString quoteArg(const QString &arg)
{
    if (!arg.contains(QLatin1Char(' ')) && !arg.contains(QLatin1Char('"')))
        return arg;
    QString escaped = arg;
    escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

// ---------------------------------------------------------- 标签栏 ---

// 标签宽度按这几段拼出来。左内边距、右内边距、margin-right 都要和 app.qss 里
// QTabBar::tab 的那几个值对上，改一边就得改另一边。
constexpr int kTabPaddingLeft = 14;     // 文字区距标签盒子左边（= QSS 的 padding-left）
constexpr int kTabPaddingRight = 20;    // = QSS 的 padding-right
constexpr int kTabMarginRight = 0;      // = QSS 的 margin-right（分隔线用 border-right）
constexpr int kTabMarkerSize = 8;       // 标签名前面的状态圆点（和分组菜单的点一样大）
constexpr int kTabMarkerGap = 8;        // 圆点到文字的间距
constexpr int kTabCloseSize = 18;       // 关闭按钮的命中区边长
constexpr int kTabCloseRightMargin = 8; // 关闭按钮距**画出来的**盒子右边
constexpr int kTabCloseGap = 8;         // 文字末尾到关闭按钮之间
constexpr int kTabIndicatorHeight = 2;  // 选中标签顶部蓝色条的高度

// 标签栏最右边给「+」留的一整块。标签栏的宽度会被压到「总宽 - 这一块」，
// 否则标签一多，滚动箭头就会跑到「+」下面去。
constexpr int kTabNewButtonSize = 28;
constexpr int kTabNewRightMargin = 10;
constexpr int kTabNewSlot = kTabNewButtonSize + kTabNewRightMargin;

// 标签栏只负责每个标签自己的前景。整条轨道由底下的 TabRail 画。
class TerminalTabBar : public QTabBar
{
public:
    explicit TerminalTabBar(QWidget *parent = nullptr) : QTabBar(parent) {}

protected:
    // 自己摆每个标签的关闭按钮 —— **不要**用 QTabBar::setTabButton()。
    //
    // 挂上去之后 Qt 会在文字区里额外扣一圈（扣得比控件本身还宽约 9px），那 9px 不论
    // 从哪边补都难看。自己摆就没这回事：宽度完全由 tabSizeHint() 说了算，按钮落在
    // 右内边距那块里，碰不到文字。
    void paintEvent(QPaintEvent *event) override
    {
        QTabBar::paintEvent(event);
        // 借这里把关闭按钮重新贴上去
        repinCloseButtons();
        polishScrollButtons();
        // 状态圆点和顶部蓝条都画在标签上面，所以放在 base paint 之后
        drawTabMarkers();
        drawActiveTopBar();
    }

public:
    // = QSS margin-right 之后真正画出来的那个盒子
    QRect paintedTabRect(int index) const
    {
        QRect r = tabRect(index);
        r.adjust(0, 0, -kTabMarginRight, 0);
        return r;
    }

    // 每个标签都给「名字完整显示」需要的宽度，一个字都不省、也不压缩。
    // 总宽超过标签栏时由 QTabBar 自己滚动（这是唯一能碰到后面那些标签的办法）。
    QSize tabSizeHint(int index) const override
    {
        return QSize(preferredWidth(index), QTabBar::tabSizeHint(index).height());
    }

    // 这里**不要**覆盖 minimumTabSizeHint()。
    //
    // 它决定「标签能被压到多窄」：Qt 默认给的是完整宽度，于是标签装不下时 Qt
    // 只能滚动，名字一个字都不会少。一旦把它改小（哪怕只是想控制标签栏的最小
    // 宽度），Qt 立刻就不再滚动、改成把所有标签一起压缩 —— 名字就被切了。
    // 代价是标签栏的最小宽度跟着标签数涨，但 QTabWidget 那边本来就允许滚动，
    // 实测窗口照样能缩窄。

private:
    // 标签名前面的状态圆点：绿 = 连接中/正常，红 = 已断开。
    // 大小和分组菜单里的色点一致（8px 圆）。
    // 连接状态存在页容器的 "connected" 属性上，由 TerminalPage 在会话
    // exited 时改掉；这里按页取，标签拖动后颜色不会串。
    void drawTabMarkers()
    {
        auto *tabs = qobject_cast<QTabWidget *>(parentWidget());
        if (!tabs)
            return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);

        for (int i = 0; i < count(); ++i) {
            const QRect r = paintedTabRect(i);
            if (r.right() < 0 || r.left() >= width())
                continue;

            QWidget *page = tabs->widget(i);
            const bool connected = page && page->property("connected").toBool();
            const QColor color = Theme::color(
                connected ? QStringLiteral("term-tab-ok") : QStringLiteral("term-tab-err"));

            const QRectF dot(r.left() + kTabPaddingLeft,
                             r.top() + (r.height() - kTabMarkerSize) / 2.0,
                             kTabMarkerSize, kTabMarkerSize);
            p.setBrush(color);
            p.drawEllipse(dot);
        }
    }

    // 选中标签顶部的 2px 蓝条，颜色和「+ 新建连接」按钮的底色一致。
    void drawActiveTopBar()
    {
        const int index = currentIndex();
        if (index < 0)
            return;

        const QRect r = paintedTabRect(index);
        if (r.width() <= 0 || r.right() < 0 || r.left() > width())
            return;

        QPainter p(this);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::color(QStringLiteral("nav-cta-bg")));
        p.drawRect(QRect(r.left(), r.top(), r.width(), kTabIndicatorHeight));
    }

    // 标签太多时 Qt 会放两个滚动箭头。Fusion 画的是调色板色的实心小三角，
    // 深色主题下几乎看不见；换成 Icons 里那个线条箭头，颜色跟界面一致。
    void polishScrollButtons()
    {
        if (m_scrollPolishDone)
            return;
        m_scrollPolishDone = true;

        const QColor color = Theme::color(QStringLiteral("nav-text"));
        for (QToolButton *b : findChildren<QToolButton *>()) {
            const bool left = b->objectName() == QLatin1String("ScrollLeftButton");
            const bool right = b->objectName() == QLatin1String("ScrollRightButton");
            if (!left && !right)
                continue;

            QPixmap arrow = Icons::pixmap(Icon::ChevronRight, color, 12);
            if (left)   // 没有 ChevronLeft，镜像一份
                arrow = arrow.transformed(QTransform::fromScale(-1, 1));

            b->setArrowType(Qt::NoArrow);
            b->setIcon(QIcon(arrow));
            b->setIconSize(QSize(12, 12));
        }
    }

    bool m_scrollPolishDone = false;

    // 名字完整显示需要的宽度。
    //
    // 文字在内容区里是**居中**排的。为了让文字刚好落在「状态圆点 + 8px」之后，
    // 内容区要比文字宽出 2*(圆点+间距)：多出来的左半边正好放圆点和间距，
    // 右半边则给关闭按钮留位。改这几个数时两边要一起算。
    int preferredWidth(int index) const
    {
        const int textWidth = fontMetrics().horizontalAdvance(tabText(index));
        return kTabPaddingLeft + textWidth + 2 * (kTabMarkerSize + kTabMarkerGap)
               + kTabPaddingRight + kTabMarginRight;
    }

    // 把每个关闭按钮摆到它那个标签的右内边距里。
    //
    // 按钮是标签栏的子控件（不是 setTabButton 挂的），靠 page 属性认领自己属于哪一页 ——
    // 页码用 QTabWidget::indexOf() 现查，标签拖动、增删之后不用维护。
    void repinCloseButtons()
    {
        auto *tabs = qobject_cast<QTabWidget *>(parentWidget());
        if (!tabs)
            return;

        for (QToolButton *btn : findChildren<QToolButton *>(QStringLiteral("TabCloseButton"))) {
            auto *page = btn->property("page").value<QWidget *>();
            const int index = page ? tabs->indexOf(page) : -1;
            if (index < 0) {
                btn->hide();
                continue;
            }

            const QRect r = paintedTabRect(index);
            if (r.right() < 0 || r.left() > width()) {   // 滚到可视范围外了
                btn->hide();
                continue;
            }

            // 位置**跟着文字末尾**算，而不是绑标签右边。
            //
            // 文字是 Qt 居中排的，居中用的那块宽度 = 标签宽 - 左内边距 - 右内边距，
            // 这两项都固定，所以文字实际画在哪能算得和 Qt 一模一样。算出文字的末尾，
            // ✕ 往右让开 kTabCloseGap 就是。绑右边的话，右边那点余量（右内边距减去
            // ✕ 那一块）会全变成文字和 ✕ 之间的空白。
            const int textW = fontMetrics().horizontalAdvance(tabText(index));
            const int areaW = r.width() - kTabPaddingLeft - kTabPaddingRight;
            const int textLeft = r.left() + kTabPaddingLeft + qMax(0, (areaW - textW) / 2);
            const int x = qMin(textLeft + textW + kTabCloseGap,
                               r.right() + 1 - kTabCloseRightMargin - kTabCloseSize);

            btn->setGeometry(x, r.top() + (r.height() - kTabCloseSize) / 2,
                             kTabCloseSize, kTabCloseSize);
            btn->show();
            btn->raise();
        }
    }
};

// 标签栏的底板：一条通栏轨道 + 底部一条发丝线。
//
// 为什么不直接画在标签栏自己身上：标签栏的宽度被限制在「总宽 - 38px」，
// 最右边那一块要留给「+」。这里单独做一个铺满整行宽的 Widget，垫在标签栏
// 和「+」底下；它们都是透明的，轨道从底下透上来，最右端就不会缺一块。
//
// 轨道比画布暗一档（浅色深色都是），保证「看得出来有标签栏」；选中的标签
// 不铺底色，而是把轨道和底部线整块挖掉，露出下面的画布 —— 和终端内容连成
// 一体，正是参考页里「active tab 和 content 同色」的效果。
class TabRail : public QWidget
{
public:
    explicit TabRail(QWidget *parent = nullptr) : QWidget(parent)
    {
        // 它只是底板，不接鼠标事件，否则会把标签栏的点击挡掉
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (width() <= 0 || height() <= 0)
            return;

        QPainter p(this);
        p.setPen(Qt::NoPen);
        p.fillRect(rect(), Theme::color(QStringLiteral("term-rail-bg")));
        // 标签栏底部那条 1px 边框（#0B0D12）。选中标签铺的是不透明的
        // #282C34，会把自己底下这一段盖住，看起来和终端连成一体。
        p.fillRect(QRect(0, height() - 1, width(), 1),
                   Theme::color(QStringLiteral("term-rail-line")));
    }
};

// 标签上的关闭按钮。
//
// Qt 自带那个是 Fusion 用样式画出来的灰方块，跟整套界面完全不搭，而且
// **QSS 改不了它的图标颜色**（它不是图片，是样式画的）。所以自己放一个：
// 图标走 Icons::pixmap()，颜色从 Theme 取，正常灰、悬停红 —— 和侧边栏、
// 连接行那里用的是同一个路子（颜色烘进 QIcon，QSS 只管背景）。
class TabCloseButton : public QToolButton
{
public:
    explicit TabCloseButton(QWidget *parent = nullptr) : QToolButton(parent)
    {
        setObjectName(QStringLiteral("TabCloseButton"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAutoRaise(true);
        // 不给 tooltip：一个 ✕ 上飘个「关闭」的气泡纯属碍事，鼠标扫过时很吵
        setFixedSize(kTabCloseSize, kTabCloseSize);

        const int iconSize = 11;
        setIconSize(QSize(iconSize, iconSize));

        QIcon closeIcon;
        closeIcon.addPixmap(Icons::pixmap(Icon::Close,
                                          Theme::color(QStringLiteral("nav-text")), iconSize),
                            QIcon::Normal, QIcon::Off);
        // 悬停时整块底色变红（QSS），✕ 跟着变白
        closeIcon.addPixmap(Icons::pixmap(Icon::Close, Qt::white, iconSize),
                            QIcon::Active, QIcon::Off);
        setIcon(closeIcon);
    }
};

// 标签栏最右边的「+」。和关闭按钮同一套做法：图标颜色烘进 QIcon，
// 背景交给 QSS。尺寸比关闭按钮大一档，因为它是「新建」这种主动作。
class TabNewButton : public QToolButton
{
public:
    explicit TabNewButton(QWidget *parent = nullptr) : QToolButton(parent)
    {
        setObjectName(QStringLiteral("TabNewButton"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAutoRaise(true);
        setFixedSize(kTabNewButtonSize, kTabNewButtonSize);
        setToolTip(QStringLiteral("新建本地终端"));

        const int iconSize = 14;
        setIconSize(QSize(iconSize, iconSize));

        QIcon plusIcon;
        plusIcon.addPixmap(Icons::pixmap(Icon::Plus,
                                         Theme::color(QStringLiteral("nav-text")), iconSize),
                           QIcon::Normal, QIcon::Off);
        plusIcon.addPixmap(Icons::pixmap(Icon::Plus,
                                         Theme::color(QStringLiteral("nav-text-hover")), iconSize),
                           QIcon::Active, QIcon::Off);
        setIcon(plusIcon);
    }
};

// 用自己那套标签栏。setTabBar() 是 protected，只能在自己类里换，
// 所以这里绕一层。
class TerminalTabWidget : public QTabWidget
{
public:
    explicit TerminalTabWidget(QWidget *parent = nullptr) : QTabWidget(parent)
    {
        auto *bar = new TerminalTabBar(this);
        setTabBar(bar);

        // 底板铺满整行，垫在标签栏和「+」下面。
        m_rail = new TabRail(this);
        bar->installEventFilter(this);

        // **不要用 setCornerWidget() 给「+」占位。** 标签装不下的时候 Qt 会让
        // 标签栏吃满整个宽度，corner widget 被挤到控件外面去（实测 x=宽度+1），
        // 「+」和右边那个滚动箭头就叠在一起了。而且 Windows 上 corner 的顶边
        // 还会多画一条 2px 的 #CCCCCC 原生线，QSS 和控件样式都盖不掉。
        // 现在改成给标签栏限宽，把最右边 kTabNewSlot 永久留给「+」；
        // 「+」是 TerminalTabWidget 的直接子控件，位置由 syncRail() 摆。
    }

    // 「+」由 TerminalPage 创建（它要知道点下去开什么），这里只负责摆位置。
    void setNewTabButton(QWidget *button)
    {
        m_newTabButton = button;
        syncRail();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QTabWidget::resizeEvent(event);
        syncRail();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // 标签栏的高度由 QSS polish 之后才定下来，resizeEvent 之外也会变
        if (watched == tabBar()
            && (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest)) {
            syncRail();
        }
        return QTabWidget::eventFilter(watched, event);
    }

private:
    void syncRail()
    {
        if (!m_rail || !tabBar())
            return;

        tabBar()->setMaximumWidth(qMax(0, width() - kTabNewSlot));

        const QRect g(0, 0, width(), tabBar()->height());
        if (m_rail->geometry() != g)
            m_rail->setGeometry(g);
        m_rail->update();

        // 叠放顺序：标签栏在轨道上面（文字和关闭按钮不能被轨道盖住），
        // 「+」再抬到最上面。
        tabBar()->raise();
        if (m_newTabButton) {
            m_newTabButton->setGeometry(width() - kTabNewSlot,
                                        (tabBar()->height() - kTabNewButtonSize) / 2,
                                        kTabNewButtonSize, kTabNewButtonSize);
            m_newTabButton->raise();
        }
    }

    TabRail *m_rail = nullptr;
    QWidget *m_newTabButton = nullptr;
};

} // namespace

TerminalPage::TerminalPage(ConnectionStore *store, KeyStore *keys, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_keys(keys)
{
    m_tabBar = new TerminalTabWidget(this);
    // **这里必须是 false。** 关闭按钮是我们自己放的（见 createTab 里的 TabCloseButton），
    // 但 Qt 只要看到 tabsClosable 是 true，就还会在**文字区**里再给「它以为存在的」那个
    // 关闭按钮留一份宽度（实测约 14px）—— 于是标签名最后那个字被裁掉一截，而且因为
    // Qt 是在文字区里居中排的，看着像「字的底少了一块」（「本地 2」的 2 只剩一小条）。
    // 关掉它，Qt 就不再保留，文字区正好等于 tabSizeHint() 算出来的宽度。
    // 代价：中键点标签关闭不再生效（我们自己的 ✕ 按钮照常工作）。
    m_tabBar->setTabsClosable(false);
    m_tabBar->setMovable(true);
    m_tabBar->setDocumentMode(true);

    // 标签名一个字都不省略，永远完整显示（宽度由 TerminalTabBar::tabSizeHint 按
    // 名字算）。标签多到装不下时由标签栏滚动 —— 滚动时边缘会露出半个标签，那是
    // 「滚到这儿了」，不是名字被截断。
    m_tabBar->setElideMode(Qt::ElideNone);
    m_tabBar->tabBar()->setExpanding(false);

    // 标签栏右边加一个「新建」小按钮。它挂在 TerminalTabWidget 自己身上，
    // 位置和叠放顺序由那边算（见 syncRail）。
    auto *newLocal = new TabNewButton(m_tabBar);
    connect(newLocal, &QToolButton::clicked, this, [this] { openLocalSession(); });
    static_cast<TerminalTabWidget *>(m_tabBar)->setNewTabButton(newLocal);

    connect(m_tabBar, &QTabWidget::tabCloseRequested, this, &TerminalPage::closeTab);
    connect(m_tabBar, &QTabWidget::currentChanged, this, [this](int) {
        if (Tab *tab = currentTab())
            tab->view->setFocus();
    });

    // 一个标签都没有的时候显示空状态，而不是一个空标签栏。
    // 用整页切换而不是让两个控件互相 setVisible —— 那样得两处同步开关，
    // 漏一处就会出现「空状态和标签栏同时挂着」。
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_emptyPage = buildEmptyState());
    m_stack->addWidget(m_tabBar);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(m_stack);
}

// 一个标签都没有时显示的那张页。两个入口：开一个本地终端、去「所有连接」页。
// 新建连接不在这儿 —— 侧栏顶上那个按钮是常驻的，这里再来一个只是重复。
QWidget *TerminalPage::buildEmptyState()
{
    auto *page = new QWidget;

    auto *title = makeLabel(QStringLiteral("未连接任何终端"), "empty", page);
    title->setAlignment(Qt::AlignCenter);

    auto *subtitle = makeLabel(QStringLiteral("请选择您的操作："), "hint", page);
    subtitle->setAlignment(Qt::AlignCenter);

    // 三个 ActionTile。容器用透明 QWidget 而不是 Card —— tile 自己就是
    // 白底卡片，外面再套一层 Card 会变成双层边框。
    auto *row = new QWidget(page);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(12);

    auto *tileLocal = new ActionTile(Icon::Terminal, QStringLiteral("打开本机终端"),
                                     QStringLiteral("启动一个本地 shell 会话"), row);
    auto *tileAll = new ActionTile(Icon::Server, QStringLiteral("查看所有连接"),
                                   QStringLiteral("浏览、编辑已保存的服务器"), row);

    // 宽度必须统一给死：tile 的最小宽度是按文字算的，两张会参差不齐
    for (ActionTile *tile : { tileLocal, tileAll }) {
        tile->setFixedWidth(200);
        tile->setFixedHeight(112);
        rowLayout->addWidget(tile);
    }

    connect(tileLocal, &ActionTile::clicked, this, [this] {
        openLocalSession(SettingsPage::preferredShell());
    });
    connect(tileAll, &ActionTile::clicked, this, &TerminalPage::browseConnectionsRequested);

    // 整行左右各一条 stretch，把它顶在水平中央
    auto *centered = new QWidget(page);
    auto *centerLayout = new QHBoxLayout(centered);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->addStretch(1);
    centerLayout->addWidget(row);
    centerLayout->addStretch(1);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(6);
    layout->addStretch(1);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(18);
    layout->addWidget(centered);
    layout->addStretch(1);

    return page;
}

int TerminalPage::tabBarHeight() const
{
    if (!m_tabBar)
        return 0;
    const int h = m_tabBar->tabBar()->height();
    return h > 0 ? h : m_tabBar->tabBar()->sizeHint().height();
}

void TerminalPage::updateEmptyState()
{
    if (!m_stack)
        return;
    m_stack->setCurrentWidget(m_tabs.isEmpty() ? m_emptyPage
                                               : static_cast<QWidget *>(m_tabBar));
    emit tabsChanged();
}

TerminalPage::Tab *TerminalPage::tabAt(int index) const
{
    for (Tab *tab : m_tabs) {
        if (tab->index == index)
            return tab;
    }
    return nullptr;
}

TerminalPage::Tab *TerminalPage::currentTab() const
{
    return tabAt(m_tabBar->currentIndex());
}

TerminalPage::Tab *TerminalPage::createTab(const QString &title)
{
    auto *tab = new Tab;
    tab->title = title;

    tab->container = new QWidget;
    // 状态圆点靠这个属性取色：新开的会话先按绿色算，连接失败 / 断开时
    // 由 attachSession() 里的 exited 回调改成 false。
    tab->container->setProperty("connected", true);
    tab->view = new TerminalView(tab->container);

    auto *terminalWrap = new QWidget(tab->container);
    terminalWrap->setObjectName(QStringLiteral("TerminalWrap"));
    auto *wrapLayout = new QVBoxLayout(terminalWrap);
    // 原来留 1px 是为了不压住圆角边框；现在没有边框了，直接贴边
    wrapLayout->setContentsMargins(0, 0, 0, 0);
    wrapLayout->addWidget(tab->view);

    auto *layout = new QVBoxLayout(tab->container);
    // 左边、上边贴边；右、下各留 8px 呼吸。终端背景是透明的，这点留白
    // 不会露出一块不同的颜色，只是让文字别顶着窗口边缘。
    layout->setContentsMargins(0, 0, 8, 8);
    layout->setSpacing(0);
    layout->addWidget(terminalWrap);

    tab->index = m_tabBar->addTab(tab->container, title);
    // 标签上的字就是这个名字，之后只有 tooltip 会被会话报的标题更新
    m_tabBar->setTabToolTip(tab->index, title);
    m_tabs.append(tab);

    // 关闭按钮：自己画的（理由见 TabCloseButton），**直接挂在标签栏上、位置自己摆**
    // （理由见 TerminalTabBar::repinCloseButtons），不走 setTabButton()。
    // page 属性告诉它自己属于哪一页，摆位置和响应点击时都靠它现查索引。
    auto *closeButton = new TabCloseButton(m_tabBar->tabBar());
    closeButton->setProperty("page", QVariant::fromValue<QWidget *>(tab->container));

    const QPointer<QWidget> container(tab->container);
    connect(closeButton, &QToolButton::clicked, this, [this, container] {
        if (!container)
            return;
        const int index = m_tabBar->indexOf(container);   // 点的时候再取，标签会挪位
        if (index >= 0)
            closeTab(index);
    });

    updateEmptyState();   // 有标签了，把空状态换成标签栏

    connect(tab->view, &TerminalView::titleChanged, this, [this, tab](const QString &t) {
        applyTitle(tab, t);
    });
    connect(tab->view, &TerminalView::sessionFinished, this, [this, tab](int code) {
        emit statusMessage(QStringLiteral("%1：会话结束（退出码 %2）").arg(tab->title).arg(code));
    });

    return tab;
}

// 把会话接到标签上，并挂好「断开后把状态圆点变红」的回调。
//
// 圆点刚建标签时按绿色算（正在连接 / 正常）；只有会话真的 exited 了才变红。
// 用 QPointer 守住容器：用户可能已经关掉标签，而 SSH 工作线程要过一会儿
// 才把 exited 发出来，不能直接碰已经 deleteLater 的控件。
void TerminalPage::attachSession(Tab *tab, TerminalSession *session)
{
    tab->session = session;
    tab->view->setSession(session);

    tab->container->setProperty("connected", true);

    QPointer<QWidget> container(tab->container);
    connect(session, &TerminalSession::exited, this, [this, container](int) {
        if (!container)
            return;
        container->setProperty("connected", false);
        m_tabBar->tabBar()->update();
    });
}

// 会话报了窗口标题（OSC 0/2）。**只更新 tooltip，不动标签上的字。**
//
// 原来这里是拿标题覆盖标签名的，但那个标题基本没法用：
//
//  - ConPTY 一启动就会把子进程的控制台标题报上来，而那就是可执行文件的完整
//    路径（`C:\WINDOWS\...\powershell.exe`）。于是「本地 1」「雨云 - 香港 -
//    密钥」这些名字当场被冲掉，标签上只剩一个 exe 文件名，看不出连的是哪台机器。
//  - 远端 shell 也会报标题（Ubuntu 的 .bashrc 默认就设成「user@host: 路径」），
//    同样会把连接名顶掉。
//
// 这个程序里标签的意义是「这是哪条连接」，用户起的名字比任何程序报上来的标题
// 都更该信，所以标签名在建标签时就定下来，之后不再改。标题想看的话悬停就有。
void TerminalPage::applyTitle(Tab *tab, const QString &title)
{
    if (title.trimmed().isEmpty())
        return;

    m_tabBar->setTabToolTip(tab->index, title);
}

void TerminalPage::openLocalSession(const QString &shell)
{
    auto *session = new LocalSession(shell, this);

    QString error;
    if (!session->start(80, 24, &error)) {
        delete session;
        emit statusMessage(QStringLiteral("打开本地终端失败：%1").arg(error));
        return;
    }

    ++m_untitledCount;
    Tab *tab = createTab(QStringLiteral("本地 %1").arg(m_untitledCount));
    attachSession(tab, session);
    // 进程可能在 start() 之后、挂信号之前就已经退了（命令立刻报错）
    if (!session->isRunning())
        tab->container->setProperty("connected", false);

    m_tabBar->setCurrentIndex(tab->index);
    tab->view->setFocus();

    // 标签页刚建出来尺寸还没定，等一轮事件循环再按真实大小调整伪控制台
    QPointer<TerminalView> viewGuard(tab->view);
    QPointer<TerminalSession> sessionGuard(session);
    QMetaObject::invokeMethod(this, [viewGuard, sessionGuard] {
        if (viewGuard && sessionGuard)
            sessionGuard->resize(viewGuard->screen()->cols(), viewGuard->screen()->rows());
    }, Qt::QueuedConnection);
}

QString TerminalPage::buildSshCommand(const Connection &c, const KeyStore *keys)
{
    QStringList parts;
    parts << QStringLiteral("ssh");

    if (c.port > 0 && c.port != 22)
        parts << QStringLiteral("-p") << QString::number(c.port);

    if (c.auth == AuthType::PublicKey && !c.keyId.isEmpty() && keys) {
        if (const SshKey *key = keys->find(c.keyId))
            parts << QStringLiteral("-i") << quoteArg(key->privateKeyPath);
    }

    const QString target = c.username.isEmpty()
                               ? c.host
                               : c.username + QLatin1Char('@') + c.host;
    parts << quoteArg(target);

    return parts.join(QLatin1Char(' '));
}

// 从一条连接记录拼出原生 SSH 的参数。
// 返回 false 表示凭据不够（比如没存密码），调用方应该退回命令行方式。
bool TerminalPage::buildSshOptions(const Connection &connection, SshSession::Options *options) const
{
    options->host = connection.host;
    options->port = connection.port > 0 ? connection.port : 22;
    options->username = connection.username;

    if (connection.auth == AuthType::PublicKey && !connection.keyId.isEmpty() && m_keys) {
        const SshKey *key = m_keys->find(connection.keyId);
        if (!key)
            return false;

        // 内置 libssh2 用不了的密钥类型（ed25519 / ecdsa 之类）不走原生，
        // 让它落到下面的系统 ssh 分支去 —— 那边什么密钥都支持。
        if (!SshSession::supportsKeyType(key->type))
            return false;

        options->privateKeyPath = key->privateKeyPath;
        // 同名 .pub 存在的话一起给 libssh2，省得它自己去猜路径
        const QString pubPath = key->privateKeyPath + QStringLiteral(".pub");
        if (QFileInfo::exists(pubPath))
            options->publicKeyPath = pubPath;
        options->passphrase = key->passphrase;
        return true;
    }

    if (connection.auth == AuthType::Password && !connection.password.isEmpty()) {
        options->password = connection.password;
        return true;
    }

    return false;   // 没有凭据
}

void TerminalPage::openConnection(const Connection &connection)
{
    SshSession::Options options;
    if (buildSshOptions(connection, &options)) {
        openNativeSsh(connection, options);
        return;
    }

    // 走到这儿有两种情况：没凭据，或者密钥类型内置库用不了。
    // 后者要说明一句 —— 不然用户看到终端里冒出个 ssh，会以为自己点错了。
    if (connection.auth == AuthType::PublicKey && m_keys) {
        if (const SshKey *key = m_keys->find(connection.keyId); key && !SshSession::supportsKeyType(key->type)) {
            emit statusMessage(QStringLiteral(
                "「%1」是 %2 密钥，内置 SSH 只支持 RSA，改用系统 ssh 客户端连接")
                .arg(key->name, key->type));
        }
    }

    // 没存密码或没指定密钥：退回系统 ssh 客户端，让用户在终端里输。
    // 这不是降级方案，只是「没有凭据时」的正常路径。
    openConnectionViaCli(connection);
}

void TerminalPage::openNativeSsh(const Connection &connection, const SshSession::Options &options)
{
    auto *session = new SshSession(options, this);

    Tab *tab = createTab(connection.name.isEmpty() ? connection.host : connection.name);
    attachSession(tab, session);

    // 握手 / 认证阶段终端里一个字都没有，先给一行提示。
    // 远端一旦开始输出，这条就自然地留在上方。
    // 当场写（defer=false）：原生 SSH 在 started 之前不会有任何远端输出，这样
    // 「正在连接 → 连接成功 → 远端输出」的先后顺序才稳。
    // 上面不留空行（blankLineBefore=false）：此时终端还是空的，光标就在行首。
    tab->view->showNotice(QStringLiteral("正在连接主机 %1").arg(connection.identity()),
                          false, false);

    // 登录并拿到 shell 之后（started 是从工作线程发的，这里是队列连接，
    // 一定排在该会话的 dataReceived 之前），在「正在连接」下面补一行成功的提示。
    // blankLineBefore=false：紧接在「正在连接」下面一行，中间不留空行。
    //
    // started 也决定「连接失败」要不要先空一行：握手 / 认证阶段失败时终端里
    // 只有上面那行「正在连接」，提示应该紧贴着它；连上以后的读写错误则插在
    // 远端输出中间，光标可能停在提示符后面，才需要先换行。
    auto started = std::make_shared<bool>(false);
    connect(session, &TerminalSession::started, tab->view,
            [session, view = tab->view, started] {
        *started = true;
        view->showNotice(QStringLiteral("连接主机成功"), false, false);

        // 连接建立成功之后再挂断开提示：握手 / 认证阶段的失败已经有
        // 「连接失败」的消息，不该在终端里再补一句「连接断开」。
        // 这里不等到 stateChanged(Closed)：读写出错时状态停在 Failed，
        // 但连接确实已经断了，用 exited 才盖得住两种情况。
        connect(session, &TerminalSession::exited, view, [view](int) {
            view->showNotice(QStringLiteral("连接断开"), false);
        });
    });

    m_tabBar->setCurrentIndex(tab->index);
    tab->view->setFocus();

    // 主机密钥要用户点头，这一步在工作线程里没法弹窗，由这里接住
    connect(session, &SshSession::hostKeyUnknown, this,
            [this, session](const QString &fingerprint, const QString &keyType) {
                promptForHostKey(session, fingerprint, keyType);
            });

    QPointer<TerminalView> viewGuard(tab->view);
    connect(session, &SshSession::stateChanged, this,
            [this, session, viewGuard, started](SshSession::State state,
                                                 const QString &detail) {
                if (state == SshSession::State::Ready) {
                    emit statusMessage(QStringLiteral("已连接到 %1").arg(session->description()));
                } else if (state == SshSession::State::Failed) {
                    // 底部状态条已经去掉了，错误必须写进终端，否则用户只看到
                    // 一行「正在连接主机 …」就再也没有下文。
                    // 握手 / 认证阶段失败紧贴着「正在连接」写，中间别空一行；
                    // 已经连上之后的读写错误才需要和远端输出隔开（见 started）。
                    if (viewGuard)
                        viewGuard->showNotice(QStringLiteral("连接失败：%1")
                                                  .arg(session->lastError()),
                                              false, *started);
                    emit statusMessage(QStringLiteral("连接失败：%1")
                                           .arg(session->lastError()), true);
                } else if (!detail.isEmpty()) {
                    emit statusMessage(detail);
                }
            });

    QString error;
    if (!session->start(tab->view->screen()->cols(), tab->view->screen()->rows(), &error)) {
        emit statusMessage(QStringLiteral("连接 %1 失败：%2").arg(connection.host, error), true);
        closeTab(tab->index);
        return;
    }

    emit statusMessage(QStringLiteral("正在连接 %1 …").arg(connection.identity()));
}

void TerminalPage::promptForHostKey(SshSession *session, const QString &fingerprint,
                                    const QString &keyType)
{
    QPointer<SshSession> guard(session);

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("确认主机密钥"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("第一次连接这台服务器，请核对它的指纹。"));
    box.setInformativeText(
        QStringLiteral("主机：%1\n"
                       "类型：%2\n"
                       "指纹：%3\n\n"
                       "如果这个指纹和你预期的对不上（可以问服务器管理员，"
                       "或者先在命令行里用 ssh 连一次核对），应该选择「取消」。")
            .arg(session ? session->description() : QString(), keyType, fingerprint));

    QPushButton *accept = box.addButton(QStringLiteral("信任并继续"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(accept);
    box.exec();

    const bool accepted = (box.clickedButton() == accept);
    if (guard)
        guard->answerHostKeyPrompt(accepted);

    if (accepted)
        emit statusMessage(QStringLiteral("已记住 %1 的主机密钥").arg(keyType));
}

void TerminalPage::openConnectionViaCli(const Connection &connection)
{
    const QString command = buildSshCommand(connection, m_keys);

    auto *session = new LocalSession(command, this);
    session->setProperty("connectionId", connection.id);

    QString error;
    if (!session->start(80, 24, &error)) {
        delete session;
        emit statusMessage(QStringLiteral("连接 %1 失败：%2").arg(connection.host, error), true);
        return;
    }

    Tab *tab = createTab(connection.name.isEmpty() ? connection.host : connection.name);
    attachSession(tab, session);
    if (!session->isRunning())
        tab->container->setProperty("connected", false);

    // **这条路径不写「正在连接主机 …」。** 系统 ssh 客户端一启动就先发一串
    // 隐藏光标 + 清屏 + 光标归位的控制序列，写在前面的提示会被整屏清掉；
    // 它随后自己会输出密码提示或连接错误，终端不会空着。

    m_tabBar->setCurrentIndex(tab->index);
    tab->view->setFocus();

    emit statusMessage(QStringLiteral("正在连接 %1 …（这条连接没有保存密码，请在终端里输入）")
                           .arg(connection.identity()));

    QPointer<TerminalView> viewGuard(tab->view);
    QPointer<TerminalSession> sessionGuard(session);
    QMetaObject::invokeMethod(this, [viewGuard, sessionGuard] {
        if (viewGuard && sessionGuard)
            sessionGuard->resize(viewGuard->screen()->cols(), viewGuard->screen()->rows());
    }, Qt::QueuedConnection);
}

void TerminalPage::closeTab(int index)
{
    Tab *tab = tabAt(index);
    if (!tab)
        return;

    m_tabs.removeOne(tab);

    // 先把会话停干净，再拆界面，否则读者线程可能还在往已析构的控件发信号
    if (tab->session) {
        TerminalSession *session = tab->session;
        tab->view->setSession(nullptr);
        // 关掉的标签不该再收到这个会话的状态 / 失败提示
        session->disconnect(this);

        // SSH 的工作线程可能正卡在阻塞的握手 / 认证里（服务器接了 TCP 却不
        // 回包时要等到 api_timeout）。在这里 join 会把界面线程一起堵住 ——
        // 表现是点标签的 ✕ 之后窗口「未响应」。交给它自己收工后再删。
        // 本地终端的 shutdown 是同步的，而且关掉伪控制台后读者线程立刻就退了。
        if (auto *ssh = qobject_cast<SshSession *>(session))
            ssh->shutdownAsync();
        else {
            session->shutdown();
            session->deleteLater();
        }
    }

    m_tabBar->removeTab(index);
    tab->container->deleteLater();
    delete tab;

    // 删掉一个之后，后面标签的 index 都要往前挪
    for (int i = 0; i < m_tabs.size(); ++i)
        m_tabs[i]->index = m_tabBar->indexOf(m_tabs[i]->container);

    // 关到最后一个就退回空状态。必须放在重排 index 之后 —— 这里是
    // 「标签数变成 0」的唯一出口，放前面的话最后那一下还会停在标签栏上。
    updateEmptyState();
}

} // namespace Em
