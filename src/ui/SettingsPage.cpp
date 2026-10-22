#include "ui/SettingsPage.h"

#include "app/Theme.h"
#include "model/ConnectionStore.h"
#include "terminal/LocalSession.h"
#include "ui/IconProvider.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace Em {

namespace {

// QSettings 的键名集中放这儿，免得两边写岔
constexpr char kShellKey[]       = "terminal/shell";
constexpr char kFontSizeKey[]    = "terminal/fontSize";
constexpr char kBackgroundKey[]  = "appearance/backgroundImage";
constexpr char kBackgroundEnabledKey[] = "appearance/backgroundEnabled";
constexpr char kBackgroundDimKey[] = "appearance/backgroundDim";

// 蒙层默认 75%：图片还看得出来，但页面上的浅色文字不会糊在亮部里。
constexpr int kDefaultDim = 75;

QString dataDirectory()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.emshell");
    return base;
}

QString normalizedSuffix(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("png") || suffix == QLatin1String("jpg")
        || suffix == QLatin1String("jpeg") || suffix == QLatin1String("bmp")
        || suffix == QLatin1String("webp")) {
        return suffix;
    }
    return QStringLiteral("png");
}

} // namespace

// ---------------------------------------------------------------------------
// 背景图预览：在圆角框里铺一张缩略图，没图时显示「未设置背景图」。
//
// 没用 QLabel + setPixmap：QLabel 的 pixmap 不会跟着 QSS 的圆角裁，
// 四个角会从圆角边框里冒出来。这里自己画，裁切和边框都用同一个圆角路径。
// ---------------------------------------------------------------------------
class BackgroundPreview : public QWidget
{
public:
    explicit BackgroundPreview(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(220, 128);
    }

    void setImage(const QString &path)
    {
        m_pixmap = path.isEmpty() ? QPixmap() : QPixmap(path);
        update();
    }

    // 开关关掉时预览还在，只是压暗并盖一行「已停用」——
    // 否则用户会以为图片被清掉了。
    void setActive(bool active)
    {
        m_active = active;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRect box = rect().adjusted(0, 0, -1, -1);
        QPainterPath clip;
        clip.addRoundedRect(box, 10, 10);
        painter.setClipPath(clip);
        painter.fillRect(rect(), Theme::color(QStringLiteral("bg-input")));

        if (m_pixmap.isNull()) {
            painter.setPen(Theme::color(QStringLiteral("text-mute")));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("未设置背景图"));
        } else {
            // 铺满预览框，居中裁切 —— 和主窗口那边的画法一致
            const QPixmap scaled = m_pixmap.scaled(size(),
                                                   Qt::KeepAspectRatioByExpanding,
                                                   Qt::SmoothTransformation);
            const QRect source((scaled.width() - width()) / 2,
                               (scaled.height() - height()) / 2,
                               width(), height());
            painter.drawPixmap(rect(), scaled, source);

            if (!m_active) {
                QColor veil = Theme::color(QStringLiteral("bg-app"));
                veil.setAlphaF(0.62);
                painter.fillRect(rect(), veil);
                painter.setPen(Theme::color(QStringLiteral("text-dim")));
                painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("已停用"));
            }
        }

        QPen pen(Theme::color(QStringLiteral("border-strong")));
        pen.setWidth(1);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(box, 10, 10);
    }

private:
    QPixmap m_pixmap;
    bool m_active = true;
};

// ---------------------------------------------------------------------------
// 静态读取
// ---------------------------------------------------------------------------

QString SettingsPage::preferredShell()
{
    QSettings settings;
    return settings.value(QString::fromLatin1(kShellKey)).toString();
}

int SettingsPage::terminalFontSize()
{
    QSettings settings;
    return settings.value(QString::fromLatin1(kFontSizeKey), 14).toInt();
}

QString SettingsPage::backgroundImagePath()
{
    // 和 EMSHELL_THEME 一个路子：临时覆盖，方便截图和排查，不用动设置。
    const QString override = qEnvironmentVariable("EMSHELL_BACKGROUND_IMAGE");
    if (!override.isEmpty())
        return QFileInfo::exists(override) ? override : QString();

    QSettings settings;
    const QString path = settings.value(QString::fromLatin1(kBackgroundKey)).toString();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return QString();
    return path;
}

bool SettingsPage::backgroundEnabled()
{
    // 截图 / 调试时设了 EMSHELL_BACKGROUND_IMAGE 就直接当成启用，
    // 免得被用户设置里的 false 挡住（需要压测关闭态时用 EMSHELL_BACKGROUND_ENABLED=0）。
    if (!qEnvironmentVariable("EMSHELL_BACKGROUND_IMAGE").isEmpty())
        return qEnvironmentVariable("EMSHELL_BACKGROUND_ENABLED", "1") != QLatin1String("0");

    QSettings settings;
    return settings.value(QString::fromLatin1(kBackgroundEnabledKey), true).toBool();
}

int SettingsPage::backgroundDim()
{
    const QString override = qEnvironmentVariable("EMSHELL_BACKGROUND_DIM");
    if (!override.isEmpty()) {
        bool ok = false;
        const int value = override.toInt(&ok);
        if (ok)
            return qBound(0, value, 90);
    }

    QSettings settings;
    return qBound(0, settings.value(QString::fromLatin1(kBackgroundDimKey), kDefaultDim).toInt(), 90);
}

// ---------------------------------------------------------------------------
// 页面
// ---------------------------------------------------------------------------

SettingsPage::SettingsPage(ConnectionStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    auto *title = makeLabel(QStringLiteral("配置管理"), "pageTitle", this);
    auto *subtitle = makeLabel(QStringLiteral("外观、终端和数据的相关设置"), "pageSubtitle", this);

    // ---- 外观：配色 ----
    auto *appearanceCard = new Card(this);
    auto *appearanceTitle = makeLabel(QStringLiteral("配色"), "sectionTitle", appearanceCard);

    // 下拉框的内容不写死，直接照着 Theme 里的配色登记表生成 ——
    // 那边加一套新配色，这里自动就多一项，不用回来改。
    m_paletteBox = new SelectBox(appearanceCard);
    for (const Em::Theme::Palette &palette : Em::Theme::palettes())
        m_paletteBox->addItem(palette.label, palette.key);
    m_paletteBox->setCurrentIndex(m_paletteBox->findData(Em::Theme::paletteKey()));

    connect(m_paletteBox, &QComboBox::currentIndexChanged, this, [this] {
        Em::Theme::setPaletteKey(m_paletteBox->currentData().toString());
        emit statusMessage(QStringLiteral("配色已切换为「%1」")
                               .arg(m_paletteBox->currentText()));
    });

    auto *themeHint = makeLabel(
        QStringLiteral("切换后立即生效，不需要重启。"),
        "hint", appearanceCard);
    themeHint->setWordWrap(true);

    auto *appearanceForm = new QFormLayout;
    appearanceForm->setHorizontalSpacing(14);
    appearanceForm->setVerticalSpacing(10);
    appearanceForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    appearanceForm->addRow(makeLabel(QStringLiteral("配色"), "fieldLabel", appearanceCard),
                           m_paletteBox);

    auto *appearanceLayout = new QVBoxLayout(appearanceCard);
    appearanceLayout->setContentsMargins(20, 18, 20, 18);
    appearanceLayout->setSpacing(12);
    appearanceLayout->addWidget(appearanceTitle);
    appearanceLayout->addLayout(appearanceForm);
    appearanceLayout->addWidget(themeHint);

    // ---- 外观：背景图 ----
    auto *backgroundCard = new Card(this);
    auto *backgroundTitle = makeLabel(QStringLiteral("背景图"), "sectionTitle", backgroundCard);

    // 开关放在卡片标题右边：关掉之后图片和蒙层设置都留着，只是不再显示。
    m_backgroundEnabled = new CheckBox(QStringLiteral("启用背景图"), backgroundCard);
    m_backgroundEnabled->setObjectName(QStringLiteral("BackgroundEnabled"));
    connect(m_backgroundEnabled, &QCheckBox::toggled, this, [this](bool on) {
        QSettings settings;
        settings.setValue(QString::fromLatin1(kBackgroundEnabledKey), on);
        updateBackgroundUi();
        emit backgroundChanged();
        emit statusMessage(on ? QStringLiteral("背景图已启用") : QStringLiteral("背景图已停用"));
    });

    auto *backgroundTitleRow = new QHBoxLayout;
    backgroundTitleRow->setContentsMargins(0, 0, 0, 0);
    backgroundTitleRow->addWidget(backgroundTitle);
    backgroundTitleRow->addStretch();
    backgroundTitleRow->addWidget(m_backgroundEnabled);

    m_backgroundPreview = new BackgroundPreview(backgroundCard);

    m_backgroundFile = new ElidedLabel(QString(), backgroundCard);
    m_backgroundFile->setProperty("role", QStringLiteral("mono"));
    m_backgroundFile->setFont(Theme::monoFont(11));

    auto *backgroundHint = makeLabel(
        QStringLiteral("图片会铺满整个窗口（包括左侧菜单和标题栏）。上面盖的那层蒙层可以调，"
                       "保证浅色文字在亮部也看得清。支持 PNG / JPG / BMP / WebP。"),
        "hint", backgroundCard);
    backgroundHint->setWordWrap(true);

    auto *chooseButton = new PillButton(QStringLiteral("选择图片…"), PillButton::Ghost,
                                        backgroundCard);
    chooseButton->setLeadingIcon(Icon::Folder, 15);

    m_clearBackground = new PillButton(QStringLiteral("移除背景"), PillButton::Ghost,
                                       backgroundCard);
    m_clearBackground->setLeadingIcon(Icon::Trash, 15);

    connect(chooseButton, &QPushButton::clicked, this, &SettingsPage::chooseBackground);
    connect(m_clearBackground, &QPushButton::clicked, this, &SettingsPage::clearBackground);

    auto *backgroundButtons = new QHBoxLayout;
    backgroundButtons->setContentsMargins(0, 0, 0, 0);
    backgroundButtons->setSpacing(9);
    backgroundButtons->addWidget(chooseButton);
    backgroundButtons->addWidget(m_clearBackground);
    backgroundButtons->addStretch();

    auto *backgroundInfo = new QVBoxLayout;
    backgroundInfo->setContentsMargins(0, 0, 0, 0);
    backgroundInfo->setSpacing(8);
    backgroundInfo->addWidget(m_backgroundFile);
    backgroundInfo->addWidget(backgroundHint);
    backgroundInfo->addLayout(backgroundButtons);
    backgroundInfo->addStretch();

    auto *backgroundTop = new QHBoxLayout;
    backgroundTop->setContentsMargins(0, 0, 0, 0);
    backgroundTop->setSpacing(16);
    backgroundTop->addWidget(m_backgroundPreview);
    backgroundTop->addLayout(backgroundInfo, 1);

    // 蒙层浓度。0 = 原图，90 = 几乎只剩底色。
    m_dimSlider = new QSlider(Qt::Horizontal, backgroundCard);
    m_dimSlider->setRange(0, 90);
    m_dimSlider->setValue(backgroundDim());
    m_dimSlider->setFixedWidth(240);

    m_dimValue = makeLabel(QStringLiteral("%1%").arg(backgroundDim()), "hint", backgroundCard);
    m_dimValue->setFixedWidth(40);

    connect(m_dimSlider, &QSlider::valueChanged, this, [this](int value) {
        QSettings settings;
        settings.setValue(QString::fromLatin1(kBackgroundDimKey), value);
        m_dimValue->setText(QStringLiteral("%1%").arg(value));
        emit backgroundChanged();
    });

    auto *dimRow = new QHBoxLayout;
    dimRow->setContentsMargins(0, 0, 0, 0);
    dimRow->setSpacing(12);
    dimRow->addWidget(makeLabel(QStringLiteral("蒙层浓度"), "fieldLabel", backgroundCard));
    dimRow->addWidget(m_dimSlider);
    dimRow->addWidget(m_dimValue);
    dimRow->addStretch();

    auto *backgroundLayout = new QVBoxLayout(backgroundCard);
    backgroundLayout->setContentsMargins(20, 18, 20, 18);
    backgroundLayout->setSpacing(14);
    backgroundLayout->addLayout(backgroundTitleRow);
    backgroundLayout->addLayout(backgroundTop);
    backgroundLayout->addLayout(dimRow);

    // ---- 基本：终端 ----
    auto *terminalCard = new Card(this);
    auto *terminalTitle = makeLabel(QStringLiteral("终端"), "sectionTitle", terminalCard);

    m_shellBox = new SelectBox(terminalCard);
    m_shellBox->addItem(QStringLiteral("自动选择（优先 PowerShell 7）"), QString());
    for (const QString &shell : LocalSession::availableShells())
        m_shellBox->addItem(QStringLiteral("%1 — %2").arg(QFileInfo(shell).baseName(), shell), shell);

    {
        const QString saved = preferredShell();
        const int index = m_shellBox->findData(saved);
        m_shellBox->setCurrentIndex(index >= 0 ? index : 0);
    }

    connect(m_shellBox, &QComboBox::currentIndexChanged, this, [this] {
        QSettings settings;
        settings.setValue(QString::fromLatin1(kShellKey), m_shellBox->currentData().toString());
        emit statusMessage(QStringLiteral("默认终端已更新，下次开新标签生效"));
    });

    m_fontSizeBox = new QSpinBox(terminalCard);
    m_fontSizeBox->setRange(10, 28);
    m_fontSizeBox->setValue(terminalFontSize());
    m_fontSizeBox->setSuffix(QStringLiteral(" px"));

    connect(m_fontSizeBox, &QSpinBox::valueChanged, this, [this](int value) {
        QSettings settings;
        settings.setValue(QString::fromLatin1(kFontSizeKey), value);
        emit terminalFontSizeChanged(value);
    });

    auto *terminalForm = new QFormLayout;
    terminalForm->setHorizontalSpacing(14);
    terminalForm->setVerticalSpacing(10);
    terminalForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    terminalForm->addRow(makeLabel(QStringLiteral("默认 Shell"), "fieldLabel", terminalCard),
                         m_shellBox);
    terminalForm->addRow(makeLabel(QStringLiteral("终端字号"), "fieldLabel", terminalCard),
                         m_fontSizeBox);

    auto *terminalHint = makeLabel(
        QStringLiteral("终端里也可以直接用 Ctrl + Shift + + / - 临时缩放字号。"),
        "hint", terminalCard);

    auto *terminalLayout = new QVBoxLayout(terminalCard);
    terminalLayout->setContentsMargins(18, 16, 18, 18);
    terminalLayout->setSpacing(12);
    terminalLayout->addWidget(terminalTitle);
    terminalLayout->addLayout(terminalForm);
    terminalLayout->addWidget(terminalHint);

    // ---- 基本：数据 ----
    auto *dataCard = new Card(this);
    auto *dataTitle = makeLabel(QStringLiteral("数据"), "sectionTitle", dataCard);

    // 这两个路径比卡片窄，普通 QLabel 的 minimumSizeHint 按整段文字算，
    // 塞进 QFormLayout 后会被压成「C:/Users/bs/」——后半截直接没了。
    // 用 ElidedLabel + Expanding：宽度跟着卡片走，放不下时显示省略号，tooltip 里是完整路径。
    auto *pathLabel = new ElidedLabel(dataDirectory(), dataCard);
    pathLabel->setProperty("role", QStringLiteral("mono"));
    pathLabel->setFont(Theme::monoFont(12));
    pathLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    pathLabel->setToolTip(QDir::toNativeSeparators(dataDirectory()));

    auto *fileLabel = new ElidedLabel(m_store ? m_store->filePath() : QString(), dataCard);
    fileLabel->setProperty("role", QStringLiteral("mono"));
    fileLabel->setFont(Theme::monoFont(11));
    fileLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    if (m_store)
        fileLabel->setToolTip(QDir::toNativeSeparators(m_store->filePath()));

    auto *openButton = new PillButton(QStringLiteral("打开目录"), PillButton::Ghost, dataCard);
    openButton->setLeadingIcon(Icon::Folder, 15);
    connect(openButton, &QPushButton::clicked, this, &SettingsPage::openDataFolder);

    auto *clearButton = new PillButton(QStringLiteral("清空历史"), PillButton::Ghost, dataCard);
    clearButton->setLeadingIcon(Icon::Trash, 15);
    connect(clearButton, &QPushButton::clicked, this, [this] {
        if (!m_store || m_store->history().isEmpty())
            return;
        const auto answer = QMessageBox::question(
            this, QStringLiteral("清空历史"),
            QStringLiteral("确定清空 %1 条连接记录吗？").arg(m_store->history().size()));
        if (answer == QMessageBox::Yes) {
            m_store->clearHistory();
            emit statusMessage(QStringLiteral("历史记录已清空"));
        }
    });

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(9);
    buttonRow->addWidget(openButton);
    buttonRow->addWidget(clearButton);
    buttonRow->addStretch();

    auto *dataForm = new QFormLayout;
    dataForm->setHorizontalSpacing(14);
    dataForm->setVerticalSpacing(10);
    dataForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    dataForm->addRow(makeLabel(QStringLiteral("数据目录"), "fieldLabel", dataCard), pathLabel);
    dataForm->addRow(makeLabel(QStringLiteral("连接文件"), "fieldLabel", dataCard), fileLabel);

    auto *dataLayout = new QVBoxLayout(dataCard);
    dataLayout->setContentsMargins(18, 16, 18, 18);
    dataLayout->setSpacing(12);
    dataLayout->addWidget(dataTitle);
    dataLayout->addLayout(dataForm);
    dataLayout->addLayout(buttonRow);

    // ---- 基本：关于 ----
    auto *aboutCard = new Card(this);
    auto *aboutTitle = makeLabel(QStringLiteral("关于"), "sectionTitle", aboutCard);
    // 版本号取 applicationVersion()，别在这儿写死 —— 写死过一次，改版本时漏了这里
    auto *aboutText = makeLabel(
        QStringLiteral("EmShell v%1\n\n"
                       "连接密码用 Windows DPAPI 加密后写入配置文件，密文绑定当前 Windows "
                       "账户，换机器或换账户就解不开（这是有意为之）。\n\n"
                       "SSH 走的是内置的原生实现（libssh2），不依赖系统装没装 OpenSSH，"
                       "所以密码可以自动填写，也能用密钥登录。")
            .arg(QCoreApplication::applicationVersion()),
        "hint", aboutCard);
    aboutText->setWordWrap(true);

    auto *aboutLayout = new QVBoxLayout(aboutCard);
    aboutLayout->setContentsMargins(18, 16, 18, 18);
    aboutLayout->setSpacing(9);
    aboutLayout->addWidget(aboutTitle);
    aboutLayout->addWidget(aboutText);

    // ---- 选项卡 ----
    // 不用 QTabWidget：它的页签尺寸、内边距和选中态在深色主题下很难和侧栏对齐。
    // 两个可勾选按钮 + QStackedWidget，样式完全由 app.qss 控制。
    auto *tabBar = new QWidget(this);
    tabBar->setObjectName(QStringLiteral("SettingsTabs"));
    tabBar->setAttribute(Qt::WA_StyledBackground, true);
    tabBar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    const auto makeTab = [this](const QString &text, const QString &objectName) {
        auto *button = new QPushButton(text, this);
        button->setObjectName(objectName);
        button->setProperty("role", QStringLiteral("segment"));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };

    m_basicTab = makeTab(QStringLiteral("基本设置"), QStringLiteral("BasicTab"));
    m_appearanceTab = makeTab(QStringLiteral("外观设置"), QStringLiteral("AppearanceTab"));

    auto *tabBarLayout = new QHBoxLayout(tabBar);
    tabBarLayout->setContentsMargins(3, 3, 3, 3);
    tabBarLayout->setSpacing(3);
    tabBarLayout->addWidget(m_basicTab);
    tabBarLayout->addWidget(m_appearanceTab);

    auto *tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(0, 0, 0, 0);
    tabRow->addWidget(tabBar);
    tabRow->addStretch();

    auto *basicPage = new QWidget;
    auto *basicLayout = new QVBoxLayout(basicPage);
    basicLayout->setContentsMargins(0, 0, 0, 0);
    basicLayout->setSpacing(16);
    basicLayout->addWidget(terminalCard);
    basicLayout->addWidget(dataCard);
    basicLayout->addWidget(aboutCard);
    basicLayout->addStretch();

    auto *appearancePage = new QWidget;
    auto *appearancePageLayout = new QVBoxLayout(appearancePage);
    appearancePageLayout->setContentsMargins(0, 0, 0, 0);
    appearancePageLayout->setSpacing(16);
    appearancePageLayout->addWidget(appearanceCard);
    appearancePageLayout->addWidget(backgroundCard);
    appearancePageLayout->addStretch();

    m_tabStack = new QStackedWidget(this);
    m_tabStack->addWidget(basicPage);
    m_tabStack->addWidget(appearancePage);
    // 高度跟当前页走。QStackedWidget 默认取所有页里最高的那个，
    // 切到较矮的外观页时下面会空一大块。
    m_tabStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    const auto selectTab = [this](int index) {
        m_basicTab->setChecked(index == 0);
        m_appearanceTab->setChecked(index == 1);
        m_tabStack->setCurrentIndex(index);

        // 高度改成当前页的。延迟到事件循环：构造的时候样式还没 polish，
        // 现在量出来的 sizeHint 会偏小（QSS 的 min-height / padding 还没算进去），
        // 切过去会看到卡片被裁掉一截。
        QTimer::singleShot(0, this, [this, index] {
            if (m_tabStack->currentIndex() != index)
                return;
            if (QWidget *page = m_tabStack->widget(index)) {
                m_tabStack->setFixedHeight(qMax(page->sizeHint().height(),
                                                page->minimumSizeHint().height()));
            }
        });
    };

    connect(m_basicTab, &QPushButton::clicked, this, [selectTab] { selectTab(0); });
    connect(m_appearanceTab, &QPushButton::clicked, this, [selectTab] { selectTab(1); });
    selectTab(0);

    // ---- 组装 ----
    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(6, 22, 26, 22);
    bodyLayout->setSpacing(16);
    bodyLayout->addWidget(title);
    bodyLayout->addWidget(subtitle);
    bodyLayout->addSpacing(6);
    bodyLayout->addLayout(tabRow);
    bodyLayout->addWidget(m_tabStack);
    bodyLayout->addStretch();

    auto *scroll = new QScrollArea(this);
    scroll->setWidget(body);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(scroll);

    updateBackgroundUi();
}

void SettingsPage::openDataFolder()
{
    const QString dir = dataDirectory();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir)))
        emit statusMessage(QStringLiteral("打不开目录：%1").arg(dir));
}

void SettingsPage::chooseBackground()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择背景图片"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (file.isEmpty())
        return;

    // 先确认它真的是张图。只看后缀不够 —— 改名的 HTML 也能选进来。
    QImageReader reader(file);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        QMessageBox::warning(this, QStringLiteral("打不开图片"),
                             QStringLiteral("这个文件不是可用的图片：\n%1")
                                 .arg(QDir::toNativeSeparators(file)));
        return;
    }

    const QString dirPath = dataDirectory();
    if (!QDir().mkpath(dirPath)) {
        QMessageBox::warning(this, QStringLiteral("存不下来"),
                             QStringLiteral("建不出数据目录：\n%1")
                                 .arg(QDir::toNativeSeparators(dirPath)));
        return;
    }

    const QString dest =
        QDir(dirPath).filePath(QStringLiteral("background.") + normalizedSuffix(file));

    // 用户可能选的正好是上次复制进来的那张，那就没有复制这回事
    if (QFileInfo(file).absoluteFilePath().compare(QFileInfo(dest).absoluteFilePath(),
                                                   Qt::CaseInsensitive)
        != 0) {
        // 先复制到 .tmp，成功了再替换 —— 复制失败时旧背景还在，
        // 不会出现「设置里指着一个已经被删掉的文件」。
        const QString staging = dest + QStringLiteral(".tmp");
        QFile::remove(staging);
        if (!QFile::copy(file, staging)) {
            QMessageBox::warning(this, QStringLiteral("复制失败"),
                                 QStringLiteral("没能把图片复制到数据目录：\n%1")
                                     .arg(QDir::toNativeSeparators(dirPath)));
            return;
        }
        QFile::remove(dest);
        if (!QFile::rename(staging, dest)) {
            QFile::remove(staging);
            QMessageBox::warning(this, QStringLiteral("复制失败"),
                                 QStringLiteral("没能把图片放到数据目录：\n%1")
                                     .arg(QDir::toNativeSeparators(dirPath)));
            return;
        }
    }

    // 换了格式之后旧的 background.jpg / .png 就别留着了
    const QFileInfoList olds =
        QDir(dirPath).entryInfoList({QStringLiteral("background.*")}, QDir::Files);
    for (const QFileInfo &old : olds) {
        if (old.absoluteFilePath().compare(QFileInfo(dest).absoluteFilePath(),
                                           Qt::CaseInsensitive)
            != 0) {
            QFile::remove(old.absoluteFilePath());
        }
    }

    QSettings settings;
    settings.setValue(QString::fromLatin1(kBackgroundKey), dest);
    // 新选一张图默认就是启用的，不用用户再回来勾一次开关
    settings.setValue(QString::fromLatin1(kBackgroundEnabledKey), true);

    updateBackgroundUi();
    emit backgroundChanged();
    emit statusMessage(QStringLiteral("背景图已更新"));
}

void SettingsPage::clearBackground()
{
    QSettings settings;
    settings.remove(QString::fromLatin1(kBackgroundKey));
    settings.remove(QString::fromLatin1(kBackgroundEnabledKey));

    const QDir dir(dataDirectory());
    const QFileInfoList olds =
        dir.entryInfoList({QStringLiteral("background.*")}, QDir::Files);
    for (const QFileInfo &old : olds)
        QFile::remove(old.absoluteFilePath());

    updateBackgroundUi();
    emit backgroundChanged();
    emit statusMessage(QStringLiteral("背景图已移除"));
}

void SettingsPage::updateBackgroundUi()
{
    const QString path = backgroundImagePath();
    const bool hasImage = !path.isEmpty();
    const bool enabled = hasImage && backgroundEnabled();

    m_backgroundPreview->setImage(path);
    m_backgroundPreview->setActive(enabled);

    {
        // 这个函数也会被开关自己的 toggled 调用，别再触发一次
        QSignalBlocker blocker(m_backgroundEnabled);
        m_backgroundEnabled->setChecked(enabled);
    }
    m_backgroundEnabled->setEnabled(hasImage);

    m_clearBackground->setEnabled(hasImage);
    m_dimSlider->setEnabled(hasImage);

    m_backgroundFile->setFullText(path.isEmpty() ? QStringLiteral("未设置")
                                                 : QFileInfo(path).fileName());
    m_backgroundFile->setToolTip(path.isEmpty() ? QString() : QDir::toNativeSeparators(path));
    m_dimValue->setText(QStringLiteral("%1%").arg(m_dimSlider->value()));
}

} // namespace Em
