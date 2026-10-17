#include "ui/ConnectionsPage.h"

#include "app/Theme.h"
#include "model/ConnectionStore.h"
#include "model/KeyStore.h"
#include "ui/ConnectionDialog.h"
#include "ui/IconProvider.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QScrollArea>
#include <QUuid>
#include <QVBoxLayout>

namespace Em {

ConnectionsPage::ConnectionsPage(ConnectionStore *store, KeyStore *keys, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_keys(keys)
{
    m_title = makeLabel(QStringLiteral("所有连接"), "pageTitle", this);
    QLabel *title = m_title;

    // 标题下面不再放描述 —— 页头只留标题、数量、搜索和筛选
    m_count = makeLabel(QString(), "pageCount", this);

    // 这里**不放「新建连接」按钮**：侧栏顶上那个是常驻的，页头再来一个只是重复。
    // 新建的入口只剩侧栏 CTA / Ctrl+N（它们都还走 openNewConnection()）。

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("搜索名称、主机、用户"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(220);
    connect(m_search, &QLineEdit::textChanged, this, &ConnectionsPage::setFilter);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(10);

    // 分组页标题前那个色点。普通页 / 收藏夹页里它一直藏着，
    // 颜色在 showGroup() 里按分组来设（不能把「●」拼进标题文字 —— 那样永远是黑的）
    m_groupDot = new QLabel(this);
    m_groupDot->setFixedSize(10, 10);
    m_groupDot->hide();

    auto *titleLine = new QHBoxLayout;
    titleLine->setContentsMargins(0, 0, 0, 0);
    titleLine->setSpacing(9);
    titleLine->addWidget(m_groupDot);
    titleLine->addWidget(title);
    titleLine->addWidget(m_count);
    titleLine->addStretch();

    headerRow->addLayout(titleLine);
    headerRow->addStretch();
    // 页头只留搜索框 —— 收藏夹 / 分组页各自由 configureAsFavorites() / showGroup()
    // 锁死筛选范围，不再给用户一个能切来切去的下拉。
    headerRow->addWidget(m_search);

    // 列表直接铺在页面底色上，**不套 Card**：右侧内容区只要一层底色，
    // 卡片再叠一层会显得很闷。行本身是透明的，靠 hover 时浮出的底色区分。
    m_list = new ConnectionList(this);
    m_list->setStore(store);

    connect(m_list, &ConnectionList::connectRequested, this, [this](const QString &id) {
        if (m_store) {
            if (const Connection *c = m_store->find(id))
                emit connectRequested(*c);
        }
    });
    connect(m_list, &ConnectionList::enterTerminalRequested, this, [this](const QString &id) {
        if (m_store) {
            if (const Connection *c = m_store->find(id))
                emit connectRequested(*c);
        }
    });
    connect(m_list, &ConnectionList::editRequested, this, &ConnectionsPage::openEditConnection);
    connect(m_list, &ConnectionList::deleteRequested, this, [this](const QString &id) {
        if (m_store) {
            const Connection *c = m_store->find(id);
            const QString name = c ? c->name : id;
            m_store->removeConnection(id);
            emit statusMessage(QStringLiteral("已删除「%1」").arg(name));
        }
    });
    connect(m_list, &ConnectionList::favoriteToggled, this, [this](const QString &id) {
        if (m_store)
            m_store->toggleFavorite(id);
    });
    connect(m_list, &ConnectionList::copyHostRequested, this, [this](const QString &id) {
        if (m_store) {
            if (const Connection *c = m_store->find(id)) {
                QApplication::clipboard()->setText(c->identity());
                emit statusMessage(QStringLiteral("已复制 %1").arg(c->identity()));
            }
        }
    });

    // ---- 页头 + 表头（固定不滚） ----
    //
    // 右边留 36 = 列表的 26 + 竖向滚动条 10。滚动条设成 AlwaysOn，
    // 这样不管列表长短，表头、搜索框和行始终在同一条右边线上；
    // 只剩一条透明轨道，看起来和没有滚动条一样。
    auto *pageHeader = new QWidget(this);
    auto *pageHeaderLayout = new QVBoxLayout(pageHeader);
    pageHeaderLayout->setContentsMargins(6, 22, 36, 0);
    pageHeaderLayout->setSpacing(14);
    pageHeaderLayout->addLayout(headerRow);
    pageHeaderLayout->addWidget(new ConnectionListHeader(pageHeader));

    auto *scroll = new QScrollArea(this);
    auto *scrollBody = new QWidget;
    auto *scrollLayout = new QVBoxLayout(scrollBody);
    scrollLayout->setContentsMargins(6, 0, 26, 22);
    // 列表吃掉页面剩下的高度：空状态要靠这个才能整块垂直居中。
    // 有内容时列表内部的 stretch 会把行顶在上边。
    scrollLayout->addWidget(m_list, 1);
    scroll->setWidget(scrollBody);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(pageHeader);
    root->addWidget(scroll, 1);

    // 新建 / 编辑表单在 ConnectionDialog 里，这里只管「弹出来、存下去」。
    // 表单本身是公共的那一份，三处（这里、控制台、终端页）都用它。
    if (m_store) {
        // 分组页走的是 setStaticList()，那是**死数据**，不会自己跟着 store 刷。
        // 不补下面这两条的话，在分组页里新建/删除连接、或者把连接挪进挪出，
        // 列表会一直停在打开那一刻。
        connect(m_store, &ConnectionStore::connectionsChanged, this, [this] {
            if (!m_fixedGroupId.isEmpty())
                applyFilters();
        });
        connect(m_store, &ConnectionStore::groupsChanged, this, [this] {
            if (m_fixedGroupId.isEmpty())
                return;
            refreshGroupHeader();   // 改名 / 改色，副标题的连接数也会变
            applyFilters();
        });
    }

    applyFilters();
}

void ConnectionsPage::openNewConnection()
{
    if (!m_store)
        return;

    Connection input;
    if (!ConnectionDialog::createNew(m_store, m_keys, this, &input))
        return;

    Connection fresh = input;
    fresh.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    fresh.createdAt = QDateTime::currentDateTime();
    // 在分组页里新建连接，默认就归到这个分组（用户自己在下拉里选了别处时不覆盖）
    if (!m_fixedGroupId.isEmpty() && fresh.groupId.isEmpty())
        fresh.groupId = m_fixedGroupId;
    m_store->addConnection(fresh);
    emit statusMessage(QStringLiteral("已添加「%1」").arg(fresh.name));
}

void ConnectionsPage::openEditConnection(const QString &id)
{
    if (!m_store)
        return;

    const Connection *existing = m_store->find(id);
    if (!existing)
        return;

    Connection edited;
    if (!ConnectionDialog::edit(*existing, m_store, m_keys, this, &edited))
        return;

    m_store->updateConnection(edited);
    emit statusMessage(QStringLiteral("已保存「%1」").arg(edited.name));
}

void ConnectionsPage::configureAsFavorites()
{
    m_title->setText(QStringLiteral("收藏夹"));

    // 这一页只看收藏。以前靠页头那个「全部连接 / 只看收藏」下拉锁，现在
    // 下拉没了，直接在代码里锁死。
    m_favoritesOnly = true;

    m_list->setEmptyState(QStringLiteral("还没有收藏"),
                          QStringLiteral("在连接行右边点一下星标，它就会收在这里。"),
                          Icon::StarOutline);

    applyFilters();
}

void ConnectionsPage::showGroup(const QString &groupId)
{
    if (!m_store || !m_store->findGroup(groupId))
        return;

    // 同一个分组被改名/改色时 MainWindow 也会调进来 —— 那就只刷标题和列表，
    // 不要清掉用户已经输入的搜索词
    const bool changed = (m_fixedGroupId != groupId);
    m_fixedGroupId = groupId;

    if (changed) {
        m_keyword.clear();
        m_search->clear();
    }

    // 和收藏夹页不同，分组页空着通常是「还没把连接归到这个分组」，
    // 得说清楚怎么办
    m_list->setEmptyState(QStringLiteral("这个分组还是空的"),
                          QStringLiteral("编辑连接时把「分组」选成它，连接就会出现在这里。"),
                          Icon::Folder);

    refreshGroupHeader();
    applyFilters();
}

void ConnectionsPage::refreshGroupHeader()
{
    if (m_fixedGroupId.isEmpty() || !m_store)
        return;

    const ConnectionGroup *group = m_store->findGroup(m_fixedGroupId);
    if (!group)
        return;   // 被删了：切页由 MainWindow 负责，这里什么都不用做

    m_title->setText(group->name);

    m_groupDot->setStyleSheet(QStringLiteral("background-color:%1; border-radius:5px;")
                                  .arg(groupAccent(*group).name()));
    m_groupDot->show();
}

void ConnectionsPage::setGlassOpacity(int percent)
{
    if (!m_search)
        return;

    if (percent < 0) {
        m_search->setStyleSheet(QString());
        return;
    }

    // 只覆盖 background；边框、圆角、focus 边框仍然走 app.qss
    QColor color = Theme::color(QStringLiteral("bg-input"));
    color.setAlphaF(qBound(0.0, percent / 100.0, 1.0));
    m_search->setStyleSheet(QStringLiteral("QLineEdit { background: %1; }")
                                .arg(color.name(QColor::HexArgb)));
}

void ConnectionsPage::setFilter(const QString &keyword)
{
    m_keyword = keyword;
    applyFilters();
}

void ConnectionsPage::applyFilters()
{
    if (!m_store || !m_list)
        return;

    // 范围只有两种来源：分组页锁死的 groupId，或者「所有连接」的平铺/分节。
    // 收藏夹页用 m_favoritesOnly 锁死只看收藏。
    const QString groupId = m_fixedGroupId;

    m_list->setGrouped(groupId.isEmpty());
    m_list->setFavoritesOnly(m_favoritesOnly);
    m_list->setFilter(m_keyword);

    // 指定了分组就不再按分组分节，直接把那一组平铺出来
    if (!groupId.isEmpty()) {
        QVector<Connection> bucket;
        for (const Connection &c : m_store->connections()) {
            if (c.groupId == groupId)
                bucket << c;
        }
        if (!m_keyword.trimmed().isEmpty()) {
            const QString needle = m_keyword.trimmed().toLower();
            QVector<Connection> filtered;
            for (const Connection &c : bucket) {
                if (c.searchBlob().contains(needle))
                    filtered << c;
            }
            bucket = filtered;
        }
        // 收藏过滤交给列表自己做（上面已经 setFavoritesOnly），这里别再算一遍
        m_list->setStaticList(bucket);
    } else {
        m_list->setStore(m_store);
    }

    // 标题右边那个「N 台」跟着筛选结果走：搜索、只看收藏、分组都会让它变。
    // 数量从列表里拿，别在这儿再算一遍 —— 筛选规则只该有一份。
    if (m_count)
        m_count->setText(QStringLiteral("%1 台").arg(m_list->visibleCount()));
}

} // namespace Em
