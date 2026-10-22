#include "app/palettes/Palette.h"

namespace Em {

namespace {

// ---------------------------------------------------------------------------
// 墨绿
//
// 整屏是近黑的墨绿，强调色是荧光绿。和深色主题一样是「侧栏比画布亮一档」
// 的三层台阶，只是整条色相都压到绿色上：
//     画布 #050B07 → 侧栏 #08140D → 卡片 #0B180F（半透明）
//
// 这个配色的关键是**实心荧光绿上的文字用深色**（accent-on / nav-cta-text），
// 白字压在 #00D65A 上几乎看不清。QSS 里不要写死 #FFFFFF。
// 终端也走绿字黑底，ANSI 的红黄蓝仍然保留，否则输出会分不清。
// ---------------------------------------------------------------------------
const PaletteTable &greenTable()
{
    static const PaletteTable table = {
        // 表面
        { QStringLiteral("bg-app"),       QStringLiteral("#050B07") },
        { QStringLiteral("bg-panel"),     QStringLiteral("rgba(11, 24, 16, 0.86)") },
        { QStringLiteral("bg-elevated"),  QStringLiteral("#0C1A11") },
        { QStringLiteral("bg-input"),     QStringLiteral("rgba(7, 17, 11, 0.90)") },
        { QStringLiteral("bg-hover"),     QStringLiteral("rgba(0, 255, 102, 0.07)") },
        { QStringLiteral("bg-active"),    QStringLiteral("rgba(0, 255, 102, 0.12)") },

        // 侧栏：比画布亮一点点的墨绿轨道
        { QStringLiteral("nav-bg"),           QStringLiteral("#08140D") },
        { QStringLiteral("nav-bg-glass"),     QStringLiteral("rgba(8, 20, 13, 0.55)") },
        { QStringLiteral("nav-line"),         QStringLiteral("#143020") },
        { QStringLiteral("nav-text"),         QStringLiteral("#7FA98A") },
        { QStringLiteral("nav-text-hover"),   QStringLiteral("#D9FFE3") },
        { QStringLiteral("nav-text-active"),  QStringLiteral("#39FF77") },
        { QStringLiteral("nav-hover-bg"),     QStringLiteral("#10251A") },
        { QStringLiteral("nav-highlight-bg"), QStringLiteral("rgba(0, 255, 102, 0.06)") },
        { QStringLiteral("nav-active-bg"),    QStringLiteral("rgba(0, 255, 102, 0.12)") },
        { QStringLiteral("nav-group"),        QStringLiteral("#5F8A6A") },
        { QStringLiteral("nav-count-bg"),     QStringLiteral("rgba(18, 40, 28, 0.80)") },
        { QStringLiteral("nav-count-text"),   QStringLiteral("#8FBF9E") },
        // 荧光绿实心按钮 + 近黑文字：终端绿字黑底的延伸
        { QStringLiteral("nav-cta-bg"),       QStringLiteral("#21D96A") },
        { QStringLiteral("nav-cta-bg-hover"), QStringLiteral("#3BF07D") },
        { QStringLiteral("nav-cta-text"),     QStringLiteral("#04150B") },

        { QStringLiteral("border"),        QStringLiteral("#143020") },
        { QStringLiteral("border-strong"), QStringLiteral("#1F4A31") },
        { QStringLiteral("titlebar-line"), QStringLiteral("#10301F") },
        { QStringLiteral("titlebar-bg"),   QStringLiteral("rgba(5, 11, 7, 0.90)") },
        { QStringLiteral("titlebar-icon-hover"), QStringLiteral("#D9FFE3") },
        { QStringLiteral("edge-shadow"),   QStringLiteral("#3A000000") },

        { QStringLiteral("text"),      QStringLiteral("#D7FFE2") },
        { QStringLiteral("text-dim"),  QStringLiteral("#93C2A2") },
        { QStringLiteral("text-mute"), QStringLiteral("#6F9A7E") },

        // 强调色：荧光绿。accent-on 必须是深色，白字在这上面读不出来。
        { QStringLiteral("accent"),       QStringLiteral("rgba(0, 214, 90, 0.96)") },
        { QStringLiteral("accent-hover"), QStringLiteral("rgba(0, 232, 100, 0.96)") },
        { QStringLiteral("accent-press"), QStringLiteral("rgba(0, 184, 76, 0.96)") },
        { QStringLiteral("accent-soft"),  QStringLiteral("rgba(0, 214, 90, 0.13)") },
        { QStringLiteral("accent-line"),  QStringLiteral("#1F7A45") },
        { QStringLiteral("accent-text"),  QStringLiteral("#4DFF88") },
        { QStringLiteral("accent-on"),    QStringLiteral("#03140A") },

        // 状态色：绿和强调色同色系，靠明度区分；红 / 黄保持能认出来
        { QStringLiteral("ok"),       QStringLiteral("#4ADE80") },
        { QStringLiteral("warn"),     QStringLiteral("#D8A73A") },
        { QStringLiteral("err"),      QStringLiteral("#FF5C5C") },
        { QStringLiteral("ok-soft"),  QStringLiteral("rgba(74, 222, 128, 0.14)") },
        { QStringLiteral("err-soft"), QStringLiteral("rgba(255, 92, 92, 0.16)") },

        { QStringLiteral("dialog-mask"), QStringLiteral("rgba(0, 0, 0, 0.62)") },
        { QStringLiteral("dialog-pane"), QStringLiteral("rgba(12, 27, 18, 0.92)") },
        { QStringLiteral("dialog-rail-glass"), QStringLiteral("rgba(8, 20, 13, 0.88)") },

        { QStringLiteral("brand-bg"),        QStringLiteral("#071009") },
        { QStringLiteral("brand-line"),      QStringLiteral("#10301F") },
        { QStringLiteral("brand-text"),      QStringLiteral("#DFFFE7") },
        { QStringLiteral("brand-sub"),       QStringLiteral("#7FA98A") },
        { QStringLiteral("win-hover"),       QStringLiteral("rgba(0, 255, 102, 0.08)") },
        { QStringLiteral("win-close-hover"), QStringLiteral("rgba(255, 92, 92, 0.20)") },

        // 终端标签栏：保持深色，但跟着绿色主题染一点
        { QStringLiteral("term-rail-bg"),        QStringLiteral("#06110B") },
        { QStringLiteral("term-rail-line"),      QStringLiteral("#010604") },
        { QStringLiteral("term-tab-hover-bg"),   QStringLiteral("rgba(0, 255, 102, 0.07)") },
        { QStringLiteral("term-tab-sep"),        QStringLiteral("#010604") },
        { QStringLiteral("term-tab-active-bg"),  QStringLiteral("#0B1D13") },
        { QStringLiteral("term-tab-close-line"), QStringLiteral("#1F4A31") },
        { QStringLiteral("term-tab-text"),        QStringLiteral("#6F9A7E") },
        { QStringLiteral("term-tab-text-hover"),  QStringLiteral("#D9FFE3") },
        { QStringLiteral("term-tab-text-active"), QStringLiteral("#E8FFF0") },
        { QStringLiteral("term-tab-ok"),         QStringLiteral("#4ADE80") },
        { QStringLiteral("term-tab-err"),        QStringLiteral("#FF5C5C") },
        { QStringLiteral("term-tab-wait"),       QStringLiteral("#6F8F7A") },

        // 终端：黑底绿字。背景仍然全透明，画在画布上。
        { QStringLiteral("term-bg"),        QStringLiteral("rgba(0, 0, 0, 0)") },
        { QStringLiteral("term-fg"),        QStringLiteral("#B9FFCB") },
        { QStringLiteral("term-cursor"),    QStringLiteral("#00FF66") },
        { QStringLiteral("term-cursor-text"), QStringLiteral("#011A0B") },
        { QStringLiteral("term-sel"),       QStringLiteral("#0E6B38") },
    };
    return table;
}

// 黑底绿字终端。ANSI 的红 / 黄 / 蓝 / 品红都保留正常色相，
// 只有 7 / 15（白）跟着整体偏绿，免得输出里出现一块刺眼的纯白。
const QColor kGreenAnsi[kAnsiColorCount] = {
    QColor(0x0A, 0x1A, 0x10), // 0  黑
    QColor(0xFF, 0x5C, 0x5C), // 1  红
    QColor(0x39, 0xE7, 0x5F), // 2  绿
    QColor(0xC6, 0xD9, 0x4A), // 3  黄
    QColor(0x58, 0xA6, 0xFF), // 4  蓝
    QColor(0xBC, 0x7D, 0xFF), // 5  品红
    QColor(0x48, 0xD1, 0xCC), // 6  青
    QColor(0xB9, 0xFF, 0xC8), // 7  白
    QColor(0x4E, 0x7A, 0x5C), // 8  亮黑
    QColor(0xFF, 0x8A, 0x8A), // 9  亮红
    QColor(0x7C, 0xFF, 0x9A), // 10 亮绿
    QColor(0xE6, 0xF0, 0x7A), // 11 亮黄
    QColor(0x8F, 0xC7, 0xFF), // 12 亮蓝
    QColor(0xDD, 0xB0, 0xFF), // 13 亮品红
    QColor(0x8F, 0xF0, 0xEA), // 14 亮青
    QColor(0xE8, 0xFF, 0xF0), // 15 亮白
};

static_assert(sizeof(kGreenAnsi) / sizeof(kGreenAnsi[0]) == kAnsiColorCount,
              "ANSI 调色板必须是 16 色");

const PaletteRegistrar kGreenRegistrar({
    QStringLiteral("green"),
    QStringLiteral("墨绿"),
    30,
    false,
    &greenTable(),
    kGreenAnsi,
});

} // namespace

} // namespace Em
