#include "ui/ConnectionFormPanel.h"

#include "app/Theme.h"
#include "model/ConnectionStore.h"
#include "model/KeyStore.h"
#include "ui/IconProvider.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpacerItem>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

namespace Em {

ConnectionFormPanel::ConnectionFormPanel(ConnectionStore *store, KeyStore *keys, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_keys(keys)
{
    buildUi();
    startNew();
}

void ConnectionFormPanel::buildUi()
{
    m_title = makeLabel(QStringLiteral("新建连接"), "sectionTitle", this);

    // 抽屉的收起按钮，放在标题行最右边
    auto *closeButton = new PillButton(QString(), PillButton::Ghost, this);
    closeButton->makeCompact(Icon::Close, 13, QStringLiteral("收起"));
    connect(closeButton, &QPushButton::clicked, this, &ConnectionFormPanel::cancelled);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    titleRow->addWidget(m_title);
    titleRow->addStretch();
    titleRow->addWidget(closeButton);

    // 用网格而不是 QFormLayout：主机地址和端口、用户名和认证方式本来就该并排，
    // QFormLayout 只能一行一个字段，12 行竖下来又长又没层次。
    //
    // 四列：标签 / 主字段 / 次标签 / 次字段。主字段吃满剩余宽度，
    // 端口和认证方式固定在右边那一列，宽度够用又不会把主字段挤窄。
    auto *form = new QGridLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    form->setColumnMinimumWidth(0, 64);
    form->setColumnStretch(1, 1);
    form->setColumnMinimumWidth(2, 56);
    form->setColumnMinimumWidth(3, 128);

    const auto fieldLabel = [this](const QString &text) {
        return makeLabel(text, "fieldLabel", this);
    };
    // 同一行右边那个小标签（「端口」「认证方式」）靠右贴着自己的字段
    const auto inlineLabel = [this](const QString &text) {
        QLabel *label = makeLabel(text, "fieldLabel", this);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return label;
    };
    const auto sectionLabel = [this](const QString &text) {
        return makeLabel(text, "formSection", this);
    };

    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(QStringLiteral("留空则用主机地址做名字"));

    m_group = new SelectBox(this);
    refreshGroups();

    m_host = new QLineEdit(this);
    m_host->setPlaceholderText(QStringLiteral("192.168.1.100 或 example.com"));

    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(22);

    m_user = new QLineEdit(this);
    m_user->setPlaceholderText(QStringLiteral("root"));

    m_auth = new SelectBox(this);
    m_auth->addItem(QStringLiteral("密码"));
    m_auth->addItem(QStringLiteral("公钥"));
    connect(m_auth, &QComboBox::currentIndexChanged, this, &ConnectionFormPanel::onAuthChanged);

    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(QStringLiteral("登录密码"));

    // 标签写短，加密方式挪进 tooltip。
    // QCheckBox 的 minimumSizeHint 就是那行字的宽度，且不能省略也不能换行 ——
    // 原来那句「保存密码（用当前 Windows 账户加密）」有 260px 宽，直接把整个
    // 表单的最小宽度顶到 355，装进 300 的抽屉里会溢出被裁。
    // 加密方式在下面那行小字里已经说过了，这里不重复。
    m_savePassword = new QCheckBox(QStringLiteral("保存密码"), this);
    m_savePassword->setToolTip(QStringLiteral("密码会用当前 Windows 账户加密（DPAPI）后写入配置文件"));
    m_savePassword->setChecked(true);

    m_key = new SelectBox(this);
    refreshKeyList();

    m_favorite = new QCheckBox(QStringLiteral("加入收藏夹"), this);

    m_tags = new QLineEdit(this);
    m_tags->setPlaceholderText(QStringLiteral("多个标签用空格隔开，例如：生产 阿里云"));

    m_note = new QTextEdit(this);
    m_note->setPlaceholderText(QStringLiteral("备注（可选）"));
    m_note->setFixedHeight(64);

    int row = 0;

    // 分组标题之间的那段留白放在布局里（而不是 QSS 的 padding-top）——
    // 布局会把它算进 sizeHint，弹窗量高度时不会漏掉。见 app.qss 里的说明。
    const auto addSection = [&](const QString &text, bool extraTop) {
        if (extraTop) {
            form->addItem(new QSpacerItem(0, 6, QSizePolicy::Minimum, QSizePolicy::Fixed),
                          row++, 0, 1, 4);
        }
        form->addWidget(sectionLabel(text), row++, 0, 1, 4);
    };

    addSection(QStringLiteral("连接信息"), false);

    form->addWidget(fieldLabel(QStringLiteral("名称")), row, 0);
    form->addWidget(m_name, row++, 1, 1, 3);

    form->addWidget(fieldLabel(QStringLiteral("主机地址")), row, 0);
    form->addWidget(m_host, row, 1);
    form->addWidget(inlineLabel(QStringLiteral("端口")), row, 2);
    form->addWidget(m_port, row++, 3);

    form->addWidget(fieldLabel(QStringLiteral("用户名")), row, 0);
    form->addWidget(m_user, row, 1);
    form->addWidget(inlineLabel(QStringLiteral("认证方式")), row, 2);
    form->addWidget(m_auth, row++, 3);

    addSection(QStringLiteral("认证"), true);

    form->addWidget(fieldLabel(QStringLiteral("密码")), row, 0);
    form->addWidget(m_password, row++, 1, 1, 3);
    form->addWidget(m_savePassword, row++, 1, 1, 3);

    form->addWidget(fieldLabel(QStringLiteral("密钥")), row, 0);
    form->addWidget(m_key, row++, 1, 1, 3);

    addSection(QStringLiteral("其他信息"), true);

    form->addWidget(fieldLabel(QStringLiteral("分组")), row, 0);
    form->addWidget(m_group, row++, 1, 1, 3);

    form->addWidget(fieldLabel(QStringLiteral("标签")), row, 0);
    form->addWidget(m_tags, row++, 1, 1, 3);
    form->addWidget(m_favorite, row++, 1, 1, 3);

    form->addWidget(fieldLabel(QStringLiteral("备注")), row, 0);
    form->addWidget(m_note, row++, 1, 1, 3);

    form->setRowStretch(row, 1);

    auto *cancel = new PillButton(QStringLiteral("取消"), PillButton::Ghost, this);
    connect(cancel, &QPushButton::clicked, this, &ConnectionFormPanel::cancelled);

    m_submit = new PillButton(QStringLiteral("创建"), PillButton::Primary, this);
    m_submit->setLeadingIcon(Icon::Check, 15);
    connect(m_submit, &QPushButton::clicked, this, [this] {
        if (commit())
            emit saved(m_result);
    });

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(9);
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(m_submit);

    // ---- 三段式：头部 / 可滚动的表单 / 底部按钮 ----
    //
    // 头和底固定在抽屉上下两端，**只有中间的表单滚**。全都塞进一个滚动区的话，
    // 字段多、窗口矮的时候标题和「保存」会被一起滚出屏幕，得拉到顶才能点保存。
    m_header = new QWidget(this);
    auto *headerLayout = new QVBoxLayout(m_header);
    headerLayout->setContentsMargins(16, 22, 16, 16);
    headerLayout->setSpacing(0);
    headerLayout->addLayout(titleRow);

    m_formBody = new QWidget;
    m_formBody->setObjectName(QStringLiteral("FormBody"));
    auto *formBodyLayout = new QVBoxLayout(m_formBody);
    // 左右 22 和对话框头部对齐，表单不会比标题窄一截
    formBodyLayout->setContentsMargins(22, 4, 22, 0);
    formBodyLayout->setSpacing(0);
    formBodyLayout->addLayout(form);
    formBodyLayout->addStretch(1);

    m_formScroll = new QScrollArea(this);
    m_formScroll->setObjectName(QStringLiteral("FormScroll"));
    m_formScroll->setWidget(m_formBody);
    m_formScroll->setWidgetResizable(true);
    m_formScroll->setFrameShape(QFrame::NoFrame);
    m_formScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("FormFooter"));
    footer->setAttribute(Qt::WA_StyledBackground, true);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(22, 14, 22, 18);
    footerLayout->setSpacing(9);
    footerLayout->addLayout(buttons);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_header);
    // 头下面、按钮上面各压一道渐变，让「上下固定、中间在滚」这件事看得出来。
    // 没有它的话三块全是白的，滚动区像是凭空被切开。
    m_headerShadow = new EdgeShadow(Qt::BottomEdge, this);
    layout->addWidget(m_headerShadow);
    layout->addWidget(m_formScroll, 1);
    layout->addWidget(new EdgeShadow(Qt::TopEdge, this));
    layout->addWidget(footer);

    // 和别的控件一样，回车提交
    for (QLineEdit *edit : { m_name, m_host, m_user, m_password, m_tags })
        connect(edit, &QLineEdit::returnPressed, m_submit, &QPushButton::click);

    if (m_store) {
        connect(m_store, &ConnectionStore::groupsChanged,
                this, &ConnectionFormPanel::refreshGroups);
    }
}

void ConnectionFormPanel::startNew()
{
    m_isNew = true;
    m_result = Connection();

    m_title->setText(QStringLiteral("新建连接"));
    m_submit->setText(m_submitOverride.isEmpty() ? QStringLiteral("创建") : m_submitOverride);

    // 每次打开都重新拉一遍密钥，用户可能刚在密钥页导入过
    refreshKeyList();
    loadInto(m_result);
    m_host->setFocus();
}

void ConnectionFormPanel::startNewWith(const Connection &seed)
{
    startNew();

    // 只抄字段，不抄 id：这条是「照着它再建一条」，不是「改它」
    Connection fields = seed;
    fields.id.clear();
    loadInto(fields);

    m_host->setFocus();
}

void ConnectionFormPanel::startEdit(const Connection &existing)
{
    m_isNew = false;
    m_result = existing;   // 带上 id / createdAt，commit 时不会丢

    m_title->setText(QStringLiteral("编辑连接"));
    m_submit->setText(m_submitOverride.isEmpty() ? QStringLiteral("保存") : m_submitOverride);

    refreshKeyList();
    loadInto(existing);
    m_host->setFocus();
}

void ConnectionFormPanel::setSubmitText(const QString &text)
{
    m_submitOverride = text;
    m_submit->setText(text);
}

void ConnectionFormPanel::setHeaderVisible(bool visible)
{
    m_header->setVisible(visible);
    m_headerShadow->setVisible(visible);
}

int ConnectionFormPanel::formContentHeight() const
{
    if (!m_formScroll || !m_formBody)
        return sizeHint().height();

    // 面板自己的 sizeHint 里，中间那段用的是 QScrollArea 那个被削过的
    // sizeHint。把它换成内容本身的高度，就是「一点不滚」时需要的总高。
    return sizeHint().height() - m_formScroll->sizeHint().height()
           + m_formBody->sizeHint().height();
}

void ConnectionFormPanel::refreshGroups()
{
    if (!m_group)
        return;

    const QString keep = m_group->currentData().toString();

    QSignalBlocker blocker(m_group);
    m_group->clear();
    m_group->addItem(QStringLiteral("未分组"), QString());
    if (m_store) {
        for (const ConnectionGroup &g : m_store->groups())
            m_group->addItem(g.name, g.id);
    }

    const int index = m_group->findData(keep);
    m_group->setCurrentIndex(index >= 0 ? index : 0);
}

void ConnectionFormPanel::refreshKeyList()
{
    const QString keep = m_key->currentData().toString();

    QSignalBlocker blocker(m_key);
    m_key->clear();
    m_key->addItem(QStringLiteral("不使用密钥"), QString());
    if (m_keys) {
        for (const SshKey &k : m_keys->keys())
            m_key->addItem(QStringLiteral("%1（%2）").arg(k.name, k.summary()), k.id);
    }
    if (m_key->count() == 1)
        m_key->addItem(QStringLiteral("（还没有导入任何密钥）"), QString());

    const int index = m_key->findData(keep);
    m_key->setCurrentIndex(index >= 0 ? index : 0);
}

void ConnectionFormPanel::loadInto(const Connection &c)
{
    m_name->setText(c.name);
    m_host->setText(c.host);
    m_port->setValue(c.port > 0 ? c.port : 22);
    m_user->setText(c.username);
    m_auth->setCurrentIndex(c.auth == AuthType::Password ? 0 : 1);
    m_password->setText(c.password);
    m_savePassword->setChecked(c.savePassword);
    m_favorite->setChecked(c.favorite);
    m_tags->setText(c.tags.join(QLatin1Char(' ')));
    m_note->setPlainText(c.note);

    const int groupIndex = m_group->findData(c.groupId);
    m_group->setCurrentIndex(groupIndex >= 0 ? groupIndex : 0);

    const int keyIndex = m_key->findData(c.keyId);
    m_key->setCurrentIndex(keyIndex >= 0 ? keyIndex : 0);

    onAuthChanged();
}

void ConnectionFormPanel::onAuthChanged()
{
    const bool byPassword = (m_auth->currentIndex() == 0);
    m_password->setEnabled(byPassword);
    m_savePassword->setEnabled(byPassword);
    m_key->setEnabled(!byPassword);
}

bool ConnectionFormPanel::commit()
{
    const QString host = m_host->text().trimmed();
    if (host.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("还差一点"),
                             QStringLiteral("主机地址不能为空。"));
        m_host->setFocus();
        return false;
    }

    m_result.host = host;
    m_result.name = m_name->text().trimmed().isEmpty() ? host : m_name->text().trimmed();
    m_result.port = m_port->value();
    m_result.username = m_user->text().trimmed();
    m_result.auth = m_auth->currentIndex() == 0 ? AuthType::Password : AuthType::PublicKey;
    m_result.password = m_password->text();
    m_result.savePassword = m_savePassword->isChecked();
    m_result.keyId = m_key->currentData().toString();
    m_result.groupId = m_group->currentData().toString();
    m_result.favorite = m_favorite->isChecked();
    m_result.note = m_note->toPlainText().trimmed();

    m_result.tags.clear();
    for (const QString &t : m_tags->text().split(QLatin1Char(' '), Qt::SkipEmptyParts))
        m_result.tags << t.trimmed();

    return true;
}

} // namespace Em
