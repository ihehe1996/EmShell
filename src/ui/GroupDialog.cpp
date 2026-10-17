#include "ui/GroupDialog.h"

#include "app/Theme.h"
#include "ui/Widgets.h"

#include <QColorDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace Em {

namespace {

// 分组色板。全部选中间调：侧栏轨道在两套配色下都是深色（#161A23 / #1D212B），
// 而对话框底色接近白，太深或太浅的颜色总有一边会糊掉。
//
// 这组色值是**数据**（会原样写进 connections.json），不跟主题变，所以不走
// @token。但除了这里，界面上其它颜色仍然一律从 Theme 取。
const char *const kGroupColors[] = {
    "#4F8CFF", "#6E7CF5", "#9B6BE8", "#D564C0",
    "#EF5A5A", "#EE7B2E", "#C9961A", "#7FA431",
    "#2FA45E", "#2AA79A", "#2E9BC0", "#7F889F",
};

constexpr int kSwatchSize = 26;
constexpr int kColumns = 6;

} // namespace

GroupDialog::GroupDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("新建分组"));
    setMinimumWidth(400);
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(8);

    root->addWidget(makeLabel(QStringLiteral("名称"), "fieldLabel", this));
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(QStringLiteral("比如：生产环境"));
    m_nameEdit->setMaxLength(40);
    root->addWidget(m_nameEdit);

    root->addSpacing(6);
    root->addWidget(makeLabel(QStringLiteral("标记颜色"), "fieldLabel", this));

    auto *colorRow = new QHBoxLayout;
    colorRow->setContentsMargins(0, 0, 0, 0);
    colorRow->setSpacing(8);

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(8);

    int index = 0;
    for (const char *hex : kGroupColors) {
        const QColor color(QString::fromLatin1(hex));

        auto *swatch = new QPushButton(this);
        swatch->setFixedSize(kSwatchSize, kSwatchSize);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setFocusPolicy(Qt::NoFocus);
        swatch->setToolTip(hex);
        connect(swatch, &QPushButton::clicked, this, [this, color] { setColor(color); });

        grid->addWidget(swatch, index / kColumns, index % kColumns);
        m_swatches << swatch;
        ++index;
    }

    colorRow->addLayout(grid);
    colorRow->addSpacing(6);

    auto *custom = new PillButton(QStringLiteral("自定义…"), PillButton::Ghost, this);
    connect(custom, &QPushButton::clicked, this, [this] {
        // 老分组没有颜色（m_color 为空）：取色器从一个看得见的颜色起步，
        // 别让它开在纯黑上
        QColor initial(m_color);
        if (!initial.isValid())
            initial = Theme::color(QStringLiteral("nav-text-active"));

        const QColor picked = QColorDialog::getColor(initial, this,
                                                     QStringLiteral("选择分组颜色"));
        if (picked.isValid())
            setColor(picked);
    });
    colorRow->addWidget(custom, 0, Qt::AlignVCenter);
    colorRow->addStretch();
    root->addLayout(colorRow);

    root->addSpacing(14);
    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch();

    auto *cancel = new PillButton(QStringLiteral("取消"), PillButton::Ghost, this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);

    m_submit = new PillButton(QStringLiteral("创建"), PillButton::Primary, this);
    m_submit->setDefault(true);
    connect(m_submit, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(m_submit);
    root->addLayout(buttons);

    // 名称空着不让提交 —— 空名字的分组在侧栏上就是一条没法辨认的横线
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_submit->setEnabled(!text.trimmed().isEmpty());
    });
    m_submit->setEnabled(false);

    setColor(QColor(QString::fromLatin1(kGroupColors[0])));
}

void GroupDialog::setColor(const QColor &color)
{
    // 统一存成 #rrggbb：自定义取色器给的是 QColor，预设给的是常量，
    // 两边必须落到同一种写法，不然同一个颜色会被当成两个
    m_color = color.isValid() ? color.name(QColor::HexRgb) : QString();
    refreshSwatches();
}

void GroupDialog::refreshSwatches()
{
    for (QPushButton *swatch : m_swatches) {
        const QColor color(swatch->toolTip());
        const bool selected = color.name(QColor::HexRgb) == m_color;

        // 注意：边框**永远保留 2px**，选中只改颜色。加/去 border 会占盒子尺寸，
        // 选中那格会比邻居大一圈（和侧栏选中态不能加 border 是同一个道理）。
        const QString ring = selected ? Theme::color(QStringLiteral("text")).name()
                                      : QStringLiteral("transparent");
        swatch->setStyleSheet(QStringLiteral("background-color:%1; border:2px solid %2;"
                                             "border-radius:%3px;")
                                  .arg(color.name(), ring)
                                  .arg(kSwatchSize / 2));
    }
}

bool GroupDialog::create(QWidget *parent, ConnectionGroup *out)
{
    GroupDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("新建分组"));
    dialog.m_submit->setText(QStringLiteral("创建"));

    if (dialog.exec() != QDialog::Accepted)
        return false;

    ConnectionGroup result;
    result.name = dialog.m_nameEdit->text().trimmed();
    result.color = dialog.m_color;
    if (result.name.isEmpty())
        return false;

    if (out)
        *out = result;
    return true;
}

bool GroupDialog::edit(const ConnectionGroup &existing, QWidget *parent, ConnectionGroup *out)
{
    GroupDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("编辑分组"));
    dialog.m_submit->setText(QStringLiteral("保存"));
    dialog.m_result = existing;

    dialog.m_nameEdit->setText(existing.name);
    // 老分组没有颜色：保持空，保存时也不硬塞一个 —— 让它继续跟着主题色走，
    // 直到用户真的选了一个
    dialog.m_color = existing.color;
    dialog.refreshSwatches();

    if (dialog.exec() != QDialog::Accepted)
        return false;

    ConnectionGroup result = dialog.m_result;
    result.name = dialog.m_nameEdit->text().trimmed();
    result.color = dialog.m_color;
    if (result.name.isEmpty())
        return false;

    if (out)
        *out = result;
    return true;
}

} // namespace Em
