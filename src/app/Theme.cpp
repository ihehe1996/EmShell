#include "app/Theme.h"

#include <QFile>
#include <QFontDatabase>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <algorithm>

namespace Em {

namespace {

// 存当前配色的 QSettings 键。叫 theme 不叫 palette 是历史原因，别改 ——
// 改了会把用户已经选好的配色丢掉（读出来是空，然后退回默认）。
constexpr char kModeKey[] = "ui/theme";

// 把 token 表里的色值解析成 QColor。
//
// QColor 的字符串构造函数**不认识 CSS 的 rgba(...)**（Qt 6 只认 #RRGGBB、
// #AARRGGBB 和 SVG 颜色名），而带背景图之后，面板 / 输入框 / 悬浮态这些 token
// 全都改成了 rgba 写法。不在这里拆一次，Theme::color("bg-panel") 会解析成
// 黑色，QSS 里的颜色却是对的 —— 表面上看不出来，直到某处代码要取颜色才露馅。
//
// alpha 支持两种写法：0.0~1.0 的小数（和 QSS 一致），以及 0~255 的整数。
QColor parseTokenColor(const QString &value)
{
    const QString text = value.trimmed();
    if (text.startsWith(QLatin1String("rgba("), Qt::CaseInsensitive)
        && text.endsWith(QLatin1Char(')'))) {
        const QStringList parts = text.mid(5, text.size() - 6).split(QLatin1Char(','));
        if (parts.size() == 4) {
            bool okR = false, okG = false, okB = false, okA = false;
            const int r = parts.at(0).trimmed().toInt(&okR);
            const int g = parts.at(1).trimmed().toInt(&okG);
            const int b = parts.at(2).trimmed().toInt(&okB);
            const double a = parts.at(3).trimmed().toDouble(&okA);
            if (okR && okG && okB && okA) {
                const int alpha = a <= 1.0 ? qRound(a * 255.0) : qRound(a);
                return QColor(qBound(0, r, 255), qBound(0, g, 255),
                              qBound(0, b, 255), qBound(0, alpha, 255));
            }
        }
    }
    return QColor(text);
}

// ---------------------------------------------------------------------------
// 浅色主题（默认）
//
// 思路：三档白 —— 页面底是最浅的冷灰，卡片是纯白，靠这一点点明度差把层次
// 拉开，不用阴影。强调色只用在真正需要引导视线的地方（主按钮、选中项、
// 图标），其余全部走中性灰，界面才干净。
// ---------------------------------------------------------------------------
const QHash<QString, QString> &lightTable()
{
    // -----------------------------------------------------------------------
    // 「带色轨道」配色
    //
    // 层次靠**三块表面的色相差**拿，不靠描边和阴影：
    //     侧栏  #E9EEFA   淡紫蓝的轨道，最"有色"
    //     画布  #F7F8FC   近白，中间层
    //     卡片  #FFFFFF   纯白，浮在最上面
    // 三块排在一起就有纵深，不用再靠阴影硬撑。
    //
    // 侧栏上的交互态**必须和卡片上的分开**：
    //     卡片在白色上，hover 要往**深**里走（bg-hover，淡淡的灰蓝）
    //     侧栏在有色轨道上，hover 要更深、而选中要**纯白浮起来**（nav-*）
    // 混用的话，有色底上的白卡片和选中块会分不清。
    // -----------------------------------------------------------------------
    static const QHash<QString, QString> table = {
        // 表面
        { QStringLiteral("bg-app"),       QStringLiteral("#F7F8FC") },
        // 底部状态条。侧栏改用 nav-bg 之后，这个 token 就只服务状态条了，
        // 名字跟着改掉，免得以后有人以为它是侧栏的底色。
        //
        // **凡是盖在背景图上的表面都用 rgba**（面板、输入框、标题栏…）：
        // 用户明确要求「不允许有元素彻底盖住背景图」。透明度都不高（84%~94%），
        // 没有背景图时看起来和原来的实色几乎一样。
        { QStringLiteral("bg-panel"),     QStringLiteral("rgba(255, 255, 255, 0.86)") },
        // 菜单 / 气泡 / 弹窗是独立的顶层窗口，背后不是背景图而是桌面 —— 保持实色
        { QStringLiteral("bg-elevated"),  QStringLiteral("#FFFFFF") },
        { QStringLiteral("bg-input"),     QStringLiteral("rgba(255, 255, 255, 0.90)") },
        { QStringLiteral("bg-hover"),     QStringLiteral("rgba(21, 27, 43, 0.06)") },
        { QStringLiteral("bg-active"),    QStringLiteral("rgba(21, 27, 43, 0.10)") },

        // ---- 侧栏是**深色轨道**，它整条链路要自己一套 token ----
        //
        // 不能复用 text-dim / text-mute：那套是给白底用的深色字，放到深底上
        // 就是黑底黑字。同理 hover 和选中也不能沿用卡片那套 —— 深色底上
        // 「更亮」才看成选中，「更暗」会看成按下。
        { QStringLiteral("nav-bg"),           QStringLiteral("#161A23") },
        // 设了背景图时侧栏用它：半透明的轨道，底下的图能透上来，
        // 但压得够暗，导航文字仍然清楚。没设背景图时还是上面那个实心 nav-bg。
        { QStringLiteral("nav-bg-glass"),     QStringLiteral("rgba(22, 26, 35, 0.45)") },
        { QStringLiteral("nav-line"),         QStringLiteral("#252C3D") },
        { QStringLiteral("nav-text"),         QStringLiteral("#a0a8b8") },
        { QStringLiteral("nav-text-hover"),   QStringLiteral("#f0f2f7") },
        { QStringLiteral("nav-text-active"),  QStringLiteral("#4F8CFF") },
        // 悬浮 / 按住 / 当前页**共用这一个底色**。
        // 三者只靠文字颜色区分：当前页是强调蓝、悬浮是近白、平常是灰。
        // nav-hover-bg 留给数字角标的悬浮态和滚动条，那两个要更含蓄。
        { QStringLiteral("nav-hover-bg"),     QStringLiteral("#262E40") },
        // 悬浮态用半透明白：底下的背景图能透出来一点，不再是一块实心色
        { QStringLiteral("nav-highlight-bg"), QStringLiteral("rgba(255, 255, 255, 0.05)") },
        // 当前页单独的底色：强调蓝压到 12% 透明度，压在侧栏底色上就是一层很淡的
        // 同色雾。**和 nav-highlight-bg（悬浮/按住）分开**，那两个不要跟着变蓝。
        { QStringLiteral("nav-active-bg"),    QStringLiteral("rgba(79, 140, 255, 0.12)") },
        { QStringLiteral("nav-group"),        QStringLiteral("#6B7590") },
        { QStringLiteral("nav-count-bg"),     QStringLiteral("rgba(42, 51, 70, 0.72)") },
        { QStringLiteral("nav-count-text"),   QStringLiteral("#9AA4BB") },
        // 侧栏顶上那个「+ 新建连接」。整条侧栏唯一的实心强调色，一眼能找到。
        // 也留一点透明度：不彻底盖住背景图，但视觉上仍然是实心色块。
        // 实色。设了背景图时由 SideBar::setGlassOpacity() 按「蒙层浓度」动态加 alpha
        { QStringLiteral("nav-cta-bg"),       QStringLiteral("#4f8cff") },
        { QStringLiteral("nav-cta-bg-hover"), QStringLiteral("#5588F0") },
        { QStringLiteral("nav-cta-text"),     QStringLiteral("#FFFFFF") },

        // 描边：底色是冷的，描边也跟着偏冷，别用中性灰
        { QStringLiteral("border"),        QStringLiteral("#E2E7F3") },
        { QStringLiteral("border-strong"), QStringLiteral("#C9D2E6") },
        // 原生标题栏下面那条分隔线（在标题栏延伸条的最底部）
        { QStringLiteral("titlebar-line"), QStringLiteral("#E2E7F3") },
        // 自绘标题栏 / 原生标题栏下面的延伸条
        { QStringLiteral("titlebar-bg"),   QStringLiteral("rgba(247, 248, 252, 0.90)") },
        // 标题栏右上角三个窗口按钮：悬浮时图标变成这个颜色
        { QStringLiteral("titlebar-icon-hover"), QStringLiteral("#151B2B") },
        { QStringLiteral("edge-shadow"),   QStringLiteral("#0D000000") },

        // 文字：三级灰阶。text-mute 比常规深一点 —— 它要落在有色轨道上，
        // 太淡在 #E9EEFA 上会糊
        { QStringLiteral("text"),      QStringLiteral("#151B2B") },
        { QStringLiteral("text-dim"),  QStringLiteral("#4A5470") },
        { QStringLiteral("text-mute"), QStringLiteral("#737C99") },

        // 强调色：靛蓝偏紫。原来那个 #3D5AE0 是典型的「网页蓝」，亮而平
        { QStringLiteral("accent"),       QStringLiteral("rgba(59, 73, 216, 0.94)") },
        { QStringLiteral("accent-hover"), QStringLiteral("rgba(49, 64, 194, 0.94)") },
        { QStringLiteral("accent-press"), QStringLiteral("rgba(41, 55, 166, 0.94)") },
        { QStringLiteral("accent-soft"),  QStringLiteral("rgba(59, 73, 216, 0.10)") },
        { QStringLiteral("accent-line"),  QStringLiteral("#C0C9F4") },
        { QStringLiteral("accent-text"),  QStringLiteral("#3542CC") },

        // 状态色
        { QStringLiteral("ok"),       QStringLiteral("#0F9D58") },
        { QStringLiteral("warn"),     QStringLiteral("#A85F06") },
        { QStringLiteral("err"),      QStringLiteral("#D92D20") },
        { QStringLiteral("ok-soft"),  QStringLiteral("rgba(15, 157, 88, 0.12)") },
        { QStringLiteral("err-soft"), QStringLiteral("rgba(217, 45, 32, 0.10)") },

        // --- 页头：普通就好 ---
        { QStringLiteral("brand-bg"),        QStringLiteral("#FFFFFF") },
        { QStringLiteral("brand-line"),      QStringLiteral("#EAEAEE") },
        { QStringLiteral("brand-text"),      QStringLiteral("#2B2733") },
        { QStringLiteral("brand-sub"),       QStringLiteral("#8A8494") },
        { QStringLiteral("win-hover"),       QStringLiteral("rgba(21, 27, 43, 0.06)") },
        { QStringLiteral("win-close-hover"), QStringLiteral("rgba(217, 45, 32, 0.10)") },

        // 终端标签栏和终端区块是**两套配色共用的固定深色**（用户指定的值）：
        //   标签栏 #1F2430，选中标签 / 终端 #282C34，分隔线和底边 #0B0D12。
        // 所以这几个 token 在浅色主题里也照样是深色；标签文字要用 nav-text
        // 那套给深色轨道准备的颜色，不能再用 text-dim（浅色下是深灰，看不见）。
        { QStringLiteral("term-rail-bg"),        QStringLiteral("#1F2430") },
        { QStringLiteral("term-rail-line"),      QStringLiteral("#0B0D12") },
        { QStringLiteral("term-tab-hover-bg"),   QStringLiteral("rgba(255, 255, 255, 0.06)") },
        { QStringLiteral("term-tab-sep"),        QStringLiteral("#0B0D12") },
        { QStringLiteral("term-tab-active-bg"),  QStringLiteral("#282C34") },
        { QStringLiteral("term-tab-close-line"), QStringLiteral("#3A3F4B") },
        // 标签名前面的状态圆点：绿 = 连接中/正常，红 = 已断开
        { QStringLiteral("term-tab-ok"),         QStringLiteral("#3FB950") },
        { QStringLiteral("term-tab-err"),        QStringLiteral("#F0553D") },

        // 终端背景保持**全透明**：文字直接画在页面背景（背景图 / 画布）上。
        // 标签栏和选中的标签仍然是不透明的固定深色，这两块不受影响。
        { QStringLiteral("term-bg"),     QStringLiteral("rgba(0, 0, 0, 0)") },
        { QStringLiteral("term-fg"),     QStringLiteral("#D8DCE4") },
        { QStringLiteral("term-cursor"), QStringLiteral("#5C7CFA") },
        { QStringLiteral("term-sel"),    QStringLiteral("#3450CC") },
    };
    return table;
}

// ---------------------------------------------------------------------------
// 深色主题
// ---------------------------------------------------------------------------
const QHash<QString, QString> &darkTable()
{
    static const QHash<QString, QString> table = {
        // 页面右侧（画布）的底色。比侧栏的 #1D212B 还深一档，
        // 让深色轨道仍旧比内容区亮，层次不会糊成一片。
        { QStringLiteral("bg-app"),       QStringLiteral("#12151C") },
        // 和浅色同理：所有盖在背景图上的表面都留一点透明度。
        // bg-elevated 是菜单 / 气泡 / 弹窗的底，背后是桌面不是背景图，保持实色。
        { QStringLiteral("bg-panel"),     QStringLiteral("rgba(35, 40, 51, 0.82)") },
        { QStringLiteral("bg-elevated"),  QStringLiteral("#2A303D") },
        { QStringLiteral("bg-input"),     QStringLiteral("rgba(30, 34, 44, 0.88)") },
        { QStringLiteral("bg-hover"),     QStringLiteral("rgba(190, 205, 235, 0.12)") },
        { QStringLiteral("bg-active"),    QStringLiteral("rgba(58, 67, 86, 0.72)") },

        { QStringLiteral("nav-bg"),           QStringLiteral("#1D212B") },
        { QStringLiteral("nav-bg-glass"),     QStringLiteral("rgba(29, 33, 43, 0.45)") },
        { QStringLiteral("nav-line"),         QStringLiteral("#2A3140") },
        { QStringLiteral("nav-text"),         QStringLiteral("#9BA3B4") },
        { QStringLiteral("nav-text-hover"),   QStringLiteral("#E8EAF0") },
        { QStringLiteral("nav-text-active"),  QStringLiteral("#4F8CFF") },
        { QStringLiteral("nav-hover-bg"),     QStringLiteral("#2C3341") },
        { QStringLiteral("nav-highlight-bg"), QStringLiteral("rgba(255, 255, 255, 0.05)") },
        { QStringLiteral("nav-active-bg"),    QStringLiteral("rgba(79, 140, 255, 0.12)") },
        { QStringLiteral("nav-group"),        QStringLiteral("#6B7385") },
        { QStringLiteral("nav-count-bg"),     QStringLiteral("rgba(58, 67, 86, 0.72)") },
        { QStringLiteral("nav-count-text"),   QStringLiteral("#9BA3B4") },
        // 「+ 新建连接」按钮。它是个品牌色，两套配色用同一组值
        { QStringLiteral("nav-cta-bg"),       QStringLiteral("#679BFF") },
        { QStringLiteral("nav-cta-bg-hover"), QStringLiteral("#5588F0") },
        { QStringLiteral("nav-cta-text"),     QStringLiteral("#FFFFFF") },

        { QStringLiteral("border"),        QStringLiteral("#2E3542") },
        { QStringLiteral("border-strong"), QStringLiteral("#3D4657") },
        // 原生标题栏下面那条分隔线（在标题栏延伸条的最底部）
        { QStringLiteral("titlebar-line"), QStringLiteral("#22262F") },
        { QStringLiteral("titlebar-bg"),   QStringLiteral("rgba(18, 21, 28, 0.88)") },
        // 标题栏右上角三个窗口按钮：悬浮时图标变成纯白
        { QStringLiteral("titlebar-icon-hover"), QStringLiteral("#FFFFFF") },

        // 深色下背景本来就暗，阴影要比浅色重不少才看得出来
        { QStringLiteral("edge-shadow"), QStringLiteral("#36000000") },

        { QStringLiteral("text"),      QStringLiteral("#E8EAF0") },
        { QStringLiteral("text-dim"),  QStringLiteral("#9BA3B4") },
        { QStringLiteral("text-mute"), QStringLiteral("#bfbfbf") },

        { QStringLiteral("accent"),       QStringLiteral("rgba(91, 124, 250, 0.94)") },
        { QStringLiteral("accent-hover"), QStringLiteral("rgba(114, 144, 255, 0.94)") },
        { QStringLiteral("accent-press"), QStringLiteral("rgba(74, 104, 216, 0.94)") },
        { QStringLiteral("accent-soft"),  QStringLiteral("rgba(91, 124, 250, 0.14)") },
        { QStringLiteral("accent-line"),  QStringLiteral("#3A4770") },
        { QStringLiteral("accent-text"),  QStringLiteral("#9DB4FF") },

        { QStringLiteral("ok"),       QStringLiteral("#3FB950") },
        { QStringLiteral("warn"),     QStringLiteral("#D6A32B") },
        { QStringLiteral("err"),      QStringLiteral("#F0553D") },
        { QStringLiteral("ok-soft"),  QStringLiteral("rgba(63, 185, 80, 0.14)") },
        { QStringLiteral("err-soft"), QStringLiteral("rgba(240, 85, 61, 0.16)") },

        // 页头：深色下的对应版本
        { QStringLiteral("brand-bg"),        QStringLiteral("#1E1C24") },
        { QStringLiteral("brand-line"),      QStringLiteral("#2E2B36") },
        { QStringLiteral("brand-text"),      QStringLiteral("#F0EDF2") },
        { QStringLiteral("brand-sub"),       QStringLiteral("#8A8494") },
        { QStringLiteral("win-hover"),       QStringLiteral("rgba(255, 255, 255, 0.07)") },
        { QStringLiteral("win-close-hover"), QStringLiteral("rgba(240, 85, 61, 0.22)") },

        // 和浅色主题共用同一套固定深色（用户指定的值）：
        // 标签栏 #1F2430，选中标签/终端 #282C34，分隔线和底边 #0B0D12。
        { QStringLiteral("term-rail-bg"),        QStringLiteral("#1F2430") },
        { QStringLiteral("term-rail-line"),      QStringLiteral("#0B0D12") },
        { QStringLiteral("term-tab-hover-bg"),   QStringLiteral("rgba(255, 255, 255, 0.06)") },
        { QStringLiteral("term-tab-sep"),        QStringLiteral("#0B0D12") },
        { QStringLiteral("term-tab-active-bg"),  QStringLiteral("#282C34") },
        { QStringLiteral("term-tab-close-line"), QStringLiteral("#3A3F4B") },
        { QStringLiteral("term-tab-ok"),         QStringLiteral("#3FB950") },
        { QStringLiteral("term-tab-err"),        QStringLiteral("#F0553D") },

        { QStringLiteral("term-bg"),     QStringLiteral("rgba(0, 0, 0, 0)") },
        { QStringLiteral("term-fg"),     QStringLiteral("#D6DAE3") },
        { QStringLiteral("term-cursor"), QStringLiteral("#5B7CFA") },
        { QStringLiteral("term-sel"),    QStringLiteral("#2F4491") },
    };
    return table;
}

// ---------------------------------------------------------------------------
// 配色登记表
//
// **加一套新配色，只需要在下面加一行，别的文件一行都不用动。**
// QSS 的 @token 替换、设置页的下拉框、QSettings 的读写，全是照着这张表走的。
//
// 照着 lightTable() 复制一份改色值的时候，token 名必须一一对上 —— 少一个
// 会画成品红（见 tokenValue() 结尾），一眼就能看见，不用担心漏了没提示。
//
// key 用过之后就不要再改：它是存在用户注册表里的值，改名等于把人家选好的
// 配色丢掉。
//
// **第一项是默认配色**（用户没选过、或者存了个认不出来的 key 时用它）。
// 目前是深色 —— 样式先只做深色，浅色那套以后再补。
// 调整默认项就是调整这里的顺序，不要另写兜底常量。
// ---------------------------------------------------------------------------
struct PaletteEntry
{
    QString key;
    QString label;
    const QHash<QString, QString> &table;
};

const QVector<PaletteEntry> &paletteEntries()
{
    // 两套表的函数内静态在第一次调用时就建好了，这里存的引用一直有效
    // 第一项是默认配色（见上面的说明）
    static const QVector<PaletteEntry> entries = {
        { QStringLiteral("dark"),  QStringLiteral("深色（默认）"), darkTable()  },
        { QStringLiteral("light"), QStringLiteral("浅色"),         lightTable() },
    };
    return entries;
}

bool isKnownPalette(const QString &key)
{
    for (const PaletteEntry &entry : paletteEntries()) {
        if (key == entry.key)
            return true;
    }
    return false;
}

// 认不出来的 key 一律退回第一套，不要崩、也不要画成一堆品红
const QHash<QString, QString> &paletteFor(const QString &key)
{
    const auto &entries = paletteEntries();
    for (const PaletteEntry &entry : entries) {
        if (key == entry.key)
            return entry.table;
    }
    return entries.first().table;
}

// ---------------------------------------------------------------------------
// 配色缓存
//
// Theme::color() 是被**按字符格**调用的 —— 终端重绘一屏就是一两次上万次调用。
// 原来它每次都现读一遍 QSettings（一趟构造 + 一次查注册表，实测约 30µs），
// 一帧下来几百毫秒，表现就是打字卡得没法用。
//
// 所以这里把「当前配色 key」和「token -> QColor」都缓存住。配色只可能通过
// setPaletteKey() 变化，那里负责作废；EMSHELL_THEME 是启动时读一次的环境变量，
// 进程跑起来之后不会变，缓存它也是安全的。
// ---------------------------------------------------------------------------
struct PaletteCache
{
    // 实际上只有 UI 线程会来取色，加锁是防着以后有人在会话线程里调
    // Theme::color() —— 无竞争的锁大约 20ns，相对免掉的注册表读可以忽略。
    QMutex mutex;
    QString key;
    QHash<QString, QColor> colors;
};

PaletteCache &paletteCache()
{
    static PaletteCache cache;
    return cache;
}

// 现算一次当前配色 key。不加锁，调用方自己保证。
QString readPaletteKey()
{
    const QString overrideKey = qEnvironmentVariable("EMSHELL_THEME").trimmed().toLower();
    if (isKnownPalette(overrideKey))
        return overrideKey;

    QSettings settings;
    const QString saved = settings.value(QString::fromLatin1(kModeKey)).toString();
    return isKnownPalette(saved) ? saved : paletteEntries().first().key;
}

// 终端 16 色 ANSI 调色板。按深色终端底调过，明度整体提了一档，
// 在 #1B1F27 上读起来不费劲。
const QColor g_ansi[16] = {
    QColor(0x26, 0x2B, 0x36), // 0  黑
    QColor(0xE5, 0x5B, 0x5B), // 1  红
    QColor(0x5E, 0xC4, 0x6E), // 2  绿
    QColor(0xD8, 0xA6, 0x3A), // 3  黄
    QColor(0x5C, 0x8C, 0xF5), // 4  蓝
    QColor(0xB4, 0x7C, 0xE8), // 5  品红
    QColor(0x45, 0xB8, 0xC8), // 6  青
    QColor(0xC6, 0xCB, 0xD6), // 7  白
    QColor(0x6B, 0x74, 0x86), // 8  亮黑
    QColor(0xFF, 0x84, 0x7B), // 9  亮红
    QColor(0x8C, 0xDF, 0x8E), // 10 亮绿
    QColor(0xEC, 0xC5, 0x5E), // 11 亮黄
    QColor(0x87, 0xB0, 0xFF), // 12 亮蓝
    QColor(0xCE, 0xA2, 0xF7), // 13 亮品红
    QColor(0x6F, 0xD8, 0xE6), // 14 亮青
    QColor(0xF5, 0xF7, 0xFA), // 15 亮白
};

QString monoFamily()
{
    static const QStringList candidates = {
        QStringLiteral("Cascadia Mono"),
        QStringLiteral("Cascadia Code"),
        QStringLiteral("JetBrains Mono"),
        QStringLiteral("Consolas"),
        QStringLiteral("Sarasa Mono SC"),
        QStringLiteral("Courier New"),
    };
    const QStringList installed = QFontDatabase::families();
    for (const QString &name : candidates) {
        if (installed.contains(name, Qt::CaseInsensitive))
            return name;
    }
    return QStringLiteral("Courier New");
}

QString uiFamily()
{
    static const QStringList candidates = {
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("Segoe UI"),
    };
    const QStringList installed = QFontDatabase::families();
    for (const QString &name : candidates) {
        if (installed.contains(name, Qt::CaseInsensitive))
            return name;
    }
    return QString();
}

} // namespace

Theme &Theme::instance()
{
    static Theme inst;
    return inst;
}

QVector<Theme::Palette> Theme::palettes()
{
    QVector<Palette> result;
    for (const PaletteEntry &entry : paletteEntries())
        result.append({ entry.key, entry.label });
    return result;
}

// 当前配色的来源有两处：环境变量 EMSHELL_THEME=light|dark（值就是 key，
// 用来临时覆盖，调试和「不想进设置页只想看一眼」的时候很方便），以及 QSettings。
// 真正的读取逻辑在 readPaletteKey()，这里只负责缓存。
QString Theme::paletteKey()
{
    PaletteCache &cache = paletteCache();
    QMutexLocker locker(&cache.mutex);
    if (cache.key.isEmpty())
        cache.key = readPaletteKey();
    return cache.key;
}

void Theme::setPaletteKey(const QString &key)
{
    // 不认识的 key 不往注册表里写，免得留个读出来也没用的垃圾值
    if (!isKnownPalette(key))
        return;

    QSettings settings;
    settings.setValue(QString::fromLatin1(kModeKey), key);

    // 换了配色，缓存里的 key 和颜色一起作废
    PaletteCache &cache = paletteCache();
    QMutexLocker locker(&cache.mutex);
    cache.key = key;
    cache.colors.clear();
}

QString Theme::tokenValue(const QString &token, const QString &key)
{
    const auto &table = paletteFor(key);
    auto it = table.constFind(token);
    if (it == table.constEnd())
        return QStringLiteral("#FF00FF");   // token 写错就画成品红，一眼看得见
    return it.value();
}

// 取色走缓存。这个函数在终端重绘里是逐格调用的，别在这里加任何要查注册表、
// 读文件或者解析字符串的活儿（见上面 PaletteCache 那段注释）。
QColor Theme::color(const QString &token)
{
    PaletteCache &cache = paletteCache();
    QMutexLocker locker(&cache.mutex);

    if (cache.key.isEmpty())
        cache.key = readPaletteKey();

    auto it = cache.colors.constFind(token);
    if (it != cache.colors.constEnd())
        return it.value();

    const QColor color = parseTokenColor(tokenValue(token, cache.key));
    cache.colors.insert(token, color);
    return color;
}

QString Theme::buildStyleSheet(const QString &key)
{
    QFile f(QStringLiteral(":/theme/app.qss"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("EmShell: 样式表 :/theme/app.qss 没加载出来，界面会很难看");
        return QString();
    }
    QString qss = QString::fromUtf8(f.readAll());

    // 把 @token 替换成真实色值。按 token 名长度倒序处理，避免
    // @accent 把 @accent-hover 的前缀先吃掉。
    const auto &table = paletteFor(key);
    QStringList tokens = table.keys();
    std::sort(tokens.begin(), tokens.end(), [](const QString &a, const QString &b) {
        return a.size() > b.size();
    });
    for (const QString &token : tokens)
        qss.replace(QLatin1Char('@') + token, table.value(token));

    return qss;
}

QString Theme::styleSheet()
{
    return buildStyleSheet(paletteKey());
}

QFont Theme::uiFont(int pixelSize, QFont::Weight weight)
{
    QFont f;
    const QString family = uiFamily();
    if (!family.isEmpty())
        f.setFamily(family);
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    f.setHintingPreference(QFont::PreferFullHinting);
    return f;
}

QFont Theme::monoFont(int pixelSize, QFont::Weight weight)
{
    QFont f(monoFamily());
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setHintingPreference(QFont::PreferFullHinting);
    return f;
}

QFont Theme::terminalFont(int pixelSize)
{
    // 终端正文用 Consolas：它比 Cascadia Mono 的 Regular 细一档，观感更接近
    // 界面其他地方的文字。Cascadia 在这个字号下看着偏粗，而 Qt 在 Windows 上
    // 把它的 Light/ExtraLight 都匹配成同一个字面（改 weight 没有效果）。
    //
    // **不能用 uiFont**：TerminalView 是按等宽格子画的，比例字体放进去列会全歪。
    static const bool hasConsolas =
        QFontDatabase::families().contains(QStringLiteral("Consolas"), Qt::CaseInsensitive);
    if (!hasConsolas)
        return monoFont(pixelSize);

    QFont f(QStringLiteral("Consolas"));
    f.setPixelSize(pixelSize);
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    // 整像素 hinting 会把细笔画补粗；终端里关掉，字缘也更干净。
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

const QColor *Theme::ansiPalette()
{
    return g_ansi;
}

} // namespace Em
