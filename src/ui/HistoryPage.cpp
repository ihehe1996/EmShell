#include "ui/HistoryPage.h"

#include "app/Theme.h"
#include "model/ConnectionStore.h"
#include "ui/IconProvider.h"
#include "ui/Widgets.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMouseEvent>
#include <QScrollArea>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace Em {

namespace {

// 和连接表同一套做法：表头、行共用列宽权重，列部件横向提示全压成 0
// （ColumnLabel / ColumnCell / ColumnElidedLabel，见 Widgets.cpp）。
constexpr int kColName = 26;
constexpr int kColHost = 22;
constexpr int kColUser = 16;
constexpr int kColStatus = 12;
constexpr int kColTime = 14;
constexpr int kColumnSpacing = 12;
constexpr int kRowSidePadding = 12;
constexpr int kRowHeight = 44;
constexpr int kActionButton = 26;

QString fullTime(const QDateTime &when)
{
    if (!when.isValid())
        return QStringLiteral("—");
    return when.toString(QStringLiteral("MM-dd HH:mm:ss"));
}

// 一条历史记录，表格的一行。
class HistoryRow : public QFrame
{
public:
    explicit HistoryRow(const HistoryEntry &entry, QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setProperty("role", QStringLiteral("row"));
        setFixedHeight(kRowHeight);
        setCursor(Qt::PointingHandCursor);

        const auto ignoreHint = [](QWidget *w) {
            w->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        };

        auto *name = new ColumnElidedLabel(
            entry.name.isEmpty() ? entry.host : entry.name, this);
        name->setProperty("role", QStringLiteral("rowTitle"));

        auto *host = new ColumnElidedLabel(entry.host, this);
        host->setProperty("role", QStringLiteral("rowHost"));
        host->setFont(Theme::monoFont(12));

        auto *user = new ColumnElidedLabel(
            entry.username.isEmpty() ? QStringLiteral("—") : entry.username, this);
        user->setProperty("role", QStringLiteral("rowUser"));

        // 状态：圆点 + 成功 / 失败
        auto *statusColumn = new ColumnCell(this);
        ignoreHint(statusColumn);
        auto *statusLayout = new QHBoxLayout(statusColumn);
        statusLayout->setContentsMargins(0, 0, 0, 0);
        statusLayout->setSpacing(6);

        auto *dot = new QLabel(statusColumn);
        dot->setFixedSize(6, 6);
        dot->setStyleSheet(QStringLiteral("background-color:%1; border-radius:3px;")
                               .arg(Theme::color(entry.ok ? QStringLiteral("ok")
                                                          : QStringLiteral("err")).name()));
        auto *status = makeLabel(entry.ok ? QStringLiteral("成功") : QStringLiteral("失败"),
                                 "historyStatus", statusColumn);
        status->setProperty("state", entry.ok ? QStringLiteral("ok") : QStringLiteral("fail"));
        statusLayout->addWidget(dot);
        statusLayout->addWidget(status);
        statusLayout->addStretch();

        auto *when = new ColumnLabel(fullTime(entry.at), this);
        when->setProperty("role", QStringLiteral("rowLast"));
        when->setFont(Theme::monoFont(11));

        auto *reconnect = new QPushButton(this);
        reconnect->setProperty("role", QStringLiteral("icon"));
        reconnect->setFixedSize(kActionButton, kActionButton);
        reconnect->setFocusPolicy(Qt::NoFocus);
        reconnect->setCursor(Qt::PointingHandCursor);
        reconnect->setIcon(Icons::icon(Icon::Play, Theme::color(QStringLiteral("text-dim")), 14));
        reconnect->setIconSize(QSize(14, 14));
        reconnect->setToolTip(QStringLiteral("重新连接"));
        connect(reconnect, &QPushButton::clicked, this, [this] {
            if (onReconnect)
                onReconnect();
        });

        auto *actions = new QWidget(this);
        actions->setFixedWidth(kActionButton);
        auto *actionsLayout = new QHBoxLayout(actions);
        actionsLayout->setContentsMargins(0, 0, 0, 0);
        actionsLayout->addWidget(reconnect);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(kRowSidePadding, 0, kRowSidePadding, 0);
        layout->setSpacing(kColumnSpacing);
        layout->addWidget(name, kColName);
        layout->addWidget(host, kColHost);
        layout->addWidget(user, kColUser);
        layout->addWidget(statusColumn, kColStatus);
        layout->addWidget(when, kColTime);
        layout->addWidget(actions);

        QString target = entry.host;
        if (!entry.username.isEmpty())
            target = entry.username + QLatin1Char('@') + entry.host;
        setToolTip(QStringLiteral("%1\n%2\n双击重新连接")
                       .arg(entry.name.isEmpty() ? entry.host : entry.name,
                            target));
    }

    std::function<void()> onReconnect;

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onReconnect)
            onReconnect();
        QFrame::mouseDoubleClickEvent(event);
    }
};

// 表头：名称 / 主机 / 用户 / 状态 / 时间 + 操作占位
class HistoryListHeader : public QWidget
{
public:
    explicit HistoryListHeader(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("ListHeader"));
        setAttribute(Qt::WA_StyledBackground, true);
        setFixedHeight(34);

        const auto column = [this](const QString &text, int stretch) {
            auto *label = new ColumnLabel(text, this);
            label->setProperty("role", QStringLiteral("columnHeader"));
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            return std::make_pair(label, stretch);
        };
        const auto add = [](QHBoxLayout *layout, const std::pair<QLabel *, int> &item) {
            layout->addWidget(item.first, item.second);
        };

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(kRowSidePadding, 0, kRowSidePadding, 0);
        layout->setSpacing(kColumnSpacing);
        add(layout, column(QStringLiteral("名称"), kColName));
        add(layout, column(QStringLiteral("主机"), kColHost));
        add(layout, column(QStringLiteral("用户"), kColUser));
        add(layout, column(QStringLiteral("状态"), kColStatus));
        add(layout, column(QStringLiteral("时间"), kColTime));

        // 固定宽度的占位，理由和连接表一样：addSpacing 放最后一项时不把右边距算进去
        auto *actionSpace = new QWidget(this);
        actionSpace->setFixedWidth(kActionButton);
        layout->addWidget(actionSpace);
    }
};

} // namespace

HistoryPage::HistoryPage(ConnectionStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    auto *title = makeLabel(QStringLiteral("最近使用"), "pageTitle", this);
    m_count = makeLabel(QString(), "pageCount", this);

    auto *clearButton = new PillButton(QStringLiteral("清空记录"), PillButton::Ghost, this);
    clearButton->setLeadingIcon(Icon::Trash, 15);
    m_clearButton = clearButton;
    connect(clearButton, &QPushButton::clicked, this, [this] {
        if (!m_store || m_store->history().isEmpty())
            return;
        const auto answer = QMessageBox::question(
            this, QStringLiteral("清空历史"),
            QStringLiteral("确定要清空全部 %1 条连接记录吗？连接本身不会被删除。")
                .arg(m_store->history().size()));
        if (answer == QMessageBox::Yes) {
            m_store->clearHistory();
            emit statusMessage(QStringLiteral("历史记录已清空"));
        }
    });

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(10);
    headerRow->addWidget(title);
    headerRow->addWidget(m_count);
    headerRow->addStretch();
    headerRow->addWidget(clearButton);

    // 和「所有连接」一样：页头 + 表头固定不滚，右边留出滚动条那 10px
    auto *pageHeader = new QWidget(this);
    auto *pageHeaderLayout = new QVBoxLayout(pageHeader);
    pageHeaderLayout->setContentsMargins(6, 22, 36, 0);
    pageHeaderLayout->setSpacing(14);
    pageHeaderLayout->addLayout(headerRow);
    pageHeaderLayout->addWidget(new HistoryListHeader(pageHeader));

    auto *body = new QWidget(this);
    m_layout = new QVBoxLayout(body);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    auto *scrollBody = new QWidget;
    auto *scrollLayout = new QVBoxLayout(scrollBody);
    scrollLayout->setContentsMargins(6, 0, 26, 22);
    scrollLayout->addWidget(body, 1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidget(scrollBody);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(pageHeader);
    root->addWidget(scroll, 1);

    if (m_store)
        connect(m_store, &ConnectionStore::historyChanged, this, &HistoryPage::rebuild);

    rebuild();
}

void HistoryPage::rebuild()
{
    if (!m_layout)
        return;

    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    const int count = m_store ? m_store->history().size() : 0;
    if (m_count)
        m_count->setText(QStringLiteral("%1 条").arg(count));
    if (m_clearButton)
        m_clearButton->setEnabled(count > 0);

    if (!m_store || m_store->history().isEmpty()) {
        auto *emptyBox = new QWidget(this);
        auto *emptyLayout = new QVBoxLayout(emptyBox);
        emptyLayout->setContentsMargins(24, 40, 24, 40);
        emptyLayout->setSpacing(0);

        auto *icon = new QLabel(emptyBox);
        icon->setFixedSize(36, 36);
        icon->setAlignment(Qt::AlignCenter);
        icon->setPixmap(Icons::pixmap(Icon::History,
                                      Theme::color(QStringLiteral("text-mute")), 36));
        emptyLayout->addWidget(icon, 0, Qt::AlignHCenter);
        emptyLayout->addSpacing(16);

        auto *emptyTitle = makeLabel(QStringLiteral("还没有连接记录"), "emptyTitle", emptyBox);
        emptyTitle->setAlignment(Qt::AlignCenter);
        emptyLayout->addWidget(emptyTitle);
        emptyLayout->addSpacing(6);

        auto *emptyHint = makeLabel(QStringLiteral("连一次服务器，这里就会出现一条记录。"),
                                    "emptyHint", emptyBox);
        emptyHint->setAlignment(Qt::AlignCenter);
        emptyLayout->addWidget(emptyHint, 0, Qt::AlignHCenter);

        m_layout->addStretch(4);
        m_layout->addWidget(emptyBox);
        m_layout->addStretch(5);
        return;
    }

    for (const HistoryEntry &entry : m_store->history()) {
        auto *row = new HistoryRow(entry, this);
        const QString id = entry.connectionId;
        row->onReconnect = [this, id] { emit reconnectRequested(id); };
        m_layout->addWidget(row);
    }

    m_layout->addStretch();
}

} // namespace Em
