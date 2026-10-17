#include "ui/UpdatePage.h"

#include "app/AppConfig.h"
#include "app/Theme.h"
#include "ui/IconProvider.h"
#include "ui/Widgets.h"

#include <QCoreApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace Em {

UpdatePage::UpdatePage(QWidget *parent) : QWidget(parent)
{
    auto *title = makeLabel(QStringLiteral("软件更新"), "pageTitle", this);
    auto *subtitle = makeLabel(QStringLiteral("检查并安装新版本"), "pageSubtitle", this);

    auto *card = new Card(this);
    auto *cardTitle = makeLabel(QStringLiteral("版本信息"), "sectionTitle", card);

    auto *versionLabel = makeLabel(QStringLiteral("v%1").arg(appVersion()), "mono", card);
    versionLabel->setFont(Theme::monoFont(13));
    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // 更新源没配置时也照样显示一行，写清楚「未配置」——
    // 否则页面上少一行，用户不知道是没配还是界面坏了。
    const QString repo = updateRepo();
    auto *sourceLabel = makeLabel(repo.isEmpty() ? QStringLiteral("未配置")
                                                 : QStringLiteral("GitHub · %1").arg(repo),
                                 "mono", card);
    sourceLabel->setFont(Theme::monoFont(12));
    sourceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *hint = makeLabel(
        QStringLiteral("发现新版本后会下载、校验，然后自动替换文件并重启程序。"
                       "装在 Program Files 之类需要管理员权限的目录时没法自动换，"
                       "会提示你手动更新。"),
        "hint", card);
    hint->setWordWrap(true);

    auto *checkButton = new PillButton(QStringLiteral("检查更新"), PillButton::Primary, card);
    checkButton->setLeadingIcon(Icon::Refresh, 15);
    connect(checkButton, &QPushButton::clicked, this, &UpdatePage::checkUpdateRequested);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(9);
    buttonRow->addWidget(checkButton);
    buttonRow->addStretch();

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->addRow(makeLabel(QStringLiteral("当前版本"), "fieldLabel", card), versionLabel);
    form->addRow(makeLabel(QStringLiteral("更新源"), "fieldLabel", card), sourceLabel);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(20, 18, 20, 18);
    cardLayout->setSpacing(12);
    cardLayout->addWidget(cardTitle);
    cardLayout->addLayout(form);
    cardLayout->addWidget(hint);
    cardLayout->addLayout(buttonRow);

    // 页面结构和「配置管理」一致：标题、副标题、卡片，整体包在滚动区里
    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(6, 22, 26, 22);
    bodyLayout->setSpacing(16);
    bodyLayout->addWidget(title);
    bodyLayout->addWidget(subtitle);
    bodyLayout->addSpacing(6);
    bodyLayout->addWidget(card);
    bodyLayout->addStretch();

    auto *scroll = new QScrollArea(this);
    scroll->setWidget(body);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(scroll);
}

} // namespace Em
