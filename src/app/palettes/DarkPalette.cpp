#include "app/palettes/Palette.h"

namespace Em {

namespace {

// ---------------------------------------------------------------------------
// 深色主题
// ---------------------------------------------------------------------------
const PaletteTable &darkTable()
{
    static const PaletteTable table = {
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
        { QStringLiteral("edge-shadow"),   QStringLiteral("#36000000") },

        { QStringLiteral("text"),      QStringLiteral("#E8EAF0") },
        { QStringLiteral("text-dim"),  QStringLiteral("#9BA3B4") },
        { QStringLiteral("text-mute"), QStringLiteral("#bfbfbf") },

        { QStringLiteral("accent"),       QStringLiteral("rgba(91, 124, 250, 0.94)") },
        { QStringLiteral("accent-hover"), QStringLiteral("rgba(114, 144, 255, 0.94)") },
        { QStringLiteral("accent-press"), QStringLiteral("rgba(74, 104, 216, 0.94)") },
        { QStringLiteral("accent-soft"),  QStringLiteral("rgba(91, 124, 250, 0.14)") },
        { QStringLiteral("accent-line"),  QStringLiteral("#3A4770") },
        { QStringLiteral("accent-text"),  QStringLiteral("#9DB4FF") },
        // 压在实心 accent 上的文字 / 图标（主按钮、选中文本、复选框对勾）。
        // 荧光色配色（墨绿）要把这一项改成深色，不能写死白色。
        { QStringLiteral("accent-on"),    QStringLiteral("#FFFFFF") },

        { QStringLiteral("ok"),       QStringLiteral("#3FB950") },
        { QStringLiteral("warn"),     QStringLiteral("#D6A32B") },
        { QStringLiteral("err"),      QStringLiteral("#F0553D") },
        { QStringLiteral("ok-soft"),  QStringLiteral("rgba(63, 185, 80, 0.14)") },
        { QStringLiteral("err-soft"), QStringLiteral("rgba(240, 85, 61, 0.16)") },

        // 新建连接弹窗的外层遮罩
        { QStringLiteral("dialog-mask"), QStringLiteral("rgba(0, 0, 0, 0.55)") },
        // 弹窗右栏比左栏的 nav-bg（#1D212B）亮一档、再偏冷一点。
        // 两栏亮度差得太少时中间那条边界会糊掉，看着像一整块深灰。
        { QStringLiteral("dialog-pane"), QStringLiteral("rgba(42, 49, 64, 0.90)") },
        { QStringLiteral("dialog-rail-glass"), QStringLiteral("rgba(29, 33, 43, 0.86)") },

        // 页头：深色下的对应版本
        { QStringLiteral("brand-bg"),        QStringLiteral("#1E1C24") },
        { QStringLiteral("brand-line"),      QStringLiteral("#2E2B36") },
        { QStringLiteral("brand-text"),      QStringLiteral("#F0EDF2") },
        { QStringLiteral("brand-sub"),       QStringLiteral("#8A8494") },
        { QStringLiteral("win-hover"),       QStringLiteral("rgba(255, 255, 255, 0.07)") },
        { QStringLiteral("win-close-hover"), QStringLiteral("rgba(240, 85, 61, 0.22)") },

        // 终端标签栏在每套配色里都是**深色**。深色 / 浅色两套共用这一组值：
        // 标签栏 #1F2430，选中标签 #282C34，分隔线和底边 #0B0D12
        // （深红 / 墨绿在自己的文件里给了染色版本）。
        // 标签上的文字 / 图标**用 term-tab-text 这一组，不要用 nav-text** ——
        // 浅色主题的侧栏是浅底，nav-text 会跟着变成深灰，放到这条深色栏上
        // 就看不见了。
        { QStringLiteral("term-rail-bg"),        QStringLiteral("#1F2430") },
        { QStringLiteral("term-rail-line"),      QStringLiteral("#0B0D12") },
        { QStringLiteral("term-tab-hover-bg"),   QStringLiteral("rgba(255, 255, 255, 0.06)") },
        { QStringLiteral("term-tab-sep"),        QStringLiteral("#0B0D12") },
        { QStringLiteral("term-tab-active-bg"),  QStringLiteral("#282C34") },
        { QStringLiteral("term-tab-close-line"), QStringLiteral("#3A3F4B") },
        { QStringLiteral("term-tab-text"),        QStringLiteral("#9BA3B4") },
        { QStringLiteral("term-tab-text-hover"),  QStringLiteral("#E8EAF0") },
        { QStringLiteral("term-tab-text-active"), QStringLiteral("#FFFFFF") },
        // 标签名前面的状态圆点：
        //   灰 = SSH 正在连接，绿 = SSH 已连接，红 = 已断开
        //   本地终端用 nav-cta-bg（蓝色），不单独占一个 token
        { QStringLiteral("term-tab-ok"),         QStringLiteral("#3FB950") },
        { QStringLiteral("term-tab-err"),        QStringLiteral("#F0553D") },
        { QStringLiteral("term-tab-wait"),       QStringLiteral("#8A93A5") },

        // 终端背景保持**全透明**：文字直接画在页面背景（背景图 / 画布）上。
        { QStringLiteral("term-bg"),        QStringLiteral("rgba(0, 0, 0, 0)") },
        { QStringLiteral("term-fg"),        QStringLiteral("#D6DAE3") },
        { QStringLiteral("term-cursor"),    QStringLiteral("#5B7CFA") },
        // 光标是实色块：底下那个字要用这个颜色「抠」出来
        { QStringLiteral("term-cursor-text"), QStringLiteral("#FFFFFF") },
        { QStringLiteral("term-sel"),       QStringLiteral("#2F4491") },
    };
    return table;
}

// 深色终端底上调过的一套 16 色。明度整体提了一档，在 #12151C / #1F2430 上
// 读起来不费劲。浅色主题要用另一套（见 LightPalette.cpp）。
const QColor kDarkAnsi[kAnsiColorCount] = {
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

static_assert(sizeof(kDarkAnsi) / sizeof(kDarkAnsi[0]) == kAnsiColorCount,
              "ANSI 调色板必须是 16 色");

const PaletteRegistrar kDarkRegistrar({
    QStringLiteral("dark"),
    QStringLiteral("深色"),
    10,
    false,
    &darkTable(),
    kDarkAnsi,
});

} // namespace

} // namespace Em
