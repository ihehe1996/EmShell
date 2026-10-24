#pragma once

#include "model/Connection.h"
#include "ui/IconProvider.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QVector>
#include <QWidget>

class QEvent;
class QShowEvent;
class QVBoxLayout;

namespace Em {

class ConnectionProbe;
class ConnectionStore;

// 改了用于 QSS 匹配的动态属性之后，必须重新 polish 一次样式，
// 否则界面不会跟着变。所有 setProperty 之后都要调它。
void repolish(QWidget *w);

// 造一个带 role 的标签，role 用来在 QSS 里区分文字层级。
QLabel *makeLabel(const QString &text, const char *role, QWidget *parent = nullptr);

// 分组的标记色。用户选过的用选的颜色；老数据没有 color 字段（或将来哪里漏填了）
// 就回退到主题的强调蓝。**兜底只写在这一处**，别在侧栏和连接列表各写一遍。
QColor groupAccent(const ConnectionGroup &group);

// ---------------------------------------------------------------------------
// 容器
// ---------------------------------------------------------------------------

class Card : public QFrame
{
    Q_OBJECT
public:
    explicit Card(QWidget *parent = nullptr, bool flat = false);
};

// 会按实际宽度自动省略的标签。
//
// QLabel 有两个毛病凑在一起，窄容器里必然溢出：
//   1. minimumSizeHint() 是按**整段文字**算的，在布局里压不小；
//   2. 真放不下时它不省略，直接把超出部分裁掉 —— 中文正裁在半个字上，很难看。
// 这个类两头都治：宽度不给下界，文字按当前宽度缩成「前半截…」。
// 右栏那种固定宽度、内容又比它宽的地方就得用它。
class ElidedLabel : public QLabel
{
    Q_OBJECT
public:
    ElidedLabel(const QString &text, QWidget *parent = nullptr);

    void setFullText(const QString &text);
    QString fullText() const { return m_full; }

    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyElide();

    QString m_full;
};

// 表格列用的三个小部件：横向 sizeHint / minimumSizeHint 一律压成 0。
//
// 表头和行是两套独立的 QHBoxLayout，只靠相同的 stretch 比例还不够 —— 布局会先
// 满足每项的最小宽度，再把剩余空间按 stretch 分。行里「名称」这格要装下圆点等
// 固定内容，最小宽度比表头那两个字大，整列就会被往右推，越往后的列偏得越多。
// 所有表格（连接列表、最近使用）的列都用这几个类。
class ColumnLabel : public QLabel
{
public:
    explicit ColumnLabel(const QString &text, QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
};

class ColumnCell : public QWidget
{
public:
    explicit ColumnCell(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
};

// ElidedLabel 只压了 minimumSizeHint，sizeHint 还是整段文字的宽度；
// 表格里连 sizeHint 也要压成 0。
class ColumnElidedLabel : public ElidedLabel
{
public:
    explicit ColumnElidedLabel(const QString &text, QWidget *parent = nullptr);

    QSize sizeHint() const override;
};

// 一条渐变阴影，夹在两块底色接近的区域中间，模拟一边投在另一边的影子。
//
// 用 QGraphicsDropShadowEffect 也能做，但那个是把整个控件先渲染到离屏位图再模糊，
// 侧栏里挂着 QScrollArea，滚动时会很难看。这里只是一条十几像素的渐变，便宜得多。
//
// side 说的是**影子落在哪一侧**（也就是这条该插在哪儿）：
//   Qt::RightEdge  —— 影子往右淡出。插在左边那块的右侧（侧栏）
//   Qt::LeftEdge   —— 影子往左淡出。插在右边那块的左侧（抽屉面板）
//   Qt::BottomEdge —— 影子往下淡出。插在头部那块的下方（抽屉的头部下面）
//   Qt::TopEdge    —— 影子往上淡出。插在底部那块的上方（抽屉的按钮上面）
class EdgeShadow : public QWidget
{
    Q_OBJECT
public:
    explicit EdgeShadow(Qt::Edge side, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Qt::Edge m_side;
};

// 首页那种「图标 + 标题 + 说明」的快捷入口
class ActionTile : public QFrame
{
    Q_OBJECT
public:
    ActionTile(Icon icon, const QString &title, const QString &desc, QWidget *parent = nullptr);

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    // 配色切换：图标颜色是构造时烘进 pixmap 的，收到 ThemeChange 要重画
    void changeEvent(QEvent *event) override;

private:
    Icon m_icon;
    QLabel *m_iconBox = nullptr;
};

// ---------------------------------------------------------------------------
// 按钮
// ---------------------------------------------------------------------------

class PillButton : public QPushButton
{
    Q_OBJECT
public:
    enum Variant { Primary, Ghost, Subtle, Danger };

    explicit PillButton(const QString &text, Variant variant = Primary, QWidget *parent = nullptr);

    void setVariant(Variant variant);
    // 把图标画在文字左边，颜色跟着按钮的 variant 走
    void setLeadingIcon(Icon which, int size = 15);
    // 只有图标、没有文字的紧凑按钮
    void makeCompact(Icon which, int size = 15, const QString &tooltip = QString());

protected:
    void changeEvent(QEvent *event) override;

private:
    void refreshIcon();
    Variant m_variant;
    Icon m_icon = Icon::Close;
    bool m_hasIcon = false;
    int m_iconSize = 15;
};

// 下拉选择框。
//
// 自己画右边那颗箭头，弹出列表也按主题配色。整套控件要跟 App 的圆角、行高、
// 强调色对齐，Windows 原生那套放在深色界面里太跳。
class SelectBox : public QComboBox
{
    Q_OBJECT
public:
    explicit SelectBox(QWidget *parent = nullptr);

public slots:
    // 弹出前把列表行距压回 0，否则每项会重叠。理由写在 .cpp 里。
    void showPopup() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    // 弹出列表的样式只能挂在 view 自己身上（见 .cpp 里的说明）。
    // 配色变了要重挂一遍。
    void applyPopupStyle();

    bool m_hover = false;
};

// QSS 只负责画方框和底色，**不会画对勾**（选中时就是一个实心色块）。
// 这里在它画完之后补一颗白色的 Check 图标，深色 / 浅色主题都看得清。
class CheckBox : public QCheckBox
{
    Q_OBJECT
public:
    explicit CheckBox(const QString &text, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

// 侧边栏的导航项。
//
// 图标和文字是两独立的 QLabel，用布局排出来的，**没有**用 QPushButton 自带的
// setIcon / setText —— 那套的「图标和文字之间留多少空」是 Qt 样式内部写死的，
// QSS 里根本没有对应属性可调，结果就是两者糊在一起。走布局之后间距、左边距、
// 数字的位置全都自己说了算。按钮本身只负责背景和点击。
class NavButton : public QPushButton
{
    Q_OBJECT
public:
    NavButton(Icon icon, const QString &text, QWidget *parent = nullptr);
    // 分组项：图标列里不是一个图标，而是一个 8px 的彩色圆点。
    // 圆点用 QSS 的背景色画（margin 5px 把 18 的槽缩成 8 并居中），
    // 不自己 QPainter 画 pixmap —— 那会撞上高 DPI 缩放的坑（图标被裁掉一条边）。
    NavButton(const QColor &dotColor, const QString &text, QWidget *parent = nullptr);

    // hideWhenZero=false 时 0 也显示（分组项要占住那一列，不然数字会跳）
    void setCount(int count, bool hideWhenZero = true);
    // 「软件更新」那一项右边的红色版本徽标。传空串表示隐藏。
    // 和数量药丸复用同一个 QLabel，但走单独的 badge 属性，颜色不受 active/hover 影响。
    void setUpdateBadge(const QString &text);
    void setActive(bool active);
    bool isActive() const { return m_active; }
    void setDotColor(const QColor &color);

    // 显示的文字在一个子 QLabel 里，QPushButton::text() 是空的。
    // 要拿界面上看到的文字（比如自检脚本给截图起名）用这个。
    QString labelText() const;

protected:
    // 悬停时图标和文字要一起加深。QSS 管不到 QLabel 里的 pixmap，
    // 而且子控件的样式也不会跟着按钮的状态自动重算，只能自己盯着进出事件刷。
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    // 配色切换后图标颜色要重新生成
    void changeEvent(QEvent *event) override;

private:
    // 把当前的 active / hover 状态刷到图标、文字和数字上。状态一变就要调，
    // 为什么不交给 QSS 见实现里的注释（和祖先选择器有关，是个隐蔽的坑）。
    void refreshState();

    void buildUi(const QString &text);

    Icon m_icon = Icon::Home;   // 圆点模式下不用
    bool m_dotMode = false;
    // 完整文字。ElidedLabel 会把 QLabel::text() 改成带省略号的显示值，
    // 所以 labelText() 得返回这里存的原串（--uishots 靠它给截图起名）。
    QString m_text;
    QLabel *m_iconLabel = nullptr;
    QLabel *m_textLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    bool m_active = false;
    bool m_hover = false;
};

// 标题栏右上角的窗口按钮
class WindowButton : public QPushButton
{
    Q_OBJECT
public:
    WindowButton(Icon icon, const QString &role, QWidget *parent = nullptr);

protected:
    // QCommonStyle 画按钮图标时不会因为鼠标悬浮就切到 QIcon::Active，
    // 所以 QIcon 里预烘的那版悬停色根本用不上 —— 只能自己盯着进出事件换。
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void refreshIcon();

    Icon m_icon = Icon::Close;
    bool m_hover = false;
};

// ---------------------------------------------------------------------------
// 列表元素
// ---------------------------------------------------------------------------

// 分组标题行：三角 + 文件夹 + 名字 + 数量
class GroupHeader : public QFrame
{
    Q_OBJECT
public:
    // color 是分组的标记色：文件夹图标用它着色，和侧栏那个圆点对得上
    GroupHeader(const QString &name, int count, const QColor &color, QWidget *parent = nullptr);

    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }
    void setCount(int count);

signals:
    void toggled(bool expanded);

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    bool m_expanded = true;
    QLabel *m_chevron = nullptr;
    QLabel *m_folder = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_count = nullptr;
    QColor m_folderColor;
};

// 连接列表的表头。列宽权重和 ConnectionRow 里的是同一组常量（见 Widgets.cpp），
// 改一边必须改另一边，否则表头和内容会对不齐。
class ConnectionListHeader : public QWidget
{
public:
    explicit ConnectionListHeader(QWidget *parent = nullptr);
};

// 一条连接。**表格的一行**，不是卡片：
//   名称（分组色点 + 协议图标 + 名字） / 主机 / 端口 / 用户 / 状态 / 延迟 / 操作
// 行的行为还是原来那些：双击连、星标收藏、右边按钮弹菜单。
class ConnectionRow : public QFrame
{
    Q_OBJECT
public:
    // groupName 只进 tooltip，**不显示在名称列里**（用户明确不要）；
    // groupColor 是名字前面那个小圆点的颜色。
    ConnectionRow(const Connection &connection, const QString &groupName,
                  const QColor &groupColor, QWidget *parent = nullptr);

    const Connection &connection() const { return m_connection; }
    void setConnection(const Connection &c);
    // 探测结果：probing=true 时显示「检测中…」；否则 online 决定状态，
    // latencyMs < 0 或离线时延迟显示「—」。颜色由 QSS 的 level 属性控制。
    void setProbeStatus(bool online, int latencyMs, bool probing);
    void setActive(bool active);

signals:
    void connectRequested(const QString &id);
    void editRequested(const QString &id);
    void manageLinksRequested(const QString &id);
    void deleteRequested(const QString &id);
    void favoriteToggled(const QString &id);
    void copyHostRequested(const QString &id);
    void enterTerminalRequested(const QString &id);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    // 右键整行也弹出「打开终端 / 编辑 / 复制主机地址 / 删除」，
    // 位置跟着鼠标走；三点按钮复用同一份菜单。
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void refresh();
    void showMenu(const QPoint &globalPos);

    Connection m_connection;
    QString m_groupName;
    bool m_probing = true;
    bool m_online = false;
    int m_latencyMs = -1;

    QLabel *m_groupDot = nullptr;
    // 名字前的协议图标：SSH 是终端，RDP 是显示器
    QLabel *m_protocolIcon = nullptr;
    ElidedLabel *m_name = nullptr;

    ElidedLabel *m_host = nullptr;
    QLabel *m_port = nullptr;
    ElidedLabel *m_user = nullptr;

    QLabel *m_statusDot = nullptr;
    QLabel *m_statusText = nullptr;
    QLabel *m_latency = nullptr;

    QPushButton *m_star = nullptr;
    QPushButton *m_connect = nullptr;
    QPushButton *m_more = nullptr;
};

// ---------------------------------------------------------------------------
// 连接列表
//
// 首页、所有连接页、收藏夹页三处都是同一套东西，抽出来一份。
// 数据来源二选一：挂一个 ConnectionStore（自动跟着信号刷新），
// 或者直接喂一个列表（收藏夹那种过滤后的结果）。
// ---------------------------------------------------------------------------
class ConnectionList : public QWidget
{
    Q_OBJECT

public:
    explicit ConnectionList(QWidget *parent = nullptr);

    void setStore(ConnectionStore *store);
    void setStaticList(const QVector<Connection> &list);

    // 按 store 里的分组顺序排序；关掉就是列表原来的顺序
    void setGrouped(bool grouped);
    // 只显示收藏
    void setFavoritesOnly(bool only);
    // 关键字过滤
    void setFilter(const QString &keyword);
    // 列表为空时显示什么。不设就用默认的「还没有连接」。
    // 有关键字过滤时仍然优先显示「没有匹配…」。
    // icon 画在标题上方那个圆角底座里（收藏夹给 StarOutline、分组给 Folder）。
    void setEmptyState(const QString &title, const QString &hint,
                       Icon icon = Icon::Server);

    // 当前实际显示的行数（收藏 / 关键字筛选之后）。页头的「N 台」用它。
    int visibleCount() const { return m_visibleCount; }

    void refresh();

signals:
    void connectRequested(const QString &id);
    void editRequested(const QString &id);
    void manageLinksRequested(const QString &id);
    void deleteRequested(const QString &id);
    void favoriteToggled(const QString &id);
    void copyHostRequested(const QString &id);
    void enterTerminalRequested(const QString &id);

protected:
    // 页面切回来时补一次探测（缓存 60 秒内不会重复发）
    void showEvent(QShowEvent *event) override;
    // 行里的图标颜色是建行时烘进去的，配色变了整表重建一次最省心
    void changeEvent(QEvent *event) override;

private:
    void rebuild();
    void requestProbes();
    void onProbeFinished(const QString &id, bool online, int latencyMs);

    struct ProbeInfo
    {
        bool valid = false;
        bool online = false;
        int latencyMs = -1;
        qint64 checkedAt = 0;
        QString signature;   // host:port，改了地址就重新探测
    };

    ConnectionStore *m_store = nullptr;
    ConnectionProbe *m_probe = nullptr;
    QHash<QString, ProbeInfo> m_probeCache;
    QHash<QString, ConnectionRow *> m_rows;
    QVector<Connection> m_visibleConnections;
    QVector<Connection> m_static;
    bool m_useStatic = false;
    bool m_grouped = true;
    bool m_favoritesOnly = false;
    QString m_filter;
    QString m_emptyTitle;
    QString m_emptyHint;
    Icon m_emptyIcon = Icon::Server;
    int m_visibleCount = 0;

    QVBoxLayout *m_layout = nullptr;
};

} // namespace Em
