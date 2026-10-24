#include "ui/ConnectionFormPanel.h"

#include "app/Theme.h"
#include "model/ConnectionStore.h"
#include "model/KeyStore.h"
#include "ui/IconProvider.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
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

    // 字段是「标签在上、输入框在下」的竖排：右栏宽度足够，竖排比左右两列
    // 更好扫读，也和参考图的结构一致。主机和端口、分组和收藏仍然并排。
    auto *form = new QVBoxLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(12);

    m_name = new QLineEdit(this);
    m_name->setToolTip(QStringLiteral("留空则用主机做名字"));

    m_host = new QLineEdit(this);

    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(22);
    m_port->setAlignment(Qt::AlignRight);

    m_user = new QLineEdit(this);

    m_auth = new SelectBox(this);
    m_auth->addItem(QStringLiteral("密码登录"));
    m_auth->addItem(QStringLiteral("私钥文件"));
    connect(m_auth, &QComboBox::currentIndexChanged,
            this, &ConnectionFormPanel::onAuthChanged);

    m_password = new QLineEdit(this);
    m_password->setObjectName(QStringLiteral("PasswordEdit"));
    m_password->setEchoMode(QLineEdit::Password);

    // 密码框右侧的显隐按钮：按钮挂进 QLineEdit 自己的布局，
    // 再用 QSS 的 padding-right 给文字让位（参考页里的那颗眼睛）。
    auto *eyeButton = new QPushButton(m_password);
    eyeButton->setProperty("role", "icon");
    eyeButton->setFixedSize(26, 26);
    eyeButton->setCursor(Qt::PointingHandCursor);
    eyeButton->setIcon(Icons::icon(Icon::EyeOff,
                                   Theme::color(QStringLiteral("text-mute")), 15));
    eyeButton->setIconSize(QSize(15, 15));
    eyeButton->setToolTip(QStringLiteral("显示密码"));
    auto *passwordLayout = new QHBoxLayout(m_password);
    passwordLayout->setContentsMargins(0, 0, 6, 0);
    passwordLayout->setSpacing(0);
    passwordLayout->addStretch();
    passwordLayout->addWidget(eyeButton);
    connect(eyeButton, &QPushButton::clicked, this, [this, eyeButton] {
        const bool hidden = m_password->echoMode() == QLineEdit::Password;
        m_password->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        eyeButton->setIcon(Icons::icon(hidden ? Icon::Eye : Icon::EyeOff,
                                       Theme::color(QStringLiteral("text-mute")), 15));
        eyeButton->setToolTip(hidden ? QStringLiteral("隐藏密码")
                                     : QStringLiteral("显示密码"));
    });

    // 标签写短，存储方式挪进 tooltip。QCheckBox 的 minimumSizeHint 就是
    // 那行字的宽度，且不能省略也不能换行，写长了会把整个表单的最小宽度顶大。
    m_savePassword = new CheckBox(QStringLiteral("保存密码"), this);
    m_savePassword->setToolTip(QStringLiteral("密码会明文写入本机配置文件"));
    m_savePassword->setChecked(true);

    m_group = new SelectBox(this);
    refreshGroups();

    m_key = new SelectBox(this);
    refreshKeyList();
    m_keyHint = makeLabel(QStringLiteral("在「密钥管理」里导入"), "hint", this);
    m_keyHint->setVisible(false);

    // 私钥选择区：虚线框 + 钥匙图标，对应参考页的上传区
    auto *keyPicker = new QFrame(this);
    keyPicker->setObjectName(QStringLiteral("KeyPicker"));
    keyPicker->setAttribute(Qt::WA_StyledBackground, true);
    auto *keyLayout = new QHBoxLayout(keyPicker);
    keyLayout->setContentsMargins(12, 0, 4, 0);
    keyLayout->setSpacing(8);
    auto *keyIcon = new QLabel(keyPicker);
    keyIcon->setPixmap(Icons::pixmap(Icon::Key,
                                     Theme::color(QStringLiteral("text-mute")), 17));
    keyLayout->addWidget(keyIcon, 0, Qt::AlignVCenter);
    keyLayout->addWidget(m_key, 1);

    // 密码和「保存密码」同一行：勾选项紧跟它修饰的字段，不再占一行标签
    auto *passwordRow = new QWidget(this);
    auto *passwordRowLayout = new QHBoxLayout(passwordRow);
    passwordRowLayout->setContentsMargins(0, 0, 0, 0);
    passwordRowLayout->setSpacing(12);
    passwordRowLayout->addWidget(m_password, 1);
    passwordRowLayout->addWidget(m_savePassword, 0, Qt::AlignVCenter);

    // 密码 / 私钥共用一个字段位置，切换认证方式时整块换掉
    m_secretStack = new QStackedWidget(this);
    m_secretStack->setFrameShape(QFrame::NoFrame);
    m_secretStack->addWidget(passwordRow);
    m_secretStack->addWidget(keyPicker);

    m_tags = new QLineEdit(this);
    m_tags->setToolTip(QStringLiteral("多个标签用空格隔开，例如：生产 阿里云"));

    m_note = new QTextEdit(this);
    m_note->setFixedHeight(56);
    // Tab 在表单里应该换焦点，而不是往备注里插一个制表符
    m_note->setTabChangesFocus(true);

    const auto formLabel = [this](const QString &text) {
        return makeLabel(text, "formLabel", this);
    };

    // 一个字段块 = 标签行（可带右侧小控件） + 输入控件
    const auto makeBlock = [this](QLabel *label, QWidget *field, QWidget *accessory = nullptr) {
        auto *block = new QWidget(this);
        auto *v = new QVBoxLayout(block);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(6);

        auto *labelRow = new QWidget(block);
        auto *h = new QHBoxLayout(labelRow);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        h->addWidget(label);
        h->addStretch();
        if (accessory)
            h->addWidget(accessory, 0, Qt::AlignRight | Qt::AlignVCenter);

        v->addWidget(labelRow);
        v->addWidget(field);
        return block;
    };

    form->addWidget(makeBlock(formLabel(QStringLiteral("名称")), m_name));

    // 主机和端口等宽
    auto *hostBlock = makeBlock(formLabel(QStringLiteral("主机")), m_host);
    auto *portBlock = makeBlock(formLabel(QStringLiteral("端口")), m_port);
    auto *hostRow = new QHBoxLayout;
    hostRow->setContentsMargins(0, 0, 0, 0);
    hostRow->setSpacing(12);
    hostRow->addWidget(hostBlock, 1);
    hostRow->addWidget(portBlock, 1);
    form->addLayout(hostRow);

    // 用户名和认证方式并排、等宽
    auto *userBlock = makeBlock(formLabel(QStringLiteral("用户名")), m_user);
    m_authBlock = makeBlock(formLabel(QStringLiteral("认证方式")), m_auth);
    auto *userRow = new QHBoxLayout;
    userRow->setContentsMargins(0, 0, 0, 0);
    userRow->setSpacing(12);
    userRow->addWidget(userBlock, 1);
    userRow->addWidget(m_authBlock, 1);
    form->addLayout(userRow);

    // 密码 / 密钥共用一行：标签字和右边的小控件跟着认证方式切
    m_secretLabel = formLabel(QStringLiteral("密码"));
    auto *secretBlock = new QWidget(this);
    auto *secretLayout = new QVBoxLayout(secretBlock);
    secretLayout->setContentsMargins(0, 0, 0, 0);
    secretLayout->setSpacing(6);

    auto *secretLabelRow = new QWidget(secretBlock);
    auto *secretRowLayout = new QHBoxLayout(secretLabelRow);
    secretRowLayout->setContentsMargins(0, 0, 0, 0);
    secretRowLayout->setSpacing(8);
    secretRowLayout->addWidget(m_secretLabel);
    secretRowLayout->addStretch();
    secretRowLayout->addWidget(m_keyHint, 0, Qt::AlignRight | Qt::AlignVCenter);

    secretLayout->addWidget(secretLabelRow);
    secretLayout->addWidget(m_secretStack);
    form->addWidget(secretBlock);

    // ---- 远程桌面选项（protocol == Rdp 时才显示）----
    // 和端口一样，都是写进临时 .rdp 文件的参数；这里只放最常用的三个开关。
    m_rdpOptionsBlock = new QWidget(this);
    auto *rdpOptionsLayout = new QVBoxLayout(m_rdpOptionsBlock);
    rdpOptionsLayout->setContentsMargins(0, 0, 0, 0);
    rdpOptionsLayout->setSpacing(6);

    auto *rdpLabelRow = new QWidget(m_rdpOptionsBlock);
    auto *rdpLabelLayout = new QHBoxLayout(rdpLabelRow);
    rdpLabelLayout->setContentsMargins(0, 0, 0, 0);
    rdpLabelLayout->setSpacing(0);
    rdpLabelLayout->addWidget(formLabel(QStringLiteral("桌面选项")));
    rdpLabelLayout->addStretch();

    auto *rdpChecks = new QHBoxLayout;
    rdpChecks->setContentsMargins(0, 0, 0, 0);
    rdpChecks->setSpacing(18);
    m_rdpFullscreen = new CheckBox(QStringLiteral("全屏"), m_rdpOptionsBlock);
    m_rdpMultiMon = new CheckBox(QStringLiteral("多显示器"), m_rdpOptionsBlock);
    m_rdpAdmin = new CheckBox(QStringLiteral("管理会话"), m_rdpOptionsBlock);
    m_rdpMultiMon->setToolTip(QStringLiteral("在全部显示器上铺满（需要全屏）"));
    m_rdpAdmin->setToolTip(QStringLiteral("连接控制台管理会话，相当于 mstsc /admin"));
    rdpChecks->addWidget(m_rdpFullscreen);
    rdpChecks->addWidget(m_rdpMultiMon);
    rdpChecks->addWidget(m_rdpAdmin);
    rdpChecks->addStretch();

    rdpOptionsLayout->addWidget(rdpLabelRow);
    rdpOptionsLayout->addLayout(rdpChecks);
    form->addWidget(m_rdpOptionsBlock);

    // 「更多信息」：一条细线把可选项和上面的连接 / 认证信息分开
    auto *moreRow = new QWidget(this);
    auto *moreLayout = new QHBoxLayout(moreRow);
    moreLayout->setContentsMargins(0, 0, 0, 0);
    moreLayout->setSpacing(10);
    auto *moreLabel = makeLabel(QStringLiteral("更多信息"), "formSection", moreRow);
    auto *moreLine = new QFrame(moreRow);
    moreLine->setObjectName(QStringLiteral("FormDivider"));
    moreLine->setFixedHeight(1);
    moreLine->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    moreLayout->addWidget(moreLabel);
    moreLayout->addWidget(moreLine, 1);
    form->addWidget(moreRow);

    // 分组和标签并排，省一行高度
    auto *groupBlock = makeBlock(formLabel(QStringLiteral("分组")), m_group);
    auto *tagsBlock = makeBlock(formLabel(QStringLiteral("标签")), m_tags);
    auto *metaRow = new QHBoxLayout;
    metaRow->setContentsMargins(0, 0, 0, 0);
    metaRow->setSpacing(12);
    metaRow->addWidget(groupBlock, 1);
    metaRow->addWidget(tagsBlock, 1);
    form->addLayout(metaRow);
    form->addWidget(makeBlock(formLabel(QStringLiteral("备注")), m_note));

    onAuthChanged();

    auto *cancel = new PillButton(QStringLiteral("取消"), PillButton::Ghost, this);
    connect(cancel, &QPushButton::clicked, this, &ConnectionFormPanel::cancelled);

    m_submit = new PillButton(QStringLiteral("创建"), PillButton::Primary, this);
    m_submit->setLeadingIcon(Icon::Check, 15);
    connect(m_submit, &QPushButton::clicked, this, &ConnectionFormPanel::submit);

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
    // 别让滚动区自己铺一层底色：表单要和弹窗 body 的底色连成一片。
    // 主题里 QSS 本来就会把它刷成透明，这行是双保险。
    m_formScroll->viewport()->setAutoFillBackground(false);

    m_footer = new QWidget(this);
    m_footer->setObjectName(QStringLiteral("FormFooter"));
    m_footer->setAttribute(Qt::WA_StyledBackground, true);
    auto *footerLayout = new QHBoxLayout(m_footer);
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
    m_footerShadow = new EdgeShadow(Qt::TopEdge, this);
    layout->addWidget(m_footerShadow);
    layout->addWidget(m_footer);

    // 和别的控件一样，回车提交。直接连 submit()，不要绕到那个可能被藏起来的
    // 主按钮上（弹窗里底部用的是自己的按钮，面板自己的那排是隐藏的）。
    for (QLineEdit *edit : { m_name, m_host, m_user, m_password, m_tags })
        connect(edit, &QLineEdit::returnPressed, this, &ConnectionFormPanel::submit);

    // 按界面上的阅读顺序走 Tab。字段的创建顺序和视觉顺序不一样（分组控件是
    // 早于表单创建的那批），不显式排的话 Tab 会从「名称」一下跳到最下面的分组。
    // 密码和密钥在同一个栈里，隐藏的那一页 Qt 会自动跳过。
    const QList<QWidget *> tabOrder = {
        m_name, m_host, m_port, m_user, m_auth,
        m_password, m_savePassword, m_key,
        m_rdpFullscreen, m_rdpMultiMon, m_rdpAdmin,
        m_group, m_tags, m_note
    };
    for (int i = 0; i + 1 < tabOrder.size(); ++i)
        QWidget::setTabOrder(tabOrder.at(i), tabOrder.at(i + 1));

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
    m_name->setFocus();
}

void ConnectionFormPanel::startNewWith(const Connection &seed)
{
    startNew();

    // 只抄字段，不抄 id：这条是「照着它再建一条」，不是「改它」
    Connection fields = seed;
    fields.id.clear();
    loadInto(fields);

    m_name->setFocus();
}

void ConnectionFormPanel::startEdit(const Connection &existing)
{
    m_isNew = false;
    m_result = existing;   // 带上 id / createdAt，commit 时不会丢

    m_title->setText(QStringLiteral("编辑连接"));
    m_submit->setText(m_submitOverride.isEmpty() ? QStringLiteral("保存") : m_submitOverride);

    refreshKeyList();
    loadInto(existing);
    m_name->setFocus();
}

void ConnectionFormPanel::setSubmitText(const QString &text)
{
    m_submitOverride = text;
    m_submit->setText(text);
}

QString ConnectionFormPanel::submitText() const
{
    return m_submit->text();
}

void ConnectionFormPanel::setProtocol(Protocol protocol)
{
    applyProtocol(protocol, true);
}

void ConnectionFormPanel::applyProtocol(Protocol protocol, bool adjustPort)
{
    const bool changed = (m_protocol != protocol);
    m_protocol = protocol;

    // 只在端口还是「另一边的默认值」时才换。用户手填过 2222 / 13389 这种
    // 就原样留着 —— 切换协议不该把人家改过的端口抹掉。
    if (adjustPort && changed) {
        const Protocol other = (protocol == Protocol::Rdp) ? Protocol::Ssh : Protocol::Rdp;
        if (m_port->value() == defaultPortFor(other))
            m_port->setValue(defaultPortFor(protocol));
    }

    const bool ssh = (protocol == Protocol::Ssh);
    if (m_authBlock)
        m_authBlock->setVisible(ssh);
    if (m_rdpOptionsBlock)
        m_rdpOptionsBlock->setVisible(!ssh);
    if (m_user) {
        m_user->setToolTip(ssh ? QString()
                               : QStringLiteral("支持 域\\用户 或 user@domain"));
    }

    // RDP 没有「认证方式」可选，强制回到密码页；切回 SSH 时再按下拉框恢复。
    onAuthChanged();

    if (changed)
        emit protocolChanged(protocol);
}

void ConnectionFormPanel::submit()
{
    // 校验、提示、saved() 都在这儿收口。弹窗底部的「创建 / 保存」、面板自己的
    // 按钮、输入框回车，全都走这一条路。
    if (commit())
        emit saved(m_result);
}

void ConnectionFormPanel::setHeaderVisible(bool visible)
{
    m_header->setVisible(visible);
    m_headerShadow->setVisible(visible);
}

void ConnectionFormPanel::setFooterVisible(bool visible)
{
    m_footer->setVisible(visible);
    m_footerShadow->setVisible(visible);
}

void ConnectionFormPanel::setContentMargins(const QMargins &margins)
{
    if (m_formBody && m_formBody->layout())
        m_formBody->layout()->setContentsMargins(margins);
}

QWidget *ConnectionFormPanel::lastField() const
{
    return m_note;
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
    // 预填不调整端口：文件里存的是什么就是什么
    applyProtocol(c.protocol, false);

    m_name->setText(c.name);
    m_host->setText(c.host);
    m_port->setValue(c.port > 0 ? c.port : defaultPortFor(c.protocol));
    m_user->setText(c.username);
    m_auth->setCurrentIndex(c.auth == AuthType::Password ? 0 : 1);
    m_password->setText(c.password);
    m_savePassword->setChecked(c.savePassword);
    m_rdpFullscreen->setChecked(c.rdpFullscreen);
    m_rdpMultiMon->setChecked(c.rdpMultiMon);
    m_rdpAdmin->setChecked(c.rdpAdmin);
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
    if (!m_auth || !m_secretStack)
        return;

    // RDP 只有密码这一种，认证方式控件本身也是隐藏的
    if (m_protocol == Protocol::Rdp) {
        m_secretStack->setCurrentIndex(0);
        m_secretLabel->setText(QStringLiteral("密码"));
        m_savePassword->setVisible(true);
        m_keyHint->setVisible(false);
        return;
    }

    const bool byPassword = (m_auth->currentIndex() == 0);

    // 密码 / 私钥在同一个位置换页：标签字、右侧小控件一起跟着变。
    // 索引 0 = 密码框，1 = 私钥选择区（KeyPicker）。
    m_secretStack->setCurrentIndex(byPassword ? 0 : 1);
    m_secretLabel->setText(byPassword ? QStringLiteral("密码")
                                      : QStringLiteral("私钥"));
    m_savePassword->setVisible(byPassword);
    m_keyHint->setVisible(!byPassword);
}

bool ConnectionFormPanel::commit()
{
    // 必填项：主机 / 端口 / 用户名。名称可以留空，留空时用主机兜底。
    const QString name = m_name->text().trimmed();
    const QString host = m_host->text().trimmed();
    if (host.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("还差一点"),
                             QStringLiteral("主机不能为空。"));
        m_host->setFocus();
        return false;
    }

    if (m_port->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("还差一点"),
                             QStringLiteral("端口不能为空。"));
        m_port->setFocus();
        return false;
    }

    // RDP 允许不填用户名：mstsc 自己会弹凭据框，或者用当前 Windows 账户
    const QString username = m_user->text().trimmed();
    if (m_protocol == Protocol::Ssh && username.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("还差一点"),
                             QStringLiteral("用户名不能为空。"));
        m_user->setFocus();
        return false;
    }

    m_result.name = name.isEmpty() ? host : name;
    m_result.host = host;
    m_result.protocol = m_protocol;
    m_result.port = m_port->value();
    m_result.username = username;
    m_result.password = m_password->text();
    m_result.savePassword = m_savePassword->isChecked();
    m_result.rdpFullscreen = m_rdpFullscreen->isChecked();
    m_result.rdpMultiMon = m_rdpMultiMon->isChecked();
    m_result.rdpAdmin = m_rdpAdmin->isChecked();

    if (m_protocol == Protocol::Rdp) {
        // RDP 走 mstsc，认证方式 / 密钥这些 SSH 字段不参与，清掉免得下次
        // 切回 SSH 时带出一条已经没意义的密钥引用
        m_result.auth = AuthType::Password;
        m_result.keyId.clear();
    } else {
        m_result.auth = m_auth->currentIndex() == 0 ? AuthType::Password
                                                    : AuthType::PublicKey;
        m_result.keyId = m_key->currentData().toString();
    }

    m_result.groupId = m_group->currentData().toString();
    m_result.note = m_note->toPlainText().trimmed();

    m_result.tags.clear();
    for (const QString &t : m_tags->text().split(QLatin1Char(' '), Qt::SkipEmptyParts))
        m_result.tags << t.trimmed();

    return true;
}

} // namespace Em
