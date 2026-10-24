#include "ui/IconProvider.h"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

namespace Em {
namespace Icons {

namespace {

// 所有图标统一 24x24 视图框、1.7 描边、圆头圆角，这样摆在一起粗细一致。
// 颜色一律先写 #FFFFFF，取用时替换成目标色。
const char *kStrokeOpen  = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#FFFFFF" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">)";
const char *kFillOpen    = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#FFFFFF" stroke="none">)";
const char *kSvgClose    = R"(</svg>)";

QString bodyFor(Icon which)
{
    switch (which) {
    case Icon::Home:
        return QStringLiteral("<path d='M3.5 10.7 12 3.8l8.5 6.9'/><path d='M5.8 9.6V19a1.2 1.2 0 0 0 1.2 1.2h10a1.2 1.2 0 0 0 1.2-1.2V9.6'/><path d='M9.8 20.2v-5.1h4.4v5.1'/>");
    case Icon::Console:
        // 仪表盘：上半圆的弧 + 指针 + 圆心点。和 Terminal 的方框形状差得远，
        // 摆在相邻两行不会看混。
        return QStringLiteral("<path d='M3 15.6a9 9 0 1 1 18 0'/><path d='M12 15.6 16.6 11'/><circle cx='12' cy='15.6' r='1.5'/>");
    case Icon::Server:
        return QStringLiteral("<rect x='3' y='4' width='18' height='6.4' rx='1.6'/><rect x='3' y='13.6' width='18' height='6.4' rx='1.6'/><path d='M6.6 7.2h.01M6.6 16.8h.01'/>");
    case Icon::Grid:
        // 2x2 四个圆角方块。方块之间要留出**看得见**的空隙：这里 6.9 的边长
        // 加 1.7 的描边，两个方块的中心距是 10.8，实际空隙约 2.2 —— 再窄一点
        // 18px 下四条边就糊成一块了，远看是个实心方块，不是四个。
        return QStringLiteral("<rect x='3.2' y='3.2' width='6.9' height='6.9' rx='1.8'/><rect x='14' y='3.2' width='6.9' height='6.9' rx='1.8'/><rect x='3.2' y='14' width='6.9' height='6.9' rx='1.8'/><rect x='14' y='14' width='6.9' height='6.9' rx='1.8'/>");
    case Icon::Folder:
        return QStringLiteral("<path d='M3 7.4A1.4 1.4 0 0 1 4.4 6h4l1.8 2h9.4A1.4 1.4 0 0 1 21 9.4v8.2a1.4 1.4 0 0 1-1.4 1.4H4.4A1.4 1.4 0 0 1 3 17.6z'/>");
    case Icon::History:
        return QStringLiteral("<path d='M3.4 12a8.6 8.6 0 1 0 2.6-6.2'/><path d='M3.2 4.4v4h4'/><path d='M12 7.6V12l3 1.8'/>");
    case Icon::Star:
    // 空心星和实心星是同一条路径，区别只在里面的 isFilled() —— 别在这边
    // 另写一个「描边版」路径，两份形状迟早会对不上。
    case Icon::StarOutline:
        return QStringLiteral("<path d='m12 3.6 2.6 5.3 5.8.85-4.2 4.1 1 5.8L12 16.9l-5.2 2.75 1-5.8-4.2-4.1 5.8-.85z'/>");
    case Icon::Terminal:
        return QStringLiteral("<rect x='3' y='4.6' width='18' height='14.8' rx='2'/><path d='m7.6 9.6 2.6 2.6-2.6 2.6'/><path d='M13 14.8h3.4'/>");
    case Icon::Monitor:
        // 显示器和 Terminal 的方框要分得开：这里下面有底座，屏幕上没有提示符
        return QStringLiteral("<rect x='3' y='4.6' width='18' height='12.4' rx='1.8'/><path d='M12 17v3.2'/><path d='M8.6 20.2h6.8'/>");
    case Icon::Key:
        return QStringLiteral("<circle cx='8' cy='12' r='3.4'/><path d='m11.4 12 9 0'/><path d='M17.4 12v3.2'/><path d='M20.4 12v2.2'/>");
    case Icon::Settings:
        return QStringLiteral("<circle cx='12' cy='12' r='3.1'/><path d='M19.1 14.6a1.6 1.6 0 0 0 .32 1.77l.06.06a1.95 1.95 0 1 1-2.76 2.76l-.06-.06a1.6 1.6 0 0 0-1.77-.32 1.6 1.6 0 0 0-.97 1.47v.17a1.95 1.95 0 1 1-3.9 0v-.09a1.6 1.6 0 0 0-1.05-1.47 1.6 1.6 0 0 0-1.77.32l-.06.06a1.95 1.95 0 1 1-2.76-2.76l.06-.06a1.6 1.6 0 0 0 .32-1.77 1.6 1.6 0 0 0-1.47-.97h-.17a1.95 1.95 0 1 1 0-3.9h.09A1.6 1.6 0 0 0 5.2 9.1a1.6 1.6 0 0 0-.32-1.77l-.06-.06A1.95 1.95 0 1 1 7.58 4.51l.06.06a1.6 1.6 0 0 0 1.77.32h.08a1.6 1.6 0 0 0 .97-1.47v-.17a1.95 1.95 0 1 1 3.9 0v.09a1.6 1.6 0 0 0 .97 1.47 1.6 1.6 0 0 0 1.77-.32l.06-.06a1.95 1.95 0 1 1 2.76 2.76l-.06.06a1.6 1.6 0 0 0-.32 1.77v.08a1.6 1.6 0 0 0 1.47.97h.17a1.95 1.95 0 1 1 0 3.9h-.09a1.6 1.6 0 0 0-1.47.97z'/>");
    case Icon::Search:
        return QStringLiteral("<circle cx='10.8' cy='10.8' r='6.4'/><path d='m15.6 15.6 4.4 4.4'/>");
    case Icon::Plus:
        return QStringLiteral("<path d='M12 5v14M5 12h14'/>");
    case Icon::Play:
        return QStringLiteral("<path d='M7.6 5.2 18.4 12 7.6 18.8z'/>");
    case Icon::More:
        return QStringLiteral("<circle cx='12' cy='5.6' r='1.5'/><circle cx='12' cy='12' r='1.5'/><circle cx='12' cy='18.4' r='1.5'/>");
    case Icon::ChevronDown:
        return QStringLiteral("<path d='m6.5 9.5 5.5 5.5 5.5-5.5'/>");
    case Icon::ChevronRight:
        return QStringLiteral("<path d='m9.5 6 5.5 5.5L9.5 17'/>");
    case Icon::Minimize:
        return QStringLiteral("<path d='M5 12h14'/>");
    case Icon::Maximize:
        return QStringLiteral("<rect x='5.5' y='5.5' width='13' height='13' rx='1.6'/>");
    case Icon::Restore:
        return QStringLiteral("<rect x='4.6' y='7.8' width='11.6' height='11.6' rx='1.6'/><path d='M8.4 7.8V6.2A1.6 1.6 0 0 1 10 4.6h7.8A1.6 1.6 0 0 1 19.4 6.2V14a1.6 1.6 0 0 1-1.6 1.6h-1.6'/>");
    case Icon::Close:
        return QStringLiteral("<path d='M6.4 6.4l11.2 11.2M17.6 6.4 6.4 17.6'/>");
    case Icon::Edit:
        return QStringLiteral("<path d='M4.6 19.4h3.6L19 8.6a1.6 1.6 0 0 0 0-2.26l-1.34-1.34a1.6 1.6 0 0 0-2.26 0L4.6 15.8z'/><path d='m14.4 6.6 3 3'/>");
    case Icon::Trash:
        return QStringLiteral("<path d='M4.6 6.8h14.8'/><path d='M9.4 6.8V5.4a1.4 1.4 0 0 1 1.4-1.4h2.4a1.4 1.4 0 0 1 1.4 1.4v1.4'/><path d='M6.6 6.8 7.5 19a1.4 1.4 0 0 0 1.4 1.3h6.2a1.4 1.4 0 0 0 1.4-1.3l.9-12.2'/><path d='M10.4 10.4v6M13.6 10.4v6'/>");
    case Icon::Copy:
        return QStringLiteral("<rect x='8.6' y='8.6' width='11.8' height='11.8' rx='1.8'/><path d='M5.4 15.4H4.6a1.8 1.8 0 0 1-1.8-1.8V4.6a1.8 1.8 0 0 1 1.8-1.8h9a1.8 1.8 0 0 1 1.8 1.8v.8'/>");
    case Icon::Eye:
        return QStringLiteral("<path d='M2.4 12S5.7 5.8 12 5.8 21.6 12 21.6 12 18.3 18.2 12 18.2 2.4 12 2.4 12z'/><circle cx='12' cy='12' r='3'/>");
    case Icon::EyeOff:
        return QStringLiteral("<path d='M4.2 4.2 19.8 19.8'/><path d='M9.6 9.7A3 3 0 0 0 12 15a3 3 0 0 0 2.3-1.1'/><path d='M6.5 6.6C4.2 8.2 2.4 12 2.4 12s3.3 6.2 9.6 6.2c1.7 0 3.1-.5 4.3-1.2'/><path d='M18.1 15.2c1.6-1.5 3.5-3.2 3.5-3.2S18.3 5.8 12 5.8c-.8 0-1.5.1-2.2.3'/>");
    case Icon::Refresh:
        return QStringLiteral("<path d='M20.6 12a8.6 8.6 0 1 1-2.6-6.2'/><path d='M20.8 4.4v4h-4'/>");
    case Icon::Check:
        return QStringLiteral("<path d='m5 12.8 4.6 4.6L19 6.6'/>");
    case Icon::Alert:
        return QStringLiteral("<path d='M12 3.6 21 19.4H3z'/><path d='M12 9.6v4.2'/><path d='M12 16.9h.01'/>");
    case Icon::Globe:
        return QStringLiteral("<circle cx='12' cy='12' r='8.6'/><path d='M3.4 12h17.2'/><path d='M12 3.4a13 13 0 0 1 0 17.2 13 13 0 0 1 0-17.2z'/>");
    case Icon::User:
        return QStringLiteral("<circle cx='12' cy='8' r='3.8'/><path d='M4.8 20.4a7.2 7.2 0 0 1 14.4 0'/>");
    case Icon::Lock:
        return QStringLiteral("<rect x='4.6' y='10.4' width='14.8' height='9.6' rx='2'/><path d='M8 10.4V7.8a4 4 0 0 1 8 0v2.6'/>");
    case Icon::Upload:
        return QStringLiteral("<path d='M12 15.6V4.4'/><path d='m7.6 8.8 4.4-4.4 4.4 4.4'/><path d='M4.6 15.6v2.6a1.8 1.8 0 0 0 1.8 1.8h11.2a1.8 1.8 0 0 0 1.8-1.8v-2.6'/>");
    case Icon::Download:
        return QStringLiteral("<path d='M12 4.4v11.2'/><path d='m16.4 11.2-4.4 4.4-4.4-4.4'/><path d='M4.6 15.6v2.6a1.8 1.8 0 0 0 1.8 1.8h11.2a1.8 1.8 0 0 0 1.8-1.8v-2.6'/>");
    case Icon::Plug:
        return QStringLiteral("<path d='M9 3.6v5.2M15 3.6v5.2'/><path d='M6.4 8.8h11.2v3.4a5.6 5.6 0 0 1-11.2 0z'/><path d='M12 17.8v2.6'/>");
    case Icon::Link:
        return QStringLiteral("<path d='M10.4 13.6a4 4 0 0 0 5.7 0l2.6-2.6a4 4 0 1 0-5.7-5.7l-1.3 1.3'/><path d='M13.6 10.4a4 4 0 0 0-5.7 0l-2.6 2.6a4 4 0 1 0 5.7 5.7l1.3-1.3'/>");
    case Icon::Filter:
        return QStringLiteral("<path d='M3.6 5.4h16.8l-6.6 7.8v5.6l-3.6 1.8v-7.4z'/>");
    }
    return {};
}

// 需要实心填充的图标（星星选中态之类）
bool isFilled(Icon which)
{
    return which == Icon::Star || which == Icon::Play;
}

QString svgFor(Icon which, const QString &color)
{
    QString svg = QString::fromLatin1(kStrokeOpen) + bodyFor(which) + QString::fromLatin1(kSvgClose);
    svg.replace(QLatin1String("#FFFFFF"), color);
    return svg;
}

} // namespace

QPixmap pixmap(Icon which, const QColor &color, int size)
{
    const qreal dpr = qApp->devicePixelRatio();

    // 同一份图标会被反复请求，缓存一下省掉重复解析 SVG 的开销
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1|%2|%3|%4")
                            .arg(int(which))
                            .arg(color.name(QColor::HexArgb))
                            .arg(size)
                            .arg(dpr);

    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    // 画布按物理像素开，但 **dpr 要等画完再设**，而且画的时必须给死目标矩形。
    //
    // 这两条缺一个，图标就会被裁掉右边和下面一条（125% 缩放时大约 20%，
    // 表现为房子的右墙、文件夹的右边全没了）。
    // 原因：QSvgRenderer::render() 不给矩形时是按 QPainter 的**逻辑**视口铺图的，
    // 而这里是按**物理**像素开的画布，两个单位差一个 dpr；再叠上 QPainter 自己
    // 那次 dpr 缩放，图就画到画布外面去了。
    const int px = int(size * dpr);
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);

    QSvgRenderer renderer(svgFor(which, color.name(QColor::HexRgb)).toUtf8());

    // 星星这类图标要实心，描边版在 16px 下几乎看不出形状
    if (isFilled(which)) {
        QString filled = QString::fromLatin1(kFillOpen) + bodyFor(which)
                         + QString::fromLatin1(kSvgClose);
        filled.replace(QLatin1String("#FFFFFF"), color.name(QColor::HexRgb));
        renderer.load(filled.toUtf8());
    }

    {
        // 此时 dpr 还是 1，逻辑坐标 == 物理坐标，目标矩形就是整块画布
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (renderer.isValid())
            renderer.render(&p, QRectF(0, 0, px, px));
    }

    // 画完了再声明 dpr，之后用的人看到的才是 size x size 的逻辑尺寸
    pm.setDevicePixelRatio(dpr);

    cache.insert(key, pm);
    return pm;
}

QIcon icon(Icon which, const QColor &color, int size)
{
    return QIcon(pixmap(which, color, size));
}

} // namespace Icons
} // namespace Em
