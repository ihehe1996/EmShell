#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>
#include <QVector>

namespace Em {

// 全局配色与字体。
//
// 整个程序里所有颜色都必须从这里取，不要在别处写死色值。
// QSS 文件（resources/theme/app.qss）里写的是 @token 占位符，
// 由 Theme::styleSheet() 在加载时替换成当前配色表里的真实色值 ——
// 所以加一套配色**不用碰样式表，也不用碰任何一个控件**。
//
// 加新配色只有一步：在 Theme.cpp 的 paletteEntries() 里加一行。
// 色值表本身留在 Theme.cpp，不往外暴露，外面只需要知道有哪些配色可选。
class Theme : public QObject
{
    Q_OBJECT

public:
    // 一套配色在设置页里需要的信息。key 存进 QSettings，label 显示在下拉框里。
    struct Palette {
        QString key;
        QString label;
    };

    static Theme &instance();

    // 所有可选的配色。顺序就是设置页下拉框里的顺序，第一项是默认。
    static QVector<Palette> palettes();

    // 当前配色。存在 QSettings 里，也可以用环境变量 EMSHELL_THEME 临时覆盖。
    // 改完需要重启才完全生效 —— 图标颜色是构造时烘进 QIcon 的，运行时换不干净。
    static QString paletteKey();
    static void setPaletteKey(const QString &key);

    // 取一个颜色，name 就是 QSS 里写的 token 名（不带 @）
    static QColor color(const QString &token);

    // 已经替换过 token 的 QSS 全文
    static QString styleSheet();

    // 界面字体（带中文回退）
    static QFont uiFont(int pixelSize = 14, QFont::Weight weight = QFont::Normal);

    // 等宽字体，终端和 IP/端口这类技术信息用它
    static QFont monoFont(int pixelSize = 13, QFont::Weight weight = QFont::Normal);

    // 终端正文用的等宽字体（Consolas + 关 hinting）。
    // 不能用 uiFont：终端按等宽格子排版，比例字体会让列对不齐。
    static QFont terminalFont(int pixelSize = 14);

    // 终端里的 16 色 ANSI 调色板，索引 0-15
    static const QColor *ansiPalette();

private:
    Theme() = default;

    static QString tokenValue(const QString &token, const QString &paletteKey);
    static QString buildStyleSheet(const QString &paletteKey);
};

} // namespace Em
