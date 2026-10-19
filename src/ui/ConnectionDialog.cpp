#include "ui/ConnectionDialog.h"

#include "ui/ConnectionFormPanel.h"
#include "ui/IconProvider.h"
#include "ui/Widgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QScreen>
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

} // namespace

ConnectionDialog::ConnectionDialog(ConnectionStore *store, KeyStore *keys, QWidget *parent)
    : QDialog(parent)
{
    // 用自绘头部，不要系统标题栏：
    //   - 系统标题栏的颜色跟 App 的深色主题对不上，一块浅色横在顶上很突兀；
    //   - 自绘之后标题、说明和关闭按钮能和表单用同一套字号 / 颜色。
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setSizeGripEnabled(false);
    setMinimumWidth(560);

    // 无边框窗口的圆角在 Win11 上默认会丢，要显式设 DwmSetWindowAttribute。
    // **但不能在这个构造函数里调 winId()** —— 此时对话框还没显示，QWidget 树里
    // 又挂在某个页面控件底下，先建原生句柄会让 Qt 沿着控件树给兄弟控件一路补建
    // 原生窗口，主窗口被切成一堆原生子窗口之后 WM_NCHITTEST 就到不了 MainWindow
    // 了 —— 表现是「开过一次对话框再关掉，标题栏就再也拖不动」。
    // 所以放到 showEvent 里，那时窗口已经由正常的 show 流程建好，winId() 只是
    // 取回已有的句柄（见 showEvent）。

    m_form = new ConnectionFormPanel(store, keys, this);
    // 面板自己那套「标题 + 收起按钮」是给抽屉用的，弹窗里有自己的头部
    m_form->setHeaderVisible(false);

    connect(m_form, &ConnectionFormPanel::cancelled, this, &QDialog::reject);
    connect(m_form, &ConnectionFormPanel::saved, this, [this](const Connection &c) {
        m_result = c;
        accept();
    });

    // ---- 头部 ----
    m_title = makeLabel(QStringLiteral("新建连接"), "dialogTitle", this);
    m_subtitle = makeLabel(QString(), "hint", this);

    auto *closeButton = new PillButton(QString(), PillButton::Ghost, this);
    closeButton->makeCompact(Icon::Close, 13, QStringLiteral("关闭"));
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    titleRow->addWidget(m_title);
    titleRow->addStretch();
    titleRow->addWidget(closeButton);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("DialogHeader"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto *headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(22, 16, 16, 14);
    headerLayout->setSpacing(3);
    headerLayout->addLayout(titleRow);
    headerLayout->addWidget(m_subtitle);

    // ---- 组装 ----
    // 根容器负责那圈 1px 描边（无边框窗口得自己补，和主窗口的 #Root 一个道理）
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("DialogRoot"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(header);
    rootLayout->addWidget(m_form, 1);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(root);

    setHeader(QStringLiteral("新建连接"), QString());

    // 高度按整张表单来，别写死 —— 表单以后还会加字段。
    // 不能用 adjustSize()：那用的是 QScrollArea 被 Qt 削过的 sizeHint，
    // 出来的窗口会矮一截，最后两个字段要滚才看得见（见 formContentHeight）。
    // +16 是 QScrollArea 自己的内缩（实测视口比分配高度少 13px）。
    // 不加的话窗口会差十来个像素，右边冒出一条滚动条、最后一个字段被切掉半行 ——
    // 看起来像布局坏了，其实只是这里少算了一点。
    const int contentHeight =
        m_form->formContentHeight() + header->sizeHint().height() + 2 + 16;
    resize(560, contentHeight);

    // 屏幕矮的时候别把窗口顶出去 —— 中间那块本来就会滚
    const QRect room = screen()->availableGeometry();
    resize(qMin(width(), room.width() - 60), qMin(height(), room.height() - 60));
}

void ConnectionDialog::setHeader(const QString &title, const QString &subtitle)
{
    m_title->setText(title);
    m_subtitle->setText(subtitle);
    m_subtitle->setVisible(!subtitle.isEmpty());
}

void ConnectionDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

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

void ConnectionDialog::mousePressEvent(QMouseEvent *event)
{
    // 自绘头部之后窗口没有系统标题栏可拖，这里补回来。
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
                     QStringLiteral("填写服务器的连接信息，保存后即可使用。"));
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
                     QStringLiteral("填写服务器的连接信息，保存后即可使用。"));
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
    dialog.setWindowTitle(QStringLiteral("编辑连接"));
    dialog.setHeader(QStringLiteral("编辑连接"),
                     QStringLiteral("修改这条连接的信息，保存后立即生效。"));
    dialog.m_form->startEdit(existing);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (out)
        *out = dialog.m_result;
    return true;
}

} // namespace Em
