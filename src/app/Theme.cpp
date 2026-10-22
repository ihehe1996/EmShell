#include "app/Theme.h"

#include "app/palettes/Palette.h"

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

// 从登记表里取一套配色的 token 表。key 认不出来时退回默认配色。
const PaletteTable &tableFor(const QString &key)
{
    const PaletteSpec *spec = findPalette(key);
    if (!spec || !spec->table)
        return *defaultPalette().table;
    return *spec->table;
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
    // 当前配色对应的 16 色 ANSI 表。跟着 key 一起解析、一起作废，
    // 这样 ansiPalette() 在终端逐格取色的热路径里不用再查一次登记表。
    const QColor *ansi = nullptr;
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
    if (findPalette(overrideKey))
        return overrideKey;

    QSettings settings;
    const QString saved = settings.value(QString::fromLatin1(kModeKey)).toString();
    return findPalette(saved) ? saved : defaultPalette().key;
}

// 取当前配色那套 ANSI 表。没登记 ansi 的配色（理论上不该有）退回默认配色。
const QColor *ansiFor(const QString &key)
{
    const PaletteSpec *spec = findPalette(key);
    if (spec && spec->ansi)
        return spec->ansi;
    return defaultPalette().ansi;
}

// 保证缓存里的 key 和 ANSI 表都填好了。调用方必须已经持有 cache.mutex。
// key 和 ansi 分开判断：paletteKey() / styleSheet() 可能在第一次取色之前
// 就把 key 填进去，那时候 ansi 还是空的。
void ensurePalette(PaletteCache &cache)
{
    if (cache.key.isEmpty())
        cache.key = readPaletteKey();
    if (!cache.ansi)
        cache.ansi = ansiFor(cache.key);
}

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
    for (const PaletteSpec &spec : paletteSpecs())
        result.append({ spec.key, spec.label });
    return result;
}

// 当前配色的来源有两处：环境变量 EMSHELL_THEME=light|dark（值就是 key，
// 用来临时覆盖，调试和「不想进设置页只想看一眼」的时候很方便），以及 QSettings。
// 真正的读取逻辑在 readPaletteKey()，这里只负责缓存。
QString Theme::paletteKey()
{
    PaletteCache &cache = paletteCache();
    QMutexLocker locker(&cache.mutex);
    ensurePalette(cache);
    return cache.key;
}

void Theme::setPaletteKey(const QString &key, bool persist)
{
    // 不认识的 key 不往注册表里写，免得留个读出来也没用的垃圾值
    if (!findPalette(key))
        return;

    if (persist) {
        QSettings settings;
        settings.setValue(QString::fromLatin1(kModeKey), key);
    }

    // 换了配色，缓存里的 key 和颜色一起作废
    bool changed = false;
    {
        PaletteCache &cache = paletteCache();
        QMutexLocker locker(&cache.mutex);
        changed = (cache.key != key);
        cache.key = key;
        cache.colors.clear();
        cache.ansi = ansiFor(key);
    }

    // 锁外发信号：槽里会重装样式表、刷新控件，不能带着这把锁跑
    if (changed)
        emit instance().paletteChanged();
}

QString Theme::tokenValue(const QString &token, const QString &key)
{
    const auto &table = tableFor(key);
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

    ensurePalette(cache);

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
    const auto &table = tableFor(key);
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
    // 和取色共用一份缓存：这个函数在终端重绘里按字符格调用，
    // 不能每次都查登记表 / 比字符串。
    PaletteCache &cache = paletteCache();
    QMutexLocker locker(&cache.mutex);
    ensurePalette(cache);
    return cache.ansi;
}

} // namespace Em
