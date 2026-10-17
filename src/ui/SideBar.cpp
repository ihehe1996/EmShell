#include "ui/SideBar.h"

#include "app/Theme.h"
#include "model/ConnectionStore.h"
#include "ui/GroupDialog.h"
#include "ui/IconProvider.h"
#include "ui/Widgets.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace Em {

namespace {

// 侧栏顶上那个「+ 新建连接」。
//
// 没用 QPushButton::setIcon()：图标和文字之间的距离在 Qt 样式内部写死，QSS 调不动，
// 而这个按钮的图标要跟文字贴得紧一点。所以和 NavButton
// 一个路子 —— 图标、文字各放一个 QLabel，间距交给布局。
//
// 颜色全部走 token：#SideBarCta 的底色和 QLabel[role="ctaText"] 的字色在 app.qss 里，
// 图标是 pixmap、QSS 管不到，构造时用 nav-cta-text 烘进去（和文字同色）。
class SideBarCta : public QPushButton
{
public:
    explicit SideBarCta(const QString &text, QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setObjectName(QStringLiteral("SideBarCta"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);

        auto *iconLabel = new QLabel(this);
        iconLabel->setFixedSize(13, 13);
        iconLabel->setPixmap(Icons::pixmap(Icon::Plus,
                                           Theme::color(QStringLiteral("nav-cta-text")), 13));
        // 三个孩子都要让鼠标事件穿过去，不然压在图标/文字上时按钮收不到点击
        iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto *textLabel = makeLabel(text, "ctaText", this);
        textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

        // 两侧各一个等权重的 stretch，图标 + 文字作为**一整组**居中。
        // 只留右边那个 stretch 的话内容是左对齐的；改成给文字标签
        // AlignCenter 也不行 —— 那样居中的是文字，图标撇在最左边，
        // 两者分开算，中间的 6px 间距就白留了。
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 0, 12, 0);
        layout->setSpacing(0);
        layout->addStretch();
        layout->addWidget(iconLabel);
        layout->addSpacing(4);
        layout->addWidget(textLabel);
        layout->addStretch();
    }
};

// 分组标题。不做 toUpper、不加 letter-spacing：这两样是给拉丁字母准备的，
// 套在中文上只会让字距变怪。
QLabel *groupLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("NavGroupLabel"));
    label->setFixedHeight(20);
    return label;
}

// 「分组」区块的标题行：标题靠左、+ 靠右。
//
// 右边距 7 是量出来的，不是随手写的：NavButton 的右内边距是 10，数字药丸的
// 右边缘因此落在距内容区右边 10px 处；而 + 是个 20x20 的按钮、图标 14 居中，
// 图标右缘比按钮右缘再缩进 3px。所以按钮的右边距要减掉这 3，两条竖线才对齐。
// 改了按钮尺寸或图标尺寸，这个 7 就要跟着重算。
QWidget *groupHeaderRow(const QString &text, QWidget *parent, QPushButton **outAddButton)
{
    auto *row = new QWidget(parent);

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 7, 0);
    layout->setSpacing(0);
    layout->addWidget(groupLabel(text, row));   // 左边距在 QSS 的 padding-left: 11
    layout->addStretch();

    auto *add = new QPushButton(row);
    add->setObjectName(QStringLiteral("NavGroupAdd"));
    add->setFixedSize(20, 20);
    add->setCursor(Qt::PointingHandCursor);
    add->setFocusPolicy(Qt::NoFocus);
    add->setToolTip(QStringLiteral("新建分组"));

    // 图标颜色是烘进 pixmap 的，QSS 管不到；悬停变亮只能预先烘一版
    // Active 状态的（QPushButton 在鼠标悬停时就是用 Active 那版画的）。
    QIcon plus;
    plus.addPixmap(Icons::pixmap(Icon::Plus, Theme::color(QStringLiteral("nav-group")), 14),
                   QIcon::Normal, QIcon::Off);
    plus.addPixmap(Icons::pixmap(Icon::Plus, Theme::color(QStringLiteral("nav-text-hover")), 14),
                   QIcon::Active, QIcon::Off);
    add->setIcon(plus);
    add->setIconSize(QSize(14, 14));

    layout->addWidget(add);
    *outAddButton = add;
    return row;
}

} // namespace

SideBar::SideBar(ConnectionStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setObjectName(QStringLiteral("SideBar"));
    setFixedWidth(228);

    // **必须显式打开**，否则 QSS 里 #SideBar 那条 background 和 border-right
    // 一个都不会画 —— 纯 QWidget 默认不绘制样式表背景，它就一直是透明的，
    // 露出来的是底下 #Root 的页面底色。
    // 后果很有迷惑性：侧栏看起来「有颜色」，其实是页面底色透上来的，
    // 所以怎么调它的配色都看不出变化。
    setAttribute(Qt::WA_StyledBackground, true);

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("SideBarScroll"));

    // 上边距要比左右大：顶上挨着标题栏，只留 10 的话第一项会贴着标题栏，
    // 看着像被压住了
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 20, 10, 10);
    layout->setSpacing(3);

    auto addNav = [this, layout, content](AppPage page, Icon icon, const QString &text) {
        auto *button = new NavButton(icon, text, content);
        layout->addWidget(button);
        m_buttons.insert(int(page), button);
        connect(button, &QPushButton::clicked, this, [this, page] {
            emit pageRequested(page);
        });
        return button;
    };

    // 分组前先拉开一段空白，视觉上把组和组分开；标题下面只留一点点，让它
    // 明确贴着下面那几条。
    auto addGroup = [layout, content](const QString &text) {
        layout->addSpacing(18);
        layout->addWidget(groupLabel(text, content));
        layout->addSpacing(6);
    };

    // 最上面是「+ 新建连接」。原来这儿是「控制台」总览页，那一页整个删掉了。
    // 它不归任何一组，也不切页 —— 点了就地弹新建连接对话框（信号交给 MainWindow）。
    auto *cta = new SideBarCta(QStringLiteral("新建连接"), content);
    m_cta = cta;
    layout->addWidget(cta);
    connect(cta, &QPushButton::clicked, this, &SideBar::newConnectionRequested);

    addGroup(QStringLiteral("工作区"));
    addNav(AppPage::Connections, Icon::Grid,        QStringLiteral("所有连接"));
    addNav(AppPage::History,     Icon::Check,       QStringLiteral("最近使用"));
    // 收藏夹用**空心**星（实心的 Star 留给连接行上那个收藏开关，
    // 那边「亮起来 = 已收藏」要靠填充来区分）
    addNav(AppPage::Favorites,   Icon::StarOutline, QStringLiteral("收藏夹"));

    // ---- 分组区块 ----
    // 分组没有独立的管理页了，直接列在这里：+ 新建，右键编辑/上移/下移/删除。
    // 和「工作区」「工具」是同级区块，所以间距照 addGroup 的节奏来。
    layout->addSpacing(18);
    QPushButton *addGroupButton = nullptr;
    layout->addWidget(groupHeaderRow(QStringLiteral("分组"), content, &addGroupButton));
    connect(addGroupButton, &QPushButton::clicked, this, &SideBar::createGroup);
    layout->addSpacing(6);

    // 分组项单独放一个容器：重建时只清这里，标题行和 + 不受影响
    m_groupBody = new QWidget(content);
    m_groupLayout = new QVBoxLayout(m_groupBody);
    m_groupLayout->setContentsMargins(0, 0, 0, 0);
    m_groupLayout->setSpacing(3);
    layout->addWidget(m_groupBody);

    addGroup(QStringLiteral("工具"));
    addNav(AppPage::Terminal, Icon::Terminal, QStringLiteral("终端"));
    addNav(AppPage::Keys,     Icon::Key,      QStringLiteral("密钥管理"));
    addNav(AppPage::Settings, Icon::Settings, QStringLiteral("配置管理"));
    // 「软件更新」放在工具区最后。它不像上面几项有数量可显示，所以 refreshCounts
    // 里没有对应的一行 —— 这页只负责版本信息和「检查更新」按钮。
    addNav(AppPage::Updates,  Icon::Refresh,  QStringLiteral("软件更新"));

    // 底部留空。原来这儿有行版本号，挪到窗口标题后面去了 ——
    // 侧栏左下角常年挂着一个版本号，既占地方又没人看。
    layout->addStretch();

    auto *scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(scroll);

    if (m_store) {
        connect(m_store, &ConnectionStore::connectionsChanged, this, &SideBar::refreshCounts);
        // 连接数变了只刷数字，不重建 —— recordUse 每连一次都会发这个信号，
        // 跟着重建会让分组项不停地 deleteLater + 重建，右键菜单还会抖
        connect(m_store, &ConnectionStore::connectionsChanged, this, &SideBar::refreshGroupCounts);
        connect(m_store, &ConnectionStore::groupsChanged, this, &SideBar::refreshCounts);
        connect(m_store, &ConnectionStore::groupsChanged, this, &SideBar::rebuildGroups);
        connect(m_store, &ConnectionStore::historyChanged, this, &SideBar::refreshCounts);
    }

    setCurrentPage(AppPage::Connections);
    refreshCounts();
    rebuildGroups();
}

void SideBar::setUpdateAvailable(const QString &version)
{
    if (NavButton *button = m_buttons.value(int(AppPage::Updates))) {
        button->setUpdateBadge(version.isEmpty() ? QString()
                                                 : QStringLiteral("v") + version);
    }
}

void SideBar::setGlassOpacity(int percent)
{
    if (!m_cta)
        return;

    if (percent < 0) {
        // 没有背景图：清掉内联样式，回到主题里的实色
        m_cta->setStyleSheet(QString());
        return;
    }

    const qreal alpha = qBound(0.0, percent / 100.0, 1.0);
    const auto tinted = [alpha](const QColor &color) {
        QColor c = color;
        c.setAlphaF(alpha);
        return c.name(QColor::HexArgb);
    };

    // 内联样式只覆盖 background；圆角、padding、min-height 和子 QLabel 的
    // 字色仍然走 app.qss。hover / pressed 也要一起写，否则内联规则会把
    // 主题里的 hover 色盖掉，按钮悬浮时没有变化。
    m_cta->setStyleSheet(
        QStringLiteral("QPushButton#SideBarCta { background: %1; }"
                       "QPushButton#SideBarCta:hover,"
                       "QPushButton#SideBarCta:pressed { background: %2; }")
            .arg(tinted(Theme::color(QStringLiteral("nav-cta-bg"))),
                 tinted(Theme::color(QStringLiteral("nav-cta-bg-hover")))));
}

void SideBar::refreshCounts()
{
    if (!m_store)
        return;

    auto setCount = [this](AppPage page, int count) {
        if (NavButton *button = m_buttons.value(int(page)))
            button->setCount(count);
    };

    setCount(AppPage::Connections, m_store->connections().size());
    setCount(AppPage::History,     m_store->history().size());
    setCount(AppPage::Favorites,   m_store->favorites().size());
    setCount(AppPage::Keys,        0);
}

void SideBar::refreshGroupCounts()
{
    if (!m_store)
        return;

    for (const ConnectionGroup &group : m_store->groups()) {
        if (NavButton *button = m_groupButtons.value(group.id))
            button->setCount(m_store->connectionsInGroup(group.id).size(), false);
    }
}

void SideBar::rebuildGroups()
{
    if (!m_groupLayout)
        return;

    // 只清分组项这个容器，标题行和「+」是长期存在的
    while (QLayoutItem *item = m_groupLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_groupButtons.clear();

    if (!m_store)
        return;

    if (m_store->groups().isEmpty()) {
        auto *empty = makeLabel(QStringLiteral("还没有分组，点 + 新建"), "navSectionEmpty", m_groupBody);
        m_groupLayout->addWidget(empty);
    }

    for (const ConnectionGroup &group : m_store->groups()) {
        auto *button = new NavButton(groupAccent(group), group.name, m_groupBody);
        // 0 也显示：数字那一列要一直占着，不然分组之间的数字会左右跳
        button->setCount(m_store->connectionsInGroup(group.id).size(), false);

        const QString id = group.id;
        connect(button, &QPushButton::clicked, this, [this, id] { emit groupRequested(id); });

        // 右键菜单交给 Qt 的 customContextMenu（项目里第一次用，QSS 已经
        // 覆盖到 QMenu 了，不用额外写样式）
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QWidget::customContextMenuRequested, this,
                [this, button, id](const QPoint &pos) {
                    showGroupMenu(id, button->mapToGlobal(pos));
                });

        m_groupLayout->addWidget(button);
        m_groupButtons.insert(id, button);
    }

    // 重建之后把「当前停在哪个分组」的高亮套回去
    if (!m_currentGroup.isEmpty())
        setCurrentGroup(m_currentGroup);
}

void SideBar::showGroupMenu(const QString &groupId, const QPoint &globalPos)
{
    if (!m_store)
        return;
    if (!m_store->findGroup(groupId))
        return;

    const auto &groups = m_store->groups();
    int index = -1;
    for (int i = 0; i < groups.size(); ++i) {
        if (groups[i].id == groupId) {
            index = i;
            break;
        }
    }

    // 菜单**不要挂在按钮上**：操作 store 之后 groupsChanged 会重建分组项，
    // 按钮走 deleteLater()，而 exec() 是嵌套事件循环 —— 菜单跟着按钮一起
    // 被删掉就崩了。栈上、无父对象最省心。
    QMenu menu;
    QAction *editAction = menu.addAction(Icons::icon(Icon::Edit,
                                                     Theme::color(QStringLiteral("text-dim")), 16),
                                         QStringLiteral("编辑"));
    menu.addSeparator();
    QAction *upAction = menu.addAction(QStringLiteral("上移"));
    QAction *downAction = menu.addAction(QStringLiteral("下移"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(Icons::icon(Icon::Trash,
                                                       Theme::color(QStringLiteral("err")), 16),
                                           QStringLiteral("删除"));

    upAction->setEnabled(index > 0);
    downAction->setEnabled(index >= 0 && index + 1 < groups.size());

    QAction *picked = menu.exec(globalPos);
    if (!picked)
        return;

    if (picked == editAction)
        editGroup(groupId);
    else if (picked == upAction)
        m_store->moveGroup(groupId, -1);
    else if (picked == downAction)
        m_store->moveGroup(groupId, 1);
    else if (picked == deleteAction)
        deleteGroup(groupId);
}

void SideBar::createGroup()
{
    ConnectionGroup result;
    if (!GroupDialog::create(this, &result))
        return;
    if (!m_store)
        return;

    m_store->addGroup(result.name, result.color);
    emit statusMessage(QStringLiteral("已新建分组「%1」").arg(result.name));
}

void SideBar::editGroup(const QString &groupId)
{
    if (!m_store)
        return;

    const ConnectionGroup *group = m_store->findGroup(groupId);
    if (!group)
        return;

    // 对话框 exec() 期间 store 有可能被别处改（比如同时开着终端页），先拷一份
    const ConnectionGroup existing = *group;

    ConnectionGroup result;
    if (!GroupDialog::edit(existing, this, &result))
        return;

    m_store->updateGroup(groupId, result.name, result.color);
    emit statusMessage(QStringLiteral("分组已保存「%1」").arg(result.name));
}

void SideBar::deleteGroup(const QString &groupId)
{
    if (!m_store)
        return;

    const ConnectionGroup *group = m_store->findGroup(groupId);
    if (!group)
        return;

    const QString name = group->name;
    const int memberCount = m_store->connectionsInGroup(groupId).size();

    if (memberCount == 0) {
        m_store->removeGroup(groupId, false);
        emit statusMessage(QStringLiteral("已删除分组「%1」").arg(name));
        return;
    }

    // 「保留连接 / 一并删除」这层语义是从原来的分组管理页搬过来的，别简化成
    // 一个「确定吗」—— 删除分组时两件事后果差得很远
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("删除分组"));
    box.setText(QStringLiteral("「%1」里还有 %2 个连接。").arg(name).arg(memberCount));
    box.setInformativeText(QStringLiteral("这些连接要怎么处理？"));
    QPushButton *keep = box.addButton(QStringLiteral("保留连接"), QMessageBox::AcceptRole);
    QPushButton *drop = box.addButton(QStringLiteral("一并删除"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == keep) {
        m_store->removeGroup(groupId, false);
        emit statusMessage(QStringLiteral("已删除分组「%1」，连接已移到未分组").arg(name));
    } else if (box.clickedButton() == drop) {
        m_store->removeGroup(groupId, true);
        emit statusMessage(QStringLiteral("已删除分组「%1」及其 %2 个连接").arg(name).arg(memberCount));
    }
}

void SideBar::setCurrentPage(AppPage page)
{
    // 从某个分组页切走：先把分组项的高亮清掉。放在提前返回**之前** ——
    // 从分组页点「所有连接」时 m_current 可能本来就是 Connections，
    // 早退会把这行清高亮的代码跳过去。
    m_currentGroup.clear();
    for (auto it = m_groupButtons.constBegin(); it != m_groupButtons.constEnd(); ++it)
        it.value()->setActive(false);

    if (m_current == page && m_buttons.value(int(page))
        && m_buttons.value(int(page))->isActive())
        return;

    m_current = page;
    for (auto it = m_buttons.constBegin(); it != m_buttons.constEnd(); ++it)
        it.value()->setActive(it.key() == int(page));
}

void SideBar::setCurrentGroup(const QString &groupId)
{
    m_currentGroup = groupId;

    for (auto it = m_buttons.constBegin(); it != m_buttons.constEnd(); ++it)
        it.value()->setActive(false);
    for (auto it = m_groupButtons.constBegin(); it != m_groupButtons.constEnd(); ++it)
        it.value()->setActive(it.key() == groupId);
}

} // namespace Em
