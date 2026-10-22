#include "app/palettes/Palette.h"

namespace Em {

namespace {

// ---------------------------------------------------------------------------
// 深红
//
// 近黑的暖色底 + 猩红强调色，像红色的告警终端。层次仍然是三层台阶：
//     画布 #0C0507 → 侧栏 #160A0D → 卡片 #1E0E11（半透明）
//
// 和「墨绿」那套相反，这里的强调色压得够深（#D62828），所以
// accent-on 还是白色；荧光色那套才需要把文字翻成深色。
// 红色容易让人烦躁，正文用带一点粉的暖白（#F2DEDD），不要纯白。
// ---------------------------------------------------------------------------
const PaletteTable &redTable()
{
    static const PaletteTable table = {
        // 表面
        { QStringLiteral("bg-app"),       QStringLiteral("#0C0507") },
        { QStringLiteral("bg-panel"),     QStringLiteral("rgba(30, 14, 17, 0.84)") },
        { QStringLiteral("bg-elevated"),  QStringLiteral("#1C0D11") },
        { QStringLiteral("bg-input"),     QStringLiteral("rgba(22, 10, 13, 0.88)") },
        { QStringLiteral("bg-hover"),     QStringLiteral("rgba(255, 120, 120, 0.10)") },
        { QStringLiteral("bg-active"),    QStringLiteral("rgba(88, 38, 44, 0.72)") },

        // 侧栏：带红调的深色轨道
        { QStringLiteral("nav-bg"),           QStringLiteral("#160A0D") },
        { QStringLiteral("nav-bg-glass"),     QStringLiteral("rgba(22, 10, 13, 0.52)") },
        { QStringLiteral("nav-line"),         QStringLiteral("#31161A") },
        { QStringLiteral("nav-text"),         QStringLiteral("#B08A8F") },
        { QStringLiteral("nav-text-hover"),   QStringLiteral("#FFE8E6") },
        { QStringLiteral("nav-text-active"),  QStringLiteral("#FF5A5A") },
        { QStringLiteral("nav-hover-bg"),     QStringLiteral("#2A1216") },
        { QStringLiteral("nav-highlight-bg"), QStringLiteral("rgba(255, 255, 255, 0.05)") },
        { QStringLiteral("nav-active-bg"),    QStringLiteral("rgba(255, 90, 90, 0.13)") },
        { QStringLiteral("nav-group"),        QStringLiteral("#7F555A") },
        { QStringLiteral("nav-count-bg"),     QStringLiteral("rgba(58, 28, 33, 0.75)") },
        { QStringLiteral("nav-count-text"),   QStringLiteral("#B08A8F") },
        { QStringLiteral("nav-cta-bg"),       QStringLiteral("#D62828") },
        { QStringLiteral("nav-cta-bg-hover"), QStringLiteral("#E63946") },
        { QStringLiteral("nav-cta-text"),     QStringLiteral("#FFFFFF") },

        { QStringLiteral("border"),        QStringLiteral("#331A1E") },
        { QStringLiteral("border-strong"), QStringLiteral("#4A262B") },
        { QStringLiteral("titlebar-line"), QStringLiteral("#2A1114") },
        { QStringLiteral("titlebar-bg"),   QStringLiteral("rgba(12, 5, 7, 0.88)") },
        { QStringLiteral("titlebar-icon-hover"), QStringLiteral("#FFFFFF") },
        { QStringLiteral("edge-shadow"),   QStringLiteral("#3A000000") },

        { QStringLiteral("text"),      QStringLiteral("#F2DEDD") },
        { QStringLiteral("text-dim"),  QStringLiteral("#B08A8F") },
        { QStringLiteral("text-mute"), QStringLiteral("#A67F84") },

        // 强调色：深猩红，白字够读；accent-text 用亮一档的红做链接 / 菜单选中
        { QStringLiteral("accent"),       QStringLiteral("rgba(214, 40, 40, 0.96)") },
        { QStringLiteral("accent-hover"), QStringLiteral("rgba(230, 57, 70, 0.96)") },
        { QStringLiteral("accent-press"), QStringLiteral("rgba(183, 28, 28, 0.96)") },
        { QStringLiteral("accent-soft"),  QStringLiteral("rgba(214, 40, 40, 0.13)") },
        { QStringLiteral("accent-line"),  QStringLiteral("#7A2A2E") },
        { QStringLiteral("accent-text"),  QStringLiteral("#FF6B6B") },
        { QStringLiteral("accent-on"),    QStringLiteral("#FFFFFF") },

        // 状态色：红主题里绿色要更亮才跳得出来，红色反而要偏粉，
        // 否则和强调色糊成一片。
        { QStringLiteral("ok"),       QStringLiteral("#4ADE80") },
        { QStringLiteral("warn"),     QStringLiteral("#E0A83A") },
        { QStringLiteral("err"),      QStringLiteral("#FF6B6B") },
        { QStringLiteral("ok-soft"),  QStringLiteral("rgba(74, 222, 128, 0.14)") },
        { QStringLiteral("err-soft"), QStringLiteral("rgba(255, 107, 107, 0.16)") },

        { QStringLiteral("dialog-mask"), QStringLiteral("rgba(0, 0, 0, 0.62)") },
        { QStringLiteral("dialog-pane"), QStringLiteral("rgba(30, 14, 17, 0.92)") },
        { QStringLiteral("dialog-rail-glass"), QStringLiteral("rgba(22, 10, 13, 0.88)") },

        { QStringLiteral("brand-bg"),        QStringLiteral("#140A0C") },
        { QStringLiteral("brand-line"),      QStringLiteral("#2E1418") },
        { QStringLiteral("brand-text"),      QStringLiteral("#FFE8E6") },
        { QStringLiteral("brand-sub"),       QStringLiteral("#9A7479") },
        { QStringLiteral("win-hover"),       QStringLiteral("rgba(255, 255, 255, 0.07)") },
        { QStringLiteral("win-close-hover"), QStringLiteral("rgba(255, 107, 107, 0.22)") },

        // 终端标签栏：深色，跟着红主题染一点
        { QStringLiteral("term-rail-bg"),        QStringLiteral("#13080A") },
        { QStringLiteral("term-rail-line"),      QStringLiteral("#040102") },
        { QStringLiteral("term-tab-hover-bg"),   QStringLiteral("rgba(255, 90, 90, 0.07)") },
        { QStringLiteral("term-tab-sep"),        QStringLiteral("#040102") },
        { QStringLiteral("term-tab-active-bg"),  QStringLiteral("#1E0C10") },
        { QStringLiteral("term-tab-close-line"), QStringLiteral("#4A262B") },
        { QStringLiteral("term-tab-text"),        QStringLiteral("#9A7479") },
        { QStringLiteral("term-tab-text-hover"),  QStringLiteral("#FFE0DD") },
        { QStringLiteral("term-tab-text-active"), QStringLiteral("#FFF0EE") },
        { QStringLiteral("term-tab-ok"),         QStringLiteral("#4ADE80") },
        { QStringLiteral("term-tab-err"),        QStringLiteral("#FF6B6B") },
        { QStringLiteral("term-tab-wait"),       QStringLiteral("#8F7478") },

        // 终端：暖白字 + 红光标
        { QStringLiteral("term-bg"),        QStringLiteral("rgba(0, 0, 0, 0)") },
        { QStringLiteral("term-fg"),        QStringLiteral("#FFD9D6") },
        { QStringLiteral("term-cursor"),    QStringLiteral("#FF3B3B") },
        { QStringLiteral("term-cursor-text"), QStringLiteral("#1A0505") },
        { QStringLiteral("term-sel"),       QStringLiteral("#7A1E22") },
    };
    return table;
}

// 红底调的 16 色。白 / 亮白偏暖（#FFD9D6 / #FFF0EE），
// 和正文一致；红 / 亮红比主题的强调红亮一档，免得在终端里看不出层次。
const QColor kRedAnsi[kAnsiColorCount] = {
    QColor(0x1A, 0x0B, 0x0D), // 0  黑
    QColor(0xFF, 0x4D, 0x4D), // 1  红
    QColor(0x5F, 0xD3, 0x7A), // 2  绿
    QColor(0xE0, 0xB8, 0x4A), // 3  黄
    QColor(0x6F, 0xA8, 0xFF), // 4  蓝
    QColor(0xD9, 0x7B, 0xC0), // 5  品红
    QColor(0x5F, 0xD3, 0xC8), // 6  青
    QColor(0xFF, 0xD9, 0xD6), // 7  白
    QColor(0x8A, 0x5A, 0x5F), // 8  亮黑
    QColor(0xFF, 0x80, 0x80), // 9  亮红
    QColor(0x8F, 0xE8, 0xA0), // 10 亮绿
    QColor(0xF0, 0xD4, 0x7A), // 11 亮黄
    QColor(0x9F, 0xC4, 0xFF), // 12 亮蓝
    QColor(0xEF, 0xA8, 0xD8), // 13 亮品红
    QColor(0x8F, 0xE8, 0xDE), // 14 亮青
    QColor(0xFF, 0xF0, 0xEE), // 15 亮白
};

static_assert(sizeof(kRedAnsi) / sizeof(kRedAnsi[0]) == kAnsiColorCount,
              "ANSI 调色板必须是 16 色");

const PaletteRegistrar kRedRegistrar({
    QStringLiteral("red"),
    QStringLiteral("深红"),
    20,
    false,
    &redTable(),
    kRedAnsi,
});

} // namespace

} // namespace Em
