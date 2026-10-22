#include "app/palettes/Palette.h"

namespace Em {

namespace {

// ---------------------------------------------------------------------------
// 浅色主题
//
// 整窗都浅：侧栏、画布、卡片三块台阶，靠色相和明度差拿层次，不用描边堆。
//     侧栏  #E9EEFA   淡紫蓝的有色轨道，最"有色"，也是最暗的一块
//     画布  #F7F8FC   近白，中间层
//     卡片  #FFFFFF   纯白，浮在最上面
//
// 侧栏上的交互态和卡片上**分开**：
//     卡片在白底上，hover 往深里走（bg-hover，淡灰蓝）
//     侧栏在有色轨道上，hover 要**更白**（nav-highlight-bg）才像浮起来，
//     选中是强调蓝压出来的淡蓝雾（nav-active-bg）
// 两者混用的话，选中和悬浮在浅色轨道上会读反。
//
// 文字三级灰阶都比深色主题再深一档 —— 白底上的灰字很容易糊，尤其是
// text-mute 那一档，浅一分就掉到 4:1 以下。
// ---------------------------------------------------------------------------
const PaletteTable &lightTable()
{
    static const PaletteTable table = {
        // 表面
        { QStringLiteral("bg-app"),       QStringLiteral("#F7F8FC") },
        // **凡是盖在背景图上的表面都用 rgba**（面板、输入框、标题栏…）：
        // 用户明确要求「不允许有元素彻底盖住背景图」。透明度都不高，
        // 没有背景图时看起来和实色几乎一样。
        { QStringLiteral("bg-panel"),     QStringLiteral("rgba(255, 255, 255, 0.92)") },
        // 菜单 / 气泡 / 弹窗是独立的顶层窗口，背后不是背景图而是桌面 —— 保持实色
        { QStringLiteral("bg-elevated"),  QStringLiteral("#FFFFFF") },
        { QStringLiteral("bg-input"),     QStringLiteral("rgba(255, 255, 255, 0.94)") },
        { QStringLiteral("bg-hover"),     QStringLiteral("rgba(21, 27, 43, 0.06)") },
        { QStringLiteral("bg-active"),    QStringLiteral("rgba(21, 27, 43, 0.09)") },

        // ---- 侧栏：浅色轨道 ----
        // 它整条链路用 nav-* 这套 token。全局的 text-dim / text-mute 是给
        // 白卡片准备的，放在有色的轨道上会偏灰、发闷。
        { QStringLiteral("nav-bg"),           QStringLiteral("#E9EEFA") },
        // 设了背景图时侧栏用它：半透明的轨道，底下的图能透上来，
        // 但压得够实，导航文字仍然清楚。
        { QStringLiteral("nav-bg-glass"),     QStringLiteral("rgba(233, 238, 250, 0.78)") },
        { QStringLiteral("nav-line"),         QStringLiteral("#DCE4F2") },
        { QStringLiteral("nav-text"),         QStringLiteral("#4C566E") },
        { QStringLiteral("nav-text-hover"),   QStringLiteral("#131B2C") },
        { QStringLiteral("nav-text-active"),  QStringLiteral("#2F4FD8") },
        // 滚动条和数字角标用的实色。侧栏底色本身有色，这里的「更深」才是 hover。
        { QStringLiteral("nav-hover-bg"),     QStringLiteral("#D7E0F0") },
        // 悬浮 / 按住的底色：浅轨道上要**更白**才像浮起来。
        // 选中态另用 nav-active-bg（强调蓝的淡雾），两条不要合成一条。
        { QStringLiteral("nav-highlight-bg"), QStringLiteral("rgba(255, 255, 255, 0.66)") },
        { QStringLiteral("nav-active-bg"),    QStringLiteral("rgba(47, 79, 216, 0.13)") },
        { QStringLiteral("nav-group"),        QStringLiteral("#626C86") },
        { QStringLiteral("nav-count-bg"),     QStringLiteral("rgba(21, 27, 43, 0.07)") },
        { QStringLiteral("nav-count-text"),   QStringLiteral("#525D78") },
        // 侧栏顶上的「+ 新建连接」。整条侧栏唯一的实心强调色，一眼能找到。
        // 用比 accent 稍亮一点的蓝：它和卡片上的主按钮同色系，但压在浅蓝轨道上
        // 仍然要跳出来。白字对 #3563E9 的对比度约 5.3:1。
        { QStringLiteral("nav-cta-bg"),       QStringLiteral("#3563E9") },
        { QStringLiteral("nav-cta-bg-hover"), QStringLiteral("#2B54D2") },
        { QStringLiteral("nav-cta-text"),     QStringLiteral("#FFFFFF") },

        // 描边：底色偏冷，描边也跟着偏冷，别用中性灰
        { QStringLiteral("border"),        QStringLiteral("#E3E8F2") },
        { QStringLiteral("border-strong"), QStringLiteral("#C9D2E4") },
        { QStringLiteral("titlebar-line"), QStringLiteral("#E3E8F2") },
        { QStringLiteral("titlebar-bg"),   QStringLiteral("rgba(247, 248, 252, 0.92)") },
        { QStringLiteral("titlebar-icon-hover"), QStringLiteral("#131B2C") },
        { QStringLiteral("edge-shadow"),   QStringLiteral("#0D000000") },

        // 文字：三级灰阶。白底上宁可深一点，也别让人眯眼
        { QStringLiteral("text"),      QStringLiteral("#161D2C") },
        { QStringLiteral("text-dim"),  QStringLiteral("#4A556E") },
        { QStringLiteral("text-mute"), QStringLiteral("#6F7994") },

        // 强调色：靛蓝。深色主题那套亮蓝压到白底上会发飘，
        // 这里整体压深一档：主按钮白字的对比度约 6:1，作为正文链接也够。
        { QStringLiteral("accent"),       QStringLiteral("rgba(47, 79, 216, 0.96)") },
        { QStringLiteral("accent-hover"), QStringLiteral("rgba(40, 70, 194, 0.96)") },
        { QStringLiteral("accent-press"), QStringLiteral("rgba(33, 59, 168, 0.96)") },
        { QStringLiteral("accent-soft"),  QStringLiteral("rgba(47, 79, 216, 0.10)") },
        { QStringLiteral("accent-line"),  QStringLiteral("#C2CDF3") },
        { QStringLiteral("accent-text"),  QStringLiteral("#2A46C9") },

        // 状态色：都在白底上量过，绿 / 黄 / 红都比深色主题暗一档
        { QStringLiteral("ok"),       QStringLiteral("#0B7A45") },
        { QStringLiteral("warn"),     QStringLiteral("#9A5B00") },
        { QStringLiteral("err"),      QStringLiteral("#C62828") },
        { QStringLiteral("ok-soft"),  QStringLiteral("rgba(11, 122, 69, 0.12)") },
        { QStringLiteral("err-soft"), QStringLiteral("rgba(198, 40, 40, 0.10)") },

        // 新建连接弹窗的外层遮罩。浅色主题下不需要压得太黑，
        // 否则弹窗周围会像被挖了个洞。
        { QStringLiteral("dialog-mask"), QStringLiteral("rgba(15, 23, 42, 0.35)") },
        // 弹窗右栏：比卡片再淡一档的冷白。左栏现在是浅色轨道，
        // 两栏靠一点点冷暖和明度差分开，不靠强对比。
        { QStringLiteral("dialog-pane"), QStringLiteral("rgba(252, 253, 255, 0.94)") },
        // 弹窗左栏的玻璃层：比主窗口侧栏的 nav-bg-glass 更实一点，
        // 弹窗跨在亮背景图上，太透文字会发虚。
        { QStringLiteral("dialog-rail-glass"), QStringLiteral("rgba(233, 238, 250, 0.88)") },

        // 页头
        { QStringLiteral("brand-bg"),        QStringLiteral("#FFFFFF") },
        { QStringLiteral("brand-line"),      QStringLiteral("#E3E8F2") },
        { QStringLiteral("brand-text"),      QStringLiteral("#161D2C") },
        { QStringLiteral("brand-sub"),       QStringLiteral("#5A6478") },
        { QStringLiteral("win-hover"),       QStringLiteral("rgba(21, 27, 43, 0.06)") },
        { QStringLiteral("win-close-hover"), QStringLiteral("rgba(198, 40, 40, 0.12)") },

        // 终端标签栏是**固定深色**（两套配色共用这一组值）。
        // 标签文字 / 图标用 term-tab-text 这一组：浅色主题下 nav-text 是深灰，
        // 放到这条 #1F2430 上根本看不见。
        { QStringLiteral("term-rail-bg"),        QStringLiteral("#1F2430") },
        { QStringLiteral("term-rail-line"),      QStringLiteral("#0B0D12") },
        { QStringLiteral("term-tab-hover-bg"),   QStringLiteral("rgba(255, 255, 255, 0.06)") },
        { QStringLiteral("term-tab-sep"),        QStringLiteral("#0B0D12") },
        { QStringLiteral("term-tab-active-bg"),  QStringLiteral("#282C34") },
        { QStringLiteral("term-tab-close-line"), QStringLiteral("#3A3F4B") },
        { QStringLiteral("term-tab-text"),        QStringLiteral("#9BA3B4") },
        { QStringLiteral("term-tab-text-hover"),  QStringLiteral("#E8EAF0") },
        { QStringLiteral("term-tab-text-active"), QStringLiteral("#FFFFFF") },
        { QStringLiteral("term-tab-ok"),         QStringLiteral("#3FB950") },
        { QStringLiteral("term-tab-err"),        QStringLiteral("#F0553D") },
        { QStringLiteral("term-tab-wait"),       QStringLiteral("#8A93A5") },

        // 终端背景保持**全透明**：文字直接画在浅色画布 / 背景图上。
        // 前景色必须是深色 —— 深色主题那套浅灰字放到这里等于白底白字。
        { QStringLiteral("term-bg"),        QStringLiteral("rgba(0, 0, 0, 0)") },
        { QStringLiteral("term-fg"),        QStringLiteral("#232A3A") },
        { QStringLiteral("term-cursor"),    QStringLiteral("#2F4FD8") },
        // 光标是实色块：底下那个字要用这个颜色「抠」出来。
        // 浅色主题下光标是深蓝，字必须是白的。
        { QStringLiteral("term-cursor-text"), QStringLiteral("#FFFFFF") },
        // 选区：浅蓝底 + 深色字。alpha 在 TerminalView 里固定成 150。
        { QStringLiteral("term-sel"),       QStringLiteral("#9DBBFF") },
    };
    return table;
}

// 浅色底上的 16 色 ANSI。
//
// 除了 7 / 15 这两个「白」，其余颜色都比深色那套压深一档，压在 #F7F8FC 上能读。
//
// 7 / 15 仍然保持浅色（和所有浅色终端主题一样）：它们不只当前景用，
// 还会当**背景色**用（SGR 47 / 107）。要是为了「白字在白底上看得见」
// 把它们改成深灰，白底高亮就会变成黑底黑字，反而更常见地坏掉。
// 用 SGR 37 / 97 在浅色底上打白字本来就不该期待可读 —— 要用深色主题。
const QColor kLightAnsi[kAnsiColorCount] = {
    QColor(0x3B, 0x40, 0x48), // 0  黑
    QColor(0xD3, 0x2F, 0x2F), // 1  红
    QColor(0x2E, 0x7D, 0x32), // 2  绿
    QColor(0xA0, 0x5A, 0x00), // 3  黄
    QColor(0x1F, 0x6F, 0xEB), // 4  蓝
    QColor(0x8E, 0x44, 0xAD), // 5  品红
    QColor(0x0E, 0x74, 0x90), // 6  青
    QColor(0xBF, 0xC5, 0xCE), // 7  白（浅色，主要当背景色用）
    QColor(0x6B, 0x72, 0x80), // 8  亮黑
    QColor(0xE0, 0x52, 0x52), // 9  亮红
    QColor(0x43, 0xA0, 0x47), // 10 亮绿
    QColor(0xB7, 0x79, 0x1F), // 11 亮黄
    QColor(0x3B, 0x82, 0xF6), // 12 亮蓝
    QColor(0xA8, 0x55, 0xC8), // 13 亮品红
    QColor(0x14, 0x96, 0xB0), // 14 亮青
    QColor(0xF2, 0xF4, 0xF8), // 15 亮白（同上）
};

static_assert(sizeof(kLightAnsi) / sizeof(kLightAnsi[0]) == kAnsiColorCount,
              "ANSI 调色板必须是 16 色");

const PaletteRegistrar kLightRegistrar({
    QStringLiteral("light"),
    QStringLiteral("浅色"),
    10,
    false,
    &lightTable(),
    kLightAnsi,
});

} // namespace

} // namespace Em
