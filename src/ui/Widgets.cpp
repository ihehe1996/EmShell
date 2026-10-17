#include "ui/Widgets.h"

#include "app/Theme.h"
#include "model/ConnectionStore.h"
#include "ui/ConnectionProbe.h"

#include <QAbstractItemView>
#include <QListView>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>
#include <QStyle>
#include <QVBoxLayout>

#include <utility>

namespace Em {

namespace {

// 一条边影的厚度（竖直边是宽度，水平边是高度）
constexpr int kEdgeShadowSize = 12;

// 连接表的列宽权重：名称 / 主机 / 端口 / 用户 / 状态 / 延迟 / 最近连接。
// **表头 ConnectionListHeader 和 ConnectionRow 共用这一组值**，改一边必须改另一边，
// 否则表头和内容会错列。右侧操作区是固定宽度，不参与拉伸。
constexpr int kColName = 22;
constexpr int kColHost = 18;
constexpr int kColPort = 7;
constexpr int kColUser = 12;
constexpr int kColStatus = 10;
constexpr int kColLatency = 9;
constexpr int kColLast = 12;
constexpr int kColumnSpacing = 12;
constexpr int kRowSidePadding = 12;
constexpr int kRowHeight = 44;
// 行内三个操作按钮都做成 26px 的纯图标按钮，和表格的行高 / 字号协调。
constexpr int kActionButton = 26;
constexpr int kActionSpacing = 4;
constexpr int kActionAreaWidth = kActionButton * 3 + kActionSpacing * 2;

QColor tokenColor(const char *name)
{
    return Theme::color(QString::fromLatin1(name));
}

// 做一个三态图标：正常、悬停、按下。QSS 没法给图标上色，
// 所以颜色变化只能在 QIcon 里预先烘好。
QIcon statefulIcon(Icon which, const QColor &normal, const QColor &hover, int size)
{
    QIcon ic;
    ic.addPixmap(Icons::pixmap(which, normal, size), QIcon::Normal, QIcon::Off);
    ic.addPixmap(Icons::pixmap(which, hover, size), QIcon::Active, QIcon::Off);
    ic.addPixmap(Icons::pixmap(which, hover, size), QIcon::Selected, QIcon::Off);
    ic.addPixmap(Icons::pixmap(which, hover, size), QIcon::Normal, QIcon::On);
    return ic;
}

// 相对时间：几分钟前 / 几小时前 / 几天前
QString humanizeSince(const QDateTime &when)
{
    if (!when.isValid())
        return QStringLiteral("从未连接");

    const qint64 secs = when.secsTo(QDateTime::currentDateTime());
    if (secs < 60)       return QStringLiteral("刚刚");
    if (secs < 3600)     return QStringLiteral("%1 分钟前").arg(secs / 60);
    if (secs < 86400)    return QStringLiteral("%1 小时前").arg(secs / 3600);
    if (secs < 86400 * 30) return QStringLiteral("%1 天前").arg(secs / 86400);
    return when.toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace

// ------------------------------------------------------ 表格列小部件 ---
//
// 横向 sizeHint / minimumSizeHint 全部压成 0：表头和行是两套独立的
// QHBoxLayout，只靠相同的 stretch 比例还不够 —— 布局会先满足每项的最小宽度，
// 再把剩余空间按 stretch 分。行里名称格要装下圆点等固定内容，最小宽度比表头
// 那两个字大，整列就会被往右推，越往后的列偏得越多（实测主机列差 30px）。
// 压成 0 之后两边就只剩下纯 stretch 比例，列能严格对齐。

ColumnLabel::ColumnLabel(const QString &text, QWidget *parent)
    : QLabel(text, parent) {}

QSize ColumnLabel::sizeHint() const
{
    const QSize s = QLabel::sizeHint();
    return QSize(0, s.height());
}

QSize ColumnLabel::minimumSizeHint() const
{
    const QSize s = QLabel::minimumSizeHint();
    return QSize(0, s.height());
}

ColumnCell::ColumnCell(QWidget *parent) : QWidget(parent) {}

QSize ColumnCell::sizeHint() const
{
    const QSize s = QWidget::sizeHint();
    return QSize(0, s.height());
}

QSize ColumnCell::minimumSizeHint() const
{
    const QSize s = QWidget::minimumSizeHint();
    return QSize(0, s.height());
}

// ElidedLabel 只压了 minimumSizeHint，sizeHint 还是整段文字的宽度；
// 表格里连 sizeHint 也要压成 0，否则它又会变成主机 / 用户列的基础宽度。
ColumnElidedLabel::ColumnElidedLabel(const QString &text, QWidget *parent)
    : ElidedLabel(text, parent) {}

QSize ColumnElidedLabel::sizeHint() const
{
    const QSize s = ElidedLabel::sizeHint();
    return QSize(0, s.height());
}

// ---------------------------------------------------------------- 工具 ---

void repolish(QWidget *w)
{
    if (!w)
        return;
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

QLabel *makeLabel(const QString &text, const char *role, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("role", QString::fromLatin1(role));
    return label;
}

QColor groupAccent(const ConnectionGroup &group)
{
    // nav-text-active 就是侧栏「当前页」那个蓝，压在深色轨道上最醒目；
    // 它在浅色内容区（连接列表的分组头）上也够深，两处都能用
    return group.color.isEmpty() ? tokenColor("nav-text-active") : QColor(group.color);
}

// ------------------------------------------------------------------ Card ---

Card::Card(QWidget *parent, bool flat)
    : QFrame(parent)
{
    setProperty("role", QStringLiteral("card"));
    if (flat)
        setProperty("flat", true);
}

// ---------------------------------------------------------- ElidedLabel ---

ElidedLabel::ElidedLabel(const QString &text, QWidget *parent)
    : QLabel(parent)
    , m_full(text)
{
    QLabel::setText(text);
}

void ElidedLabel::setFullText(const QString &text)
{
    if (m_full == text)
        return;
    m_full = text;
    applyElide();
}

QSize ElidedLabel::minimumSizeHint() const
{
    // 宽度不给下界，布局想压多小就多小；高度还得按文字算，
    // 不然这一行会塌下去。
    return QSize(0, QLabel::minimumSizeHint().height());
}

void ElidedLabel::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    applyElide();
}

void ElidedLabel::applyElide()
{
    // 宽度还没定（构造时是 0）就先放着全文，等 resize 再收
    if (width() <= 0)
        return;

    const QString shown = fontMetrics().elidedText(m_full, Qt::ElideRight, width());

    // 必须比对一下再设：setText 会触发 updateGeometry → 重新布局 → 又回到这里，
    // 内容稳定之后不再 setText，这个循环才会停下来。
    if (shown != QLabel::text())
        QLabel::setText(shown);
}

// ----------------------------------------------------------- EdgeShadow ---

EdgeShadow::EdgeShadow(Qt::Edge side, QWidget *parent)
    : QWidget(parent)
    , m_side(side)
{
    setObjectName(QStringLiteral("EdgeShadow"));
    // 纯装饰，鼠标事件该穿透到下面的内容区去
    setAttribute(Qt::WA_TransparentForMouseEvents);

    // 竖直的边固定宽、高度跟着容器走；水平的边反过来
    if (side == Qt::LeftEdge || side == Qt::RightEdge) {
        setFixedWidth(kEdgeShadowSize);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    } else {
        setFixedHeight(kEdgeShadowSize);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
}

void EdgeShadow::paintEvent(QPaintEvent *)
{
    const QColor shadow = tokenColor("edge-shadow");
    if (shadow.alpha() == 0)
        return;

    // 收尾那一段用完全透明的同色，不能直接写 Qt::transparent ——
    // 那是 (0,0,0,0)，渐变过去会带出一层灰。
    const QColor fade(shadow.red(), shadow.green(), shadow.blue(), 0);

    // 渐变总是从「贴着那一边」起步（最浓），往远处淡出
    QLinearGradient g;
    switch (m_side) {
    case Qt::RightEdge:  g = QLinearGradient(0, 0, width(), 0);  break;
    case Qt::LeftEdge:   g = QLinearGradient(width(), 0, 0, 0);  break;
    case Qt::BottomEdge: g = QLinearGradient(0, 0, 0, height()); break;
    default:             g = QLinearGradient(0, height(), 0, 0); break;   // TopEdge
    }

    // 不用线性衰减：真实的投影衰减得更快，中间补一档看着才像影子而不是色带
    g.setColorAt(0.0, shadow);
    g.setColorAt(0.4, QColor(shadow.red(), shadow.green(), shadow.blue(),
                             int(shadow.alpha() * 0.35)));
    g.setColorAt(1.0, fade);

    QPainter painter(this);
    painter.fillRect(rect(), g);
}

// ------------------------------------------------------------ ActionTile ---

ActionTile::ActionTile(Icon icon, const QString &title, const QString &desc, QWidget *parent)
    : QFrame(parent)
{
    setProperty("role", QStringLiteral("tile"));
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(104);

    // 必须显式给一个较小的最小宽度。不给的话，Qt 会按内容的 minimumSizeHint
    // 来算，而说明文字的自动换行算不出很小的最小值 —— 四张卡片并排就会把整栏
    // 撑到 1000px 以上，右侧面板被挤出窗口。显式设了之后，文字会在卡片里换行。
    setMinimumWidth(132);

    auto *iconBox = new QLabel(this);
    iconBox->setObjectName(QStringLiteral("TileIcon"));
    iconBox->setFixedSize(38, 38);
    iconBox->setAlignment(Qt::AlignCenter);
    iconBox->setPixmap(Icons::pixmap(icon, tokenColor("accent-text"), 20));

    auto *titleLabel = makeLabel(title, "tileTitle", this);
    auto *descLabel = makeLabel(desc, "tileDesc", this);
    descLabel->setWordWrap(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 14, 15, 14);
    layout->setSpacing(3);
    layout->addWidget(iconBox);
    layout->addSpacing(7);
    layout->addWidget(titleLabel);
    layout->addWidget(descLabel);
    layout->addStretch();
}

void ActionTile::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit clicked();
    QFrame::mouseReleaseEvent(event);
}

void ActionTile::enterEvent(QEnterEvent *event)
{
    setCursor(Qt::PointingHandCursor);
    QFrame::enterEvent(event);
}

void ActionTile::leaveEvent(QEvent *event)
{
    QFrame::leaveEvent(event);
}

// ------------------------------------------------------------ PillButton ---

PillButton::PillButton(const QString &text, Variant variant, QWidget *parent)
    : QPushButton(text, parent)
    , m_variant(variant)
{
    setProperty("role", QStringLiteral("pill"));
    setVariant(variant);
    setCursor(Qt::PointingHandCursor);
}

void PillButton::setVariant(Variant variant)
{
    m_variant = variant;
    static const char *names[] = { "primary", "ghost", "subtle", "danger" };
    setProperty("variant", QString::fromLatin1(names[int(variant)]));
    repolish(this);
    refreshIcon();
}

void PillButton::setLeadingIcon(Icon which, int size)
{
    m_icon = which;
    m_hasIcon = true;
    m_iconSize = size;
    refreshIcon();
}

void PillButton::makeCompact(Icon which, int size, const QString &tooltip)
{
    m_icon = which;
    m_hasIcon = true;
    m_iconSize = size;
    setText(QString());
    setFixedSize(30, 30);
    setProperty("compact", true);
    if (!tooltip.isEmpty())
        setToolTip(tooltip);
    refreshIcon();
    repolish(this);
}

void PillButton::refreshIcon()
{
    if (!m_hasIcon)
        return;

    QColor c;
    switch (m_variant) {
    case Primary: c = QColor(Qt::white);          break;
    case Danger:  c = tokenColor("err");          break;
    case Ghost:
    case Subtle:  c = tokenColor("text-dim");     break;
    }
    setIcon(statefulIcon(m_icon, c, tokenColor("text"), m_iconSize));
    setIconSize(QSize(m_iconSize, m_iconSize));
}

// ------------------------------------------------------------ SelectBox ---

namespace {

// 去掉列表项外面那圈框。
//
// 组合框弹开时，当前项只是「当前」不是「选中」，Fusion 会给它画一圈焦点框，
// 看起来就是套了个脏兮兮的黑边。这个框 **QSS 的 outline: none 关不掉**
// （试过，视图和 ::item 两处都写了，不管用）。把 State_HasFocus 摘掉再交给
// 默认绘制，样式就不会再画它了。
class PlainItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem plain(option);
        plain.state &= ~QStyle::State_HasFocus;
        QStyledItemDelegate::paint(painter, plain, index);
    }
};

// 箭头区的大小，和 app.qss 里 QComboBox::drop-down 的 width 是同一个值，
// 改一个要改另一个。
constexpr int kArrowZone = 30;
constexpr int kArrowSize = 14;

} // namespace

SelectBox::SelectBox(QWidget *parent)
    : QComboBox(parent)
{
    setCursor(Qt::PointingHandCursor);

    // 弹出列表的样式**必须挂到列表自己身上**，写进 app.qss 不管用。
    //
    // 试过三种写法，全局样式表一条都不生效：`QComboBox QAbstractItemView`（祖先链）、
    // `#ComboPopup`（objectName）、以及给 view 挂 objectName 再用 ID 选 —— 弹出列表
    // 挂在一个独立的弹出容器上，全局那条匹配链在这里靠不住。
    // 直接 setStyleSheet 到控件自己身上才稳。
    //
    // 颜色还是走 Theme::color() 取 token，不要在这里写死色值。
    const QString bg = Theme::color(QStringLiteral("bg-elevated")).name();
    const QString line = Theme::color(QStringLiteral("border-strong")).name();
    const QString text = Theme::color(QStringLiteral("text-dim")).name();
    // accent-soft 带 alpha（背景图要透上来），这里要保留它 ——
    // QColor::name() 会把 alpha 丢掉，选中项会变成一块刺眼的实心亮蓝。
    const QString selBg = Theme::color(QStringLiteral("accent-soft")).name(QColor::HexArgb);
    const QString selText = Theme::color(QStringLiteral("accent-text")).name();

    // 去掉当前项那圈焦点框，只能用 delegate，QSS 关不掉
    view()->setItemDelegate(new PlainItemDelegate(view()));

    // 弹出列表外面还套着一层 QComboBox 自己的弹出容器，它默认会画一圈方框。
    // 那个容器从外面拿不到（没有公开类型），只能顺着 view 的父控件摸过去，
    // 把底色和边框都设在它身上，里面那个列表就保持透明 —— 这样整块弹出
    // 只有一条边框，不会叠出「双线」。
    if (QWidget *container = view()->parentWidget()) {
        container->setStyleSheet(QStringLiteral("QFrame { background: %1;"
                                                "         border: 1px solid %2; }")
                                     .arg(bg, line));
    }

    view()->setStyleSheet(
        QStringLiteral("QListView {"
                       "  background: transparent;"   // 底色交给外面那层容器
                       "  border: none;"
                       "  padding: 5px;"
                       "  outline: none;"
                       "  color: %1;"
                       "}"
                       "QListView::item {"
                       "  min-height: 30px;"
                       "  padding-left: 10px;"
                       "  padding-right: 10px;"
                       "  border-radius: 6px;"
                       "  color: %1;"
                       "}"
                       "QListView::item:selected {"
                       "  background: %2;"
                       "  color: %3;"
                       "}")
            .arg(text, selBg, selText));
}

// 弹出列表的行距必须在**每次弹出之前**压成 0，否则相邻两项会重叠。
//
// 现象：每一项自己的矩形是按 QSS 的 min-height: 30px 算的，但排行用的间距
// 取的是「未设」状态的样式默认值，算出来是负的 —— 每项高 30、相邻两项的 y
// 却只差 16，于是三条文字直接叠在一起，弹出列表上半截挤成一团、下半截空着
// 一大块。
//
// 为什么不能写在构造函数里：那时候设过之后，样式表还要再处理一轮 view
// （挂 QSS 本身也算），这一步会把这个间距打回样式默认值 —— 构造函数里写了
// 等于白写，实测一点用都没有。所以放到弹出前一刻设，这时候没人再动它了。
void SelectBox::showPopup()
{
    if (auto *list = qobject_cast<QListView *>(view()))
        list->setSpacing(0);
    QComboBox::showPopup();
}

void SelectBox::paintEvent(QPaintEvent *event)
{
    QComboBox::paintEvent(event);

    const QColor color = m_hover ? tokenColor("text-dim") : tokenColor("text-mute");
    const int x = width() - kArrowZone + (kArrowZone - kArrowSize) / 2;
    const int y = (height() - kArrowSize) / 2;

    QPainter p(this);
    p.drawPixmap(x, y, Icons::pixmap(Icon::ChevronDown, color, kArrowSize));
}

void SelectBox::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QComboBox::enterEvent(event);
}

void SelectBox::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QComboBox::leaveEvent(event);
}

// ------------------------------------------------------------- NavButton ---

NavButton::NavButton(Icon icon, const QString &text, QWidget *parent)
    : QPushButton(parent)
    , m_icon(icon)
{
    buildUi(text);
    refreshState();
}

NavButton::NavButton(const QColor &dotColor, const QString &text, QWidget *parent)
    : QPushButton(parent)
    , m_dotMode(true)
{
    buildUi(text);
    setDotColor(dotColor);
    refreshState();
}

void NavButton::buildUi(const QString &text)
{
    m_text = text;

    setProperty("role", QStringLiteral("nav"));
    setCursor(Qt::PointingHandCursor);
    setCheckable(false);
    setFocusPolicy(Qt::NoFocus);

    // 三个孩子都用 WA_TransparentForMouseEvents：点击要落到按钮本体上，
    // 不然鼠标压在图标或数字上时按钮收不到 press。
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(18, 18);
    m_iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    if (m_dotMode) {
        // 分组名可能很长，得用 ElidedLabel：普通 QLabel 的最小宽度按整段文字算，
        // 在布局里压不下去，会把右边的数字顶出侧栏
        auto *label = new ElidedLabel(text, this);
        label->setProperty("role", QStringLiteral("navText"));
        m_textLabel = label;
    } else {
        m_textLabel = makeLabel(text, "navText", this);
    }
    m_textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_countLabel = new QLabel(this);
    m_countLabel->setObjectName(QStringLiteral("NavCount"));
    m_countLabel->setAlignment(Qt::AlignCenter);
    m_countLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_countLabel->hide();

    // 左边距 11，加外层 SideBar 的 10 = 图标落在距侧栏左边 21 的位置上，
    // 正好和分组标题（QSS 里 padding-left: 11）对齐。
    // 圆点模式沿用的是**同一个 18x18 的槽**，圆点靠 QSS 的 margin:5px 缩成 8x8，
    // 中心自然还落在图标中心那条竖线上 —— 所以这里不用改边距。
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(11, 0, 10, 0);
    layout->setSpacing(0);
    layout->addWidget(m_iconLabel);
    layout->addSpacing(10);          // 图标和文字之间留一口气
    layout->addWidget(m_textLabel);
    layout->addStretch();
    layout->addWidget(m_countLabel);
}

void NavButton::setDotColor(const QColor &color)
{
    if (!m_dotMode || !m_iconLabel)
        return;

    // margin:5px 把 18x18 的背景盒缩成 8x8 并居中，border-radius:4 就是正圆。
    // 和 ConnectionRow 的在线状态点用的是同一种画法。
    m_iconLabel->setStyleSheet(QStringLiteral("background-color:%1; margin:5px; border-radius:4px;")
                                   .arg(color.name()));
}

QString NavButton::labelText() const
{
    // 不能用 m_textLabel->text()：ElidedLabel 会把显示文字改成带省略号的版本
    return m_text;
}

void NavButton::setCount(int count, bool hideWhenZero)
{
    if (count <= 0 && hideWhenZero) {
        m_countLabel->hide();
        return;
    }
    m_countLabel->setText(QString::number(count));
    m_countLabel->show();
}

void NavButton::setUpdateBadge(const QString &text)
{
    if (!m_countLabel)
        return;

    if (text.isEmpty()) {
        m_countLabel->setProperty("badge", QVariant());
        repolish(m_countLabel);
        m_countLabel->hide();
        return;
    }

    m_countLabel->setText(text);
    m_countLabel->setProperty("badge", QStringLiteral("update"));
    repolish(m_countLabel);
    m_countLabel->show();
}

void NavButton::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    setProperty("active", active);
    repolish(this);
    refreshState();
}

void NavButton::enterEvent(QEnterEvent *event)
{
    m_hover = true;

    // 悬浮底色**必须自己 repolish**，不能交给 QSS 的 :hover —— 伪状态切背景时
    // Qt 只重画按钮本体，文字 QLabel 占的那块会留着上一帧的底色（悬浮或右键
    // 某一行时，文字背后会出现一个和行底色不一样的方框）。
    // 挂 hover 属性走和 active 同一条路，子控件会跟着重画。
    setProperty("hover", true);
    repolish(this);

    refreshState();
    QPushButton::enterEvent(event);
}

void NavButton::leaveEvent(QEvent *event)
{
    m_hover = false;
    setProperty("hover", false);
    repolish(this);

    refreshState();
    QPushButton::leaveEvent(event);
}

void NavButton::refreshState()
{
    // 图标颜色和文字保持同一档：正常情况下图标比文字浅的话，在白底侧栏上
    // 会显得整列都在发虚，反而看不清画的是什么。
    // 图标颜色和文字走同一套 nav-* token（它们是侧栏专用的，见 Theme.cpp）
    QColor color;
    if (m_active)
        color = tokenColor("nav-text-active");
    else if (m_hover)
        color = tokenColor("nav-text-hover");
    else
        color = tokenColor("nav-text");

    // 圆点模式不设 pixmap —— 设了会把 QSS 画的那个圆点整个盖住。
    // 圆点的颜色只由 setDotColor() 决定，不跟着 hover / active 变。
    if (!m_dotMode)
        m_iconLabel->setPixmap(Icons::pixmap(m_icon, color, 18));

    // 文字和数字的颜色**不能**交给 QSS 的祖先选择器，比如
    //     QPushButton[role="nav"][active="true"] QLabel[role="navText"] { color: ... }
    // 这条规则看着没问题，但它有个隐蔽的毛病：按钮的 active 属性变了之后，
    // Qt 不会重新解析子控件的样式，repolish(按钮) 也不会传到子控件身上。
    // 结果是切页之后**旧的那一项文字还是蓝的**，而图标（在代码里设的）已经
    // 恢复正常了 —— 半边对半边错，特别容易看漏。
    //
    // 所以改成每个子控件自己带一个 state 属性、自己 repolish 自己：
    // 规则只依赖控件自身的属性，不存在需要往外层找的问题。
    const char *state = m_active ? "active" : (m_hover ? "hover" : "normal");
    for (QLabel *label : { m_textLabel, m_countLabel }) {
        label->setProperty("state", QString::fromLatin1(state));
        repolish(label);
    }
}

// ---------------------------------------------------------- WindowButton ---

WindowButton::WindowButton(Icon icon, const QString &role, QWidget *parent)
    : QPushButton(parent)
    , m_icon(icon)
{
    setProperty("role", QStringLiteral("winbtn"));
    setProperty("kind", role);
    setFixedSize(44, 28);   // 普通的长方形，和常见桌面工具一致
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    setIconSize(QSize(13, 13));
    refreshIcon();
}

void WindowButton::refreshIcon()
{
    // 平时灰、悬浮纯白。关闭按钮的红色只留在 hover 背景上，图标不跟着变红。
    setIcon(Icons::icon(m_icon,
                        tokenColor(m_hover ? "titlebar-icon-hover" : "brand-sub"),
                        13));
}

void WindowButton::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    refreshIcon();
    QPushButton::enterEvent(event);
}

void WindowButton::leaveEvent(QEvent *event)
{
    m_hover = false;
    refreshIcon();
    QPushButton::leaveEvent(event);
}

// ----------------------------------------------------------- GroupHeader ---

GroupHeader::GroupHeader(const QString &name, int count, const QColor &color, QWidget *parent)
    : QFrame(parent)
{
    setProperty("role", QStringLiteral("groupHeader"));
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(34);

    m_chevron = new QLabel(this);
    m_chevron->setFixedSize(14, 14);
    m_chevron->setPixmap(Icons::pixmap(Icon::ChevronDown, tokenColor("text-mute"), 14));

    auto *folder = new QLabel(this);
    folder->setFixedSize(17, 17);
    folder->setPixmap(Icons::pixmap(Icon::Folder, color, 17));

    m_name = makeLabel(name, "groupName", this);
    m_count = makeLabel(QStringLiteral("(%1)").arg(count), "groupCount", this);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 10, 0);
    layout->setSpacing(7);
    layout->addWidget(m_chevron);
    layout->addWidget(folder);
    layout->addWidget(m_name);
    layout->addWidget(m_count);
    layout->addStretch();
}

void GroupHeader::setExpanded(bool expanded)
{
    if (m_expanded == expanded)
        return;
    m_expanded = expanded;
    m_chevron->setPixmap(Icons::pixmap(expanded ? Icon::ChevronDown : Icon::ChevronRight,
                                       tokenColor("text-mute"), 14));
}

void GroupHeader::setCount(int count)
{
    m_count->setText(QStringLiteral("(%1)").arg(count));
}

void GroupHeader::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        setExpanded(!m_expanded);
        emit toggled(m_expanded);
    }
    QFrame::mouseReleaseEvent(event);
}

// ------------------------------------------------------ ConnectionListHeader ---

ConnectionListHeader::ConnectionListHeader(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ListHeader"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(34);

    // 横向 Ignored：列宽完全由 stretch 比例决定，不受文字长短影响 ——
    // 否则表头「名称」两个字和行里完整名字的 sizeHint 差太多，两边列宽会错开。
    const auto column = [this](const QString &text, int stretch) {
        auto *label = new ColumnLabel(text, this);
        label->setProperty("role", QStringLiteral("columnHeader"));
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        return std::make_pair(label, stretch);
    };

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(kRowSidePadding, 0, kRowSidePadding, 0);
    layout->setSpacing(kColumnSpacing);

    const auto add = [&layout](const std::pair<QLabel *, int> &item) {
        layout->addWidget(item.first, item.second);
    };

    add(column(QStringLiteral("名称"),     kColName));
    add(column(QStringLiteral("主机"),     kColHost));
    add(column(QStringLiteral("端口"),     kColPort));
    add(column(QStringLiteral("用户"),     kColUser));
    add(column(QStringLiteral("状态"),     kColStatus));
    add(column(QStringLiteral("延迟"),     kColLatency));
    add(column(QStringLiteral("最近连接"), kColLast));

    // 和行里那块固定宽度的操作区对齐。**必须用固定宽度的 QWidget，不能用
    // addSpacing()**：addSpacing 放在最后一项时，布局不把右边距算进去，
    // 所有列会多摊到 12px（实测列位置整体右移，和行对不上）。
    auto *actionSpace = new QWidget(this);
    actionSpace->setFixedWidth(kActionAreaWidth);
    layout->addWidget(actionSpace);
}

// --------------------------------------------------------- ConnectionRow ---

ConnectionRow::ConnectionRow(const Connection &connection, const QString &groupName,
                             const QColor &groupColor, QWidget *parent)
    : QFrame(parent)
    , m_connection(connection)
    , m_groupName(groupName)
{
    setProperty("role", QStringLiteral("row"));
    setFixedHeight(kRowHeight);
    setCursor(Qt::PointingHandCursor);

    // 名称列：分组色点 + 名字。分组名不进这一列（只在 tooltip 里），
    // 否则名字长一点就被后缀挤得只剩省略号。
    m_groupDot = new QLabel(this);
    m_groupDot->setFixedSize(8, 8);
    m_groupDot->setStyleSheet(QStringLiteral("background-color:%1; border-radius:4px;")
                                  .arg(groupColor.name()));

    m_name = new ColumnElidedLabel(connection.name, this);
    m_name->setProperty("role", QStringLiteral("rowTitle"));

    // 每一列都设成横向 Ignored：列宽只由 stretch 比例决定，表头和行才会严格对齐
    const auto ignoreHint = [](QWidget *w) {
        w->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    };

    auto *nameColumn = new ColumnCell(this);
    ignoreHint(nameColumn);
    auto *nameLayout = new QHBoxLayout(nameColumn);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(6);
    nameLayout->addWidget(m_groupDot);
    nameLayout->addWidget(m_name, 1);

    // 主机 / 端口 / 用户
    m_host = new ColumnElidedLabel(connection.host, this);
    m_host->setProperty("role", QStringLiteral("rowHost"));
    m_host->setFont(Theme::monoFont(12));
    ignoreHint(m_host);

    m_port = new ColumnLabel(QString::number(connection.port), this);
    m_port->setProperty("role", QStringLiteral("rowPort"));
    m_port->setFont(Theme::monoFont(12));
    ignoreHint(m_port);

    m_user = new ColumnElidedLabel(connection.username, this);
    m_user->setProperty("role", QStringLiteral("rowUser"));
    ignoreHint(m_user);

    // 状态：小圆点 + 文字
    auto *statusColumn = new ColumnCell(this);
    ignoreHint(statusColumn);
    auto *statusLayout = new QHBoxLayout(statusColumn);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(6);
    m_statusDot = new QLabel(statusColumn);
    m_statusDot->setFixedSize(6, 6);
    m_statusText = makeLabel(QString(), "rowStatus", statusColumn);
    statusLayout->addWidget(m_statusDot);
    statusLayout->addWidget(m_statusText);
    statusLayout->addStretch();

    m_latency = new ColumnLabel(QStringLiteral("—"), this);
    m_latency->setProperty("role", QStringLiteral("rowLatency"));
    m_latency->setFont(Theme::monoFont(12));
    ignoreHint(m_latency);

    m_lastUsed = new ColumnLabel(QString(), this);
    m_lastUsed->setProperty("role", QStringLiteral("rowLast"));
    ignoreHint(m_lastUsed);

    // 收藏 / 连接 / 更多。三个 30px 按钮，放进固定宽度的容器里，
    // 这样列宽不会因为按钮的状态变化。
    m_star = new QPushButton(this);
    m_star->setProperty("role", QStringLiteral("icon"));
    m_star->setProperty("star", true);
    m_star->setFixedSize(kActionButton, kActionButton);
    m_star->setFocusPolicy(Qt::NoFocus);
    m_star->setCursor(Qt::PointingHandCursor);

    // 连接：纯图标按钮，和星标 / 更多同一套外观。之前那个实心蓝方块太重了。
    m_connect = new QPushButton(this);
    m_connect->setProperty("role", QStringLiteral("icon"));
    m_connect->setFixedSize(kActionButton, kActionButton);
    m_connect->setFocusPolicy(Qt::NoFocus);
    m_connect->setCursor(Qt::PointingHandCursor);
    m_connect->setIcon(statefulIcon(Icon::Play, tokenColor("text-dim"),
                                    tokenColor("nav-text-active"), 14));
    m_connect->setIconSize(QSize(14, 14));
    m_connect->setToolTip(QStringLiteral("连接"));

    m_more = new QPushButton(this);
    m_more->setProperty("role", QStringLiteral("icon"));
    m_more->setFixedSize(kActionButton, kActionButton);
    m_more->setFocusPolicy(Qt::NoFocus);
    m_more->setIcon(statefulIcon(Icon::More, tokenColor("text-mute"), tokenColor("text"), 16));
    m_more->setIconSize(QSize(16, 16));
    m_more->setCursor(Qt::PointingHandCursor);

    auto *actions = new QWidget(this);
    actions->setFixedWidth(kActionAreaWidth);
    auto *actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(kActionSpacing);
    actionsLayout->addWidget(m_star);
    actionsLayout->addWidget(m_connect);
    actionsLayout->addWidget(m_more);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(kRowSidePadding, 0, kRowSidePadding, 0);
    layout->setSpacing(kColumnSpacing);
    layout->addWidget(nameColumn, kColName);
    layout->addWidget(m_host, kColHost);
    layout->addWidget(m_port, kColPort);
    layout->addWidget(m_user, kColUser);
    layout->addWidget(statusColumn, kColStatus);
    layout->addWidget(m_latency, kColLatency);
    layout->addWidget(m_lastUsed, kColLast);
    layout->addWidget(actions);

    connect(m_connect, &QPushButton::clicked, this, [this] {
        emit connectRequested(m_connection.id);
    });
    connect(m_star, &QPushButton::clicked, this, [this] {
        emit favoriteToggled(m_connection.id);
    });
    connect(m_more, &QPushButton::clicked, this, [this] {
        QMenu menu(this);

        QAction *open = menu.addAction(Icons::icon(Icon::Terminal, tokenColor("text-dim"), 16),
                                       QStringLiteral("打开终端"));
        QAction *edit = menu.addAction(Icons::icon(Icon::Edit, tokenColor("text-dim"), 16),
                                       QStringLiteral("编辑"));
        QAction *copy = menu.addAction(Icons::icon(Icon::Copy, tokenColor("text-dim"), 16),
                                       QStringLiteral("复制主机地址"));
        menu.addSeparator();
        QAction *del = menu.addAction(Icons::icon(Icon::Trash, tokenColor("err"), 16),
                                      QStringLiteral("删除"));

        QAction *picked = menu.exec(m_more->mapToGlobal(QPoint(-110, m_more->height() + 4)));
        if (!picked)
            return;

        if (picked == open)       emit enterTerminalRequested(m_connection.id);
        else if (picked == edit)  emit editRequested(m_connection.id);
        else if (picked == copy)  emit copyHostRequested(m_connection.id);
        else if (picked == del)   emit deleteRequested(m_connection.id);
    });

    refresh();
    // 先显示「检测中…」；列表真正显示出来之后 ConnectionList 会发起探测
    setProbeStatus(false, -1, true);
}

void ConnectionRow::setConnection(const Connection &c)
{
    m_connection = c;
    refresh();
}

void ConnectionRow::setProbeStatus(bool online, int latencyMs, bool probing)
{
    m_probing = probing;
    m_online = online;
    m_latencyMs = online ? latencyMs : -1;

    const char *dotToken = probing ? "text-mute" : (online ? "ok" : "text-mute");
    m_statusDot->setStyleSheet(QStringLiteral("background-color:%1; border-radius:3px;")
                                   .arg(tokenColor(dotToken).name()));
    m_statusDot->setToolTip(QStringLiteral("基于 TCP 端口连通性检测"));

    if (probing)
        m_statusText->setText(QStringLiteral("检测中…"));
    else
        m_statusText->setText(online ? QStringLiteral("在线") : QStringLiteral("离线"));
    m_statusText->setProperty("online", !probing && online);
    repolish(m_statusText);

    QString level = QStringLiteral("na");
    if (!probing && online && latencyMs >= 0) {
        m_latency->setText(QStringLiteral("%1 ms").arg(latencyMs));
        level = latencyMs < 50 ? QStringLiteral("good")
                               : (latencyMs < 150 ? QStringLiteral("mid")
                                                  : QStringLiteral("poor"));
    } else {
        m_latency->setText(QStringLiteral("—"));
    }
    m_latency->setProperty("level", level);
    repolish(m_latency);
}

void ConnectionRow::setActive(bool active)
{
    setProperty("active", active);
    repolish(this);
}

void ConnectionRow::refresh()
{
    m_name->setFullText(m_connection.name.isEmpty() ? m_connection.host : m_connection.name);

    m_host->setFullText(m_connection.host);
    m_port->setText(QString::number(m_connection.port));
    m_user->setFullText(m_connection.username.isEmpty() ? QStringLiteral("—")
                                                        : m_connection.username);
    m_lastUsed->setText(humanizeSince(m_connection.lastUsedAt));

    m_star->setIcon(Icons::icon(Icon::Star,
                                tokenColor(m_connection.favorite ? "warn" : "text-mute"), 16));
    m_star->setIconSize(QSize(16, 16));
    m_star->setToolTip(m_connection.favorite ? QStringLiteral("取消收藏")
                                            : QStringLiteral("加入收藏"));

    // 表格里放不下的信息（标签、备注、认证方式）收进 tooltip
    QStringList tip;
    tip << m_connection.identity();
    tip << (m_connection.auth == AuthType::Password ? QStringLiteral("密码认证")
                                                    : QStringLiteral("公钥认证"));
    if (!m_groupName.isEmpty())
        tip << QStringLiteral("分组：%1").arg(m_groupName);
    if (!m_connection.tags.isEmpty())
        tip << QStringLiteral("标签：%1").arg(m_connection.tags.join(QLatin1Char(' ')));
    if (!m_connection.note.isEmpty())
        tip << m_connection.note;
    setToolTip(tip.join(QLatin1Char('\n')));
}

void ConnectionRow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit connectRequested(m_connection.id);
    QFrame::mouseDoubleClickEvent(event);
}

// --------------------------------------------------------- ConnectionList ---

ConnectionList::ConnectionList(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    // 表格行之间不留缝：分隔靠每行自己的底边线
    m_layout->setSpacing(0);

    m_probe = new ConnectionProbe(this);
    connect(m_probe, &ConnectionProbe::finished, this, &ConnectionList::onProbeFinished);
}

void ConnectionList::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 切回这一页时补一次。requestProbes 自己有 60 秒缓存，不会反复打。
    requestProbes();
}

void ConnectionList::requestProbes()
{
    if (!m_probe || m_visibleConnections.isEmpty())
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const Connection &c : m_visibleConnections) {
        const QString signature =
            c.host + QLatin1Char(':') + QString::number(c.port);

        const ProbeInfo cached = m_probeCache.value(c.id);
        if (cached.valid && cached.signature == signature
            && now - cached.checkedAt < 60000) {
            continue;   // 一分钟内测过，直接用缓存
        }

        if (ConnectionRow *row = m_rows.value(c.id))
            row->setProbeStatus(false, -1, true);
        m_probe->probe(c.id, c.host, quint16(c.port));
    }
}

void ConnectionList::onProbeFinished(const QString &id, bool online, int latencyMs)
{
    const Connection *found = nullptr;
    for (const Connection &c : m_visibleConnections) {
        if (c.id == id) {
            found = &c;
            break;
        }
    }
    if (!found && m_store)
        found = m_store->find(id);
    if (!found && m_useStatic) {
        for (const Connection &c : m_static) {
            if (c.id == id) {
                found = &c;
                break;
            }
        }
    }

    ProbeInfo info;
    info.valid = true;
    info.online = online;
    info.latencyMs = latencyMs;
    info.checkedAt = QDateTime::currentMSecsSinceEpoch();
    if (found)
        info.signature = found->host + QLatin1Char(':') + QString::number(found->port);
    m_probeCache.insert(id, info);

    if (ConnectionRow *row = m_rows.value(id))
        row->setProbeStatus(online, latencyMs, false);
}

void ConnectionList::setStore(ConnectionStore *store)
{
    if (m_store == store)
        return;

    if (m_store)
        m_store->disconnect(this);

    m_store = store;
    m_useStatic = false;

    if (m_store) {
        connect(m_store, &ConnectionStore::connectionsChanged, this, &ConnectionList::refresh);
        connect(m_store, &ConnectionStore::groupsChanged, this, &ConnectionList::refresh);
    }
    rebuild();
}

void ConnectionList::setStaticList(const QVector<Connection> &list)
{
    m_static = list;
    m_useStatic = true;
    rebuild();
}

void ConnectionList::setGrouped(bool grouped)
{
    if (m_grouped == grouped)
        return;
    m_grouped = grouped;
    rebuild();
}

void ConnectionList::setFavoritesOnly(bool only)
{
    if (m_favoritesOnly == only)
        return;
    m_favoritesOnly = only;
    rebuild();
}

void ConnectionList::setFilter(const QString &keyword)
{
    if (m_filter == keyword)
        return;
    m_filter = keyword;
    rebuild();
}

void ConnectionList::setEmptyState(const QString &title, const QString &hint, Icon icon)
{
    if (m_emptyTitle == title && m_emptyHint == hint && m_emptyIcon == icon)
        return;
    m_emptyTitle = title;
    m_emptyHint = hint;
    m_emptyIcon = icon;
    rebuild();
}

void ConnectionList::refresh()
{
    rebuild();
}

void ConnectionList::rebuild()
{
    // 先把旧的行拆掉
    m_rows.clear();
    m_visibleConnections.clear();
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    // ---- 取数据 ----
    QVector<Connection> list;
    if (m_useStatic) {
        list = m_static;
    } else if (m_store) {
        list = m_store->connections();
    }

    if (m_favoritesOnly) {
        QVector<Connection> filtered;
        for (const Connection &c : list) {
            if (c.favorite)
                filtered << c;
        }
        list = filtered;
    }

    if (!m_filter.trimmed().isEmpty()) {
        const QString needle = m_filter.trimmed().toLower();
        QVector<Connection> filtered;
        for (const Connection &c : list) {
            if (c.searchBlob().contains(needle))
                filtered << c;
        }
        list = filtered;
    }

    // 给外面（页头那个「N 台」）一个准数：这里已经过完收藏/关键字筛选了，
    // 页面不要再自己算一遍
    m_visibleCount = list.size();
    m_visibleConnections = list;

    if (list.isEmpty()) {
        // 空状态不是一句孤零零的灰字：一个图标底座 + 标题 + 一句怎么办。
        // 有关键字时先说「没匹配上」—— 那比「这个分组是空的」更接近用户看到的情况。
        QString title;
        QString hint;
        Icon icon = m_emptyIcon;
        if (!m_filter.trimmed().isEmpty()) {
            icon = Icon::Search;
            title = QStringLiteral("没有匹配的连接");
            hint = QStringLiteral("换个关键字试试，或者把筛选条件清掉。");
        } else if (!m_emptyTitle.isEmpty()) {
            title = m_emptyTitle;
            hint = m_emptyHint;
        } else {
            title = QStringLiteral("还没有连接");
            hint = QStringLiteral("点左上角的「新建连接」添加第一台服务器，也可以按 Ctrl+N。");
        }

        auto *box = new QWidget(this);
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(24, 40, 24, 40);
        boxLayout->setSpacing(0);

        // 只放一个细线条图标，**不加底座背景** —— 一块灰色方块压在深色底上
        // 反而比空白更显眼（原来那个 56px 的圆角底座就是这么被否掉的）。
        auto *iconLabel = new QLabel(box);
        iconLabel->setFixedSize(36, 36);
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setPixmap(Icons::pixmap(icon, tokenColor("text-mute"), 36));
        boxLayout->addWidget(iconLabel, 0, Qt::AlignHCenter);
        boxLayout->addSpacing(16);

        auto *titleLabel = makeLabel(title, "emptyTitle", box);
        titleLabel->setAlignment(Qt::AlignCenter);
        boxLayout->addWidget(titleLabel);
        boxLayout->addSpacing(6);

        // 不加 wordWrap：QLabel 在布局里算「换行后高度」时拿的未必是 QSS 之后的字体，
        // 结果经常只按一行高来分配，第二行被裁掉（文字莫名其妙少了半截）。
        // 这几句提示都不长，一行放得下；窗口最窄 1080，内容区也够宽。
        auto *hintLabel = makeLabel(hint, "emptyHint", box);
        hintLabel->setAlignment(Qt::AlignCenter);
        boxLayout->addWidget(hintLabel, 0, Qt::AlignHCenter);

        // 空状态在这块区域里居中，但**不做严格的几何居中**：上面还有页头，
        // 严格对半在观感上会显得偏下。用 4:5 的权重把它往上提一点。
        // 列表要有额外高度这两条才有效（ConnectionsPage 把它设成了 stretch=1）。
        m_layout->addStretch(4);
        m_layout->addWidget(box);
        m_layout->addStretch(5);
        return;
    }

    // ---- 建行 ----
    //
    // m_grouped = 按分组排序。**不插分组标题，也不在名字里显示分组名**：
    // 分组归属靠名字前面那个色点（以及 tooltip）表达，表格保持干净。
    const auto groupNameFor = [this](const Connection &c) -> QString {
        if (!m_grouped || !m_store || c.groupId.isEmpty())
            return QString();
        const ConnectionGroup *group = m_store->findGroup(c.groupId);
        return group ? group->name : QString();
    };
    const auto groupColorFor = [this](const Connection &c) -> QColor {
        if (m_store && !c.groupId.isEmpty()) {
            if (const ConnectionGroup *group = m_store->findGroup(c.groupId))
                return groupAccent(*group);
        }
        return tokenColor("text-mute");
    };

    const auto makeRow = [this, &groupNameFor, &groupColorFor](const Connection &c) {
        auto *row = new ConnectionRow(c, groupNameFor(c), groupColorFor(c), this);

        // 有缓存（且地址没变）就直接显示上次的探测结果；
        // 否则先保持构造函数里的「检测中…」，显示到本页时再去测。
        const ProbeInfo cached = m_probeCache.value(c.id);
        if (cached.valid
            && cached.signature == c.host + QLatin1Char(':') + QString::number(c.port)) {
            row->setProbeStatus(cached.online, cached.latencyMs, false);
        }
        m_rows.insert(c.id, row);

        connect(row, &ConnectionRow::connectRequested, this, &ConnectionList::connectRequested);
        connect(row, &ConnectionRow::editRequested, this, &ConnectionList::editRequested);
        connect(row, &ConnectionRow::deleteRequested, this, &ConnectionList::deleteRequested);
        connect(row, &ConnectionRow::favoriteToggled, this, &ConnectionList::favoriteToggled);
        connect(row, &ConnectionRow::copyHostRequested, this, &ConnectionList::copyHostRequested);
        connect(row, &ConnectionRow::enterTerminalRequested,
                this, &ConnectionList::enterTerminalRequested);
        return row;
    };

    if (!m_grouped || !m_store) {
        for (const Connection &c : list)
            m_layout->addWidget(makeRow(c));
        m_layout->addStretch();
        if (isVisible())
            requestProbes();
        return;
    }

    // 平铺，但按 store 里的分组顺序排；未分组放最后
    for (const ConnectionGroup &group : m_store->groups()) {
        for (const Connection &c : list) {
            if (c.groupId == group.id)
                m_layout->addWidget(makeRow(c));
        }
    }
    for (const Connection &c : list) {
        if (c.groupId.isEmpty() || !m_store->findGroup(c.groupId))
            m_layout->addWidget(makeRow(c));
    }

    m_layout->addStretch();
    if (isVisible())
        requestProbes();
}

} // namespace Em
