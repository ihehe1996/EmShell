#include "ui/UpdatePage.h"

#include "app/AppConfig.h"
#include "app/Theme.h"
#include "ui/IconProvider.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QScrollArea>
#include <QVBoxLayout>

namespace Em {

namespace {

// 和标题栏用同一张图。这里显示得大一些，按屏幕缩放比降采样再交给 QLabel，
// 否则 125% 缩放下会被 Qt 用最近邻放大，边缘发毛。
QPixmap logoPixmap(int size)
{
    const QPixmap source(QStringLiteral(":/icons/app-logo.png"));
    if (source.isNull())
        return {};

    const qreal dpr = qApp->devicePixelRatio();
    QPixmap scaled = source.scaled(QSize(qRound(size * dpr), qRound(size * dpr)),
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    return scaled;
}

} // namespace

UpdatePage::UpdatePage(QWidget *parent) : QWidget(parent)
{
    auto *title = makeLabel(QStringLiteral("软件更新"), "pageTitle", this);
    auto *subtitle = makeLabel(QStringLiteral("检查并安装新版本"), "pageSubtitle", this);

    // ---- 左：版本与更新 ----
    auto *updateCard = new Card(this);

    auto *mark = new QLabel(updateCard);
    mark->setFixedSize(44, 44);
    mark->setPixmap(logoPixmap(44));

    auto *appName = makeLabel(QStringLiteral("EmShell"), "appName", updateCard);
    auto *appMeta = makeLabel(QStringLiteral("v%1 · Windows 上的服务器连接工具")
                                                  .arg(appVersion()),
                              "appMeta", updateCard);

    auto *nameBox = new QVBoxLayout;
    nameBox->setContentsMargins(0, 0, 0, 0);
    nameBox->setSpacing(3);
    nameBox->addWidget(appName);
    nameBox->addWidget(appMeta);
    nameBox->addStretch();

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(14);
    header->addWidget(mark, 0, Qt::AlignTop);
    header->addLayout(nameBox, 1);

    auto *hint = makeLabel(
        QStringLiteral("发现新版本后会自动下载、校验，然后替换文件并重新启动程序。"),
        "hint", updateCard);
    hint->setWordWrap(true);
    // QLabel 开了自动换行之后，minimumSizeHint() 会按「极窄宽度」估算高度，
    // 整张卡片会被撑得非常高。给一个合理的最小宽度，让它按实际宽度折行。
    hint->setMinimumWidth(320);

    auto *hint2 = makeLabel(
        QStringLiteral("装在 Program Files 等受保护目录时可能没法自动替换，"
                       "程序会提示你手动更新。"),
        "hint", updateCard);
    hint2->setWordWrap(true);
    hint2->setMinimumWidth(320);

    auto *checkButton = new PillButton(QStringLiteral("检查更新"), PillButton::Primary, updateCard);
    checkButton->setLeadingIcon(Icon::Refresh, 15);
    connect(checkButton, &QPushButton::clicked, this, &UpdatePage::checkUpdateRequested);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(9);
    buttonRow->addWidget(checkButton);
    buttonRow->addStretch();

    auto *updateLayout = new QVBoxLayout(updateCard);
    updateLayout->setContentsMargins(22, 20, 22, 20);
    updateLayout->setSpacing(12);
    updateLayout->addLayout(header);
    updateLayout->addSpacing(2);
    updateLayout->addWidget(hint);
    updateLayout->addWidget(hint2);
    updateLayout->addSpacing(2);
    updateLayout->addLayout(buttonRow);
    updateLayout->addStretch();

    // ---- 右：关于 ----
    auto *aboutCard = new Card(this);
    auto *aboutTitle = makeLabel(QStringLiteral("关于"), "sectionTitle", aboutCard);

    auto *home = makeLabel(
        QStringLiteral("<a href=\"https://www.boshou.me\">www.boshou.me</a>"),
        "aboutValue", aboutCard);
    home->setTextFormat(Qt::RichText);
    home->setOpenExternalLinks(true);
    home->setTextInteractionFlags(Qt::TextBrowserInteraction);
    {
        // 富文本里的链接颜色走 QPalette::Link，QSS 的 color 管不到。
        QPalette palette = home->palette();
        palette.setColor(QPalette::Link, Theme::color(QStringLiteral("accent-text")));
        palette.setColor(QPalette::LinkVisited, Theme::color(QStringLiteral("accent-text")));
        home->setPalette(palette);
    }

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(12);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->addRow(makeLabel(QStringLiteral("开发语言"), "fieldLabel", aboutCard),
                 makeLabel(QStringLiteral("C++ / Qt 6"), "aboutValue", aboutCard));
    form->addRow(makeLabel(QStringLiteral("作者主页"), "fieldLabel", aboutCard), home);
    form->addRow(makeLabel(QStringLiteral("作者 QQ"), "fieldLabel", aboutCard),
                 makeLabel(QStringLiteral("10220739"), "aboutValue", aboutCard));

    auto *aboutLayout = new QVBoxLayout(aboutCard);
    aboutLayout->setContentsMargins(22, 20, 22, 20);
    aboutLayout->setSpacing(14);
    aboutLayout->addWidget(aboutTitle);
    aboutLayout->addLayout(form);
    aboutLayout->addStretch();

    // 右边固定宽度：三行信息都不长，跟着卡片一起拉伸会显得很空
    aboutCard->setFixedWidth(340);

    auto *cards = new QHBoxLayout;
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(16);
    cards->addWidget(updateCard, 1);
    cards->addWidget(aboutCard);

    // 页面结构和「配置管理」一致：标题、副标题、卡片，整体包在滚动区里
    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(6, 22, 26, 22);
    bodyLayout->setSpacing(16);
    bodyLayout->addWidget(title);
    bodyLayout->addWidget(subtitle);
    bodyLayout->addSpacing(6);
    bodyLayout->addLayout(cards);
    // 必须是 addStretch(1)：默认的 addStretch() 权重是 0，而卡片内部有
    // addStretch()，会被判定成「可纵向扩展」，多出来的高度就和它平分了 ——
    // 表现是两张卡片被拉得很高、下面留一大片空。权重 1 才能把余量全吃掉。
    bodyLayout->addStretch(1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidget(body);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(scroll);
}

} // namespace Em
