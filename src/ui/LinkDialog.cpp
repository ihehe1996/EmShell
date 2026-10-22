#include "ui/LinkDialog.h"

#include "app/Theme.h"
#include "ui/IconProvider.h"
#include "ui/Widgets.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <functional>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

namespace Em {

namespace {

// DWM 属性编号。数值以 Windows SDK 为准（Win11 22000+），
// 和 MainWindow / ConnectionDialog 里是同一套。
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmBorderColor          = 34;
constexpr DWORD kDwmCaptionColor         = 35;
constexpr DWORD kDwmTextColor            = 36;

constexpr int kRowHeight = 40;
constexpr int kMaxListHeight = 232;
constexpr int kDeleteButton = 24;

// 列表里的一条链接。整行可点（读回输入框编辑），行尾的垃圾桶直接删。
// 用 std::function 而不是信号，和 HistoryPage 的 HistoryRow 是同一个路子。
class LinkRow : public QFrame
{
public:
    LinkRow(const ConnectionLink &link, QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setProperty("role", QStringLiteral("row"));
        setFixedHeight(kRowHeight);
        setCursor(Qt::PointingHandCursor);

        auto *name = new ElidedLabel(link.name.isEmpty() ? link.url : link.name, this);
        name->setProperty("role", QStringLiteral("rowTitle"));
        name->setMinimumWidth(110);
        name->setMaximumWidth(170);
        // 点名字 / 网址空白处也算点整行，别让鼠标事件停在标签上
        name->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto *url = new ElidedLabel(link.url, this);
        url->setProperty("role", QStringLiteral("rowHost"));
        url->setFont(Theme::monoFont(12));
        url->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto *remove = new QPushButton(this);
        remove->setProperty("role", QStringLiteral("icon"));
        remove->setFixedSize(kDeleteButton, kDeleteButton);
        remove->setFocusPolicy(Qt::NoFocus);
        remove->setCursor(Qt::PointingHandCursor);
        remove->setIcon(Icons::icon(Icon::Trash,
                                    Theme::color(QStringLiteral("err")), 14));
        remove->setIconSize(QSize(14, 14));
        remove->setToolTip(QStringLiteral("删除"));
        connect(remove, &QPushButton::clicked, this, [this] {
            if (onRemove)
                onRemove();
        });

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 0, 6, 0);
        layout->setSpacing(12);
        layout->addWidget(name);
        layout->addWidget(url, 1);
        layout->addWidget(remove);
    }

    std::function<void()> onEdit;
    std::function<void()> onRemove;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onEdit)
            onEdit();
        QFrame::mousePressEvent(event);
    }
};

} // namespace

LinkDialog::LinkDialog(const Connection &connection, QWidget *parent)
    : QDialog(parent)
    , m_links(connection.links)
    , m_connectionName(connection.name.isEmpty() ? connection.host : connection.name)
{
    setWindowTitle(QStringLiteral("管理网址"));
    setModal(true);
    setMinimumWidth(520);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(8);

    root->addWidget(makeLabel(QStringLiteral("管理网址"), "dialogHeadline", this));

    auto *detail = makeLabel(
        QStringLiteral("为「%1」保存常用网址，跟着这台连接一起存盘。").arg(m_connectionName),
        "dialogDetail", this);
    detail->setWordWrap(true);
    root->addWidget(detail);
    root->addSpacing(6);

    // ---- 输入区：名称 / 网址 / 添加 ----
    auto *nameCol = new QVBoxLayout;
    nameCol->setContentsMargins(0, 0, 0, 0);
    nameCol->setSpacing(6);
    nameCol->addWidget(makeLabel(QStringLiteral("名称"), "fieldLabel", this));

    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(QStringLiteral("比如：控制面板"));
    m_name->setMaxLength(60);
    m_name->setFixedWidth(150);
    nameCol->addWidget(m_name);

    auto *urlCol = new QVBoxLayout;
    urlCol->setContentsMargins(0, 0, 0, 0);
    urlCol->setSpacing(6);
    urlCol->addWidget(makeLabel(QStringLiteral("网址"), "fieldLabel", this));

    m_url = new QLineEdit(this);
    m_url->setPlaceholderText(QStringLiteral("https://…"));
    urlCol->addWidget(m_url);

    m_apply = new PillButton(QStringLiteral("添加"), PillButton::Primary, this);
    m_apply->setAutoDefault(false);

    m_cancelDraft = new PillButton(QStringLiteral("取消修改"), PillButton::Ghost, this);
    m_cancelDraft->setAutoDefault(false);
    m_cancelDraft->hide();

    auto *form = new QHBoxLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(10);
    form->addLayout(nameCol);
    form->addLayout(urlCol, 1);
    form->addWidget(m_apply, 0, Qt::AlignBottom);
    form->addWidget(m_cancelDraft, 0, Qt::AlignBottom);
    root->addLayout(form);
    root->addSpacing(6);

    // ---- 列表 ----
    auto *listCard = new QFrame(this);
    listCard->setProperty("role", QStringLiteral("card"));
    auto *cardLayout = new QVBoxLayout(listCard);
    cardLayout->setContentsMargins(6, 6, 6, 6);
    cardLayout->setSpacing(0);

    m_rowsHost = new QWidget;
    m_rowsLayout = new QVBoxLayout(m_rowsHost);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(0);

    m_scroll = new QScrollArea(listCard);
    m_scroll->setWidget(m_rowsHost);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cardLayout->addWidget(m_scroll);
    root->addWidget(listCard);

    // ---- 底部 ----
    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    footer->setSpacing(8);

    m_count = makeLabel(QString(), "hint", this);
    footer->addWidget(m_count);
    footer->addStretch();

    auto *cancel = new PillButton(QStringLiteral("取消"), PillButton::Ghost, this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(cancel);

    auto *save = new PillButton(QStringLiteral("保存"), PillButton::Primary, this);
    save->setAutoDefault(false);
    connect(save, &QPushButton::clicked, this, &LinkDialog::accept);
    footer->addWidget(save);
    root->addLayout(footer);

    connect(m_apply, &QPushButton::clicked, this, [this] { applyDraft(); });
    connect(m_cancelDraft, &QPushButton::clicked, this, [this] {
        clearDraft();
        rebuildRows();
    });
    // 在输入框里回车 = 添加 / 更新，不直接保存整个对话框
    connect(m_name, &QLineEdit::returnPressed, this, [this] { applyDraft(); });
    connect(m_url, &QLineEdit::returnPressed, this, [this] { applyDraft(); });
    connect(m_name, &QLineEdit::textChanged, this, &LinkDialog::refreshDraftButtons);
    connect(m_url, &QLineEdit::textChanged, this, &LinkDialog::refreshDraftButtons);

    // 键盘顺序：名称 → 网址 → 添加 → 保存 → 取消。不显式排的话，
    // 隐藏的「取消修改」也会占一格，Tab 会跳到看不见的地方。
    setTabOrder(m_name, m_url);
    setTabOrder(m_url, m_apply);
    setTabOrder(m_apply, save);
    setTabOrder(save, cancel);

    rebuildRows();
    refreshDraftButtons();
}

void LinkDialog::rebuildRows()
{
    while (QLayoutItem *item = m_rowsLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    if (m_links.isEmpty()) {
        auto *empty = makeLabel(QStringLiteral("还没有网址。填好名称和网址，点「添加」。"),
                                "emptyHint", m_rowsHost);
        empty->setAlignment(Qt::AlignCenter);
        m_rowsLayout->addWidget(empty);
    } else {
        for (int i = 0; i < m_links.size(); ++i) {
            auto *row = new LinkRow(m_links.at(i), m_rowsHost);
            row->onEdit = [this, i] { beginEdit(i); };
            row->onRemove = [this, i] { removeAt(i); };
            if (i == m_editIndex)
                row->setProperty("active", true);
            m_rowsLayout->addWidget(row);
        }
    }

    m_rowsLayout->addStretch();

    // 列表高度跟着内容走，太高才滚动。空的时候给一行提示留一点空间。
    const int contentHeight = m_links.isEmpty() ? 64 : m_links.size() * kRowHeight;
    m_scroll->setFixedHeight(qBound(64, contentHeight + 8, kMaxListHeight));

    if (m_links.isEmpty())
        m_count->setText(QStringLiteral("还没有网址"));
    else
        m_count->setText(QStringLiteral("共 %1 条 · 点选一条可修改").arg(m_links.size()));

    refreshDraftButtons();
}

void LinkDialog::refreshDraftButtons()
{
    m_apply->setText(m_editIndex >= 0 ? QStringLiteral("更新")
                                      : QStringLiteral("添加"));
    m_apply->setEnabled(!m_url->text().trimmed().isEmpty());
    m_cancelDraft->setVisible(m_editIndex >= 0);
}

void LinkDialog::beginEdit(int index)
{
    if (index < 0 || index >= m_links.size())
        return;

    m_editIndex = index;
    const ConnectionLink &link = m_links.at(index);
    m_name->setText(link.name);
    m_url->setText(link.url);
    m_url->setCursorPosition(m_url->text().size());
    m_url->setFocus();

    refreshDraftButtons();
    rebuildRows();
}

void LinkDialog::clearDraft()
{
    m_editIndex = -1;
    m_name->clear();
    m_url->clear();
    refreshDraftButtons();
}

bool LinkDialog::applyDraft()
{
    const QString url = m_url->text().trimmed();
    const QString name = m_name->text().trimmed();

    if (url.isEmpty()) {
        if (name.isEmpty())
            return true;       // 两边都空：当作没有草稿，不算错误
        m_url->setFocus();
        QMessageBox::information(this, QStringLiteral("管理网址"),
                                 QStringLiteral("网址不能为空。"));
        return false;
    }

    ConnectionLink link;
    link.url = url;
    link.name = name.isEmpty() ? url : name;   // 名称可以留空，用网址顶上

    if (m_editIndex >= 0 && m_editIndex < m_links.size())
        m_links[m_editIndex] = link;
    else
        m_links.append(link);

    clearDraft();
    rebuildRows();
    m_name->setFocus();
    return true;
}

void LinkDialog::removeAt(int index)
{
    if (index < 0 || index >= m_links.size())
        return;

    m_links.removeAt(index);
    if (m_editIndex == index)
        clearDraft();
    else if (m_editIndex > index)
        --m_editIndex;
    rebuildRows();
}

void LinkDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    // 构造函数里的 setFocus() 会被 show 流程的焦点分配覆盖掉，在这里补一次
    m_name->setFocus();

    if (m_dwmApplied)
        return;
    m_dwmApplied = true;

    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;

    // 原生标题栏跟着主题走：不然深色界面顶上会顶一条白条，特别扎眼
    const BOOL dark = (Theme::paletteKey() == QLatin1String("dark")) ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));

    const auto toColorRef = [](const QColor &c) {
        return COLORREF(RGB(c.red(), c.green(), c.blue()));
    };
    const COLORREF caption = toColorRef(Theme::color(QStringLiteral("bg-app")));
    const COLORREF text = toColorRef(Theme::color(QStringLiteral("text")));
    DwmSetWindowAttribute(hwnd, kDwmCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, kDwmTextColor, &text, sizeof(text));
    DwmSetWindowAttribute(hwnd, kDwmBorderColor, &caption, sizeof(caption));
}

void LinkDialog::accept()
{
    // 输入框里还有没点「添加」的内容：保存时一起收进去，别让用户白填
    if (!m_name->text().trimmed().isEmpty() || !m_url->text().trimmed().isEmpty()) {
        if (!applyDraft())
            return;
    }
    QDialog::accept();
}

bool LinkDialog::manage(const Connection &connection, QWidget *parent,
                        QVector<ConnectionLink> *out)
{
    LinkDialog dialog(connection, parent);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (out)
        *out = dialog.m_links;
    return true;
}

} // namespace Em
