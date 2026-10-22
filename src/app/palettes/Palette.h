#pragma once

#include <QColor>
#include <QHash>
#include <QString>
#include <QVector>

namespace Em {

// 一套配色的全部色值：token -> 色值字符串（#RRGGBB / #AARRGGBB / rgba(...)）。
//
// **加一套新配色只需要新建一个 .cpp 文件**（照 DarkPalette.cpp / LightPalette.cpp
// 抄一份改色值），文件末尾用一个 PaletteRegistrar 把自己登记进来，再把文件加进
// CMakeLists.txt 的源文件列表。设置页的下拉框、QSettings 读写、QSS 的 @token
// 替换全都照着登记表走，别的文件一行都不用动。
using PaletteTable = QHash<QString, QString>;

// 终端 16 色 ANSI 调色板。浅色和深色必须各有一套 ——
// 深色那套的「白」是接近 #FFFFFF 的亮灰，放到浅色底上等于看不见。
constexpr int kAnsiColorCount = 16;

struct PaletteSpec
{
    QString key;                   // 存进 QSettings 的值，用过之后不要改名
    QString label;                 // 设置页里显示的名字
    int order = 0;                 // 下拉框排序，越小越靠前
    bool isDefault = false;        // 用户没选过 / key 认不出来时用它（只能有一套）
    const PaletteTable *table = nullptr;
    const QColor *ansi = nullptr;  // 长度必须是 kAnsiColorCount
};

// 在配色文件的匿名命名空间里建一个静态实例，进程启动时自动登记。
class PaletteRegistrar
{
public:
    explicit PaletteRegistrar(const PaletteSpec &spec);
};

void registerPalette(const PaletteSpec &spec);

// 按 order 排好序的全部配色（第一次调用时排序，之后返回同一份）。
// 第一项不一定是默认配色 —— 默认看 isDefault。
const QVector<PaletteSpec> &paletteSpecs();

// 默认配色。没有任何一套标了 isDefault 时退回排在最前面的那套。
const PaletteSpec &defaultPalette();

// 找不到返回 nullptr。
const PaletteSpec *findPalette(const QString &key);

} // namespace Em
