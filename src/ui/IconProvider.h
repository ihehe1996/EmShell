#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace Em {

// 图标集合。
//
// 图标素材就是内联的 SVG 源码（见 IconProvider.cpp），统一用白色描边，
// 取用的时候再把白色替换成想要的颜色，所以同一份图标能出任意配色，
// 深色主题下不用单独维护一套资源。
//
// 想换成真正的 .svg 文件也很简单：把 pathFor() 改成读 qrc 里的文件就行。
enum class Icon {
    Home,
    Console,   // 首页那张总览页，图标是个仪表盘
    Server,
    Grid,      // 2x2 四个方块，「所有连接」用
    Folder,
    History,
    Star,        // 实心，给收藏开关用（亮起来 = 已收藏）
    StarOutline, // 空心星，给侧栏「收藏夹」用
    Terminal,
    Monitor,   // 远程桌面：一块显示器，和 Terminal 的方框 + 提示符区分开
    Key,
    Settings,
    Search,
    Plus,
    Play,
    More,
    ChevronDown,
    ChevronRight,
    Minimize,
    Maximize,
    Restore,
    Close,
    Edit,
    Trash,
    Copy,
    Eye,
    EyeOff,
    Refresh,
    Check,
    Alert,
    Globe,
    User,
    Lock,
    Upload,
    Download,
    Plug,
    Link,
    Filter,
};

namespace Icons {

// 拿到指定颜色的图标。内部带缓存，反复调用没有额外开销。
QIcon icon(Icon which, const QColor &color, int size = 20);

// 同上，但要 QPixmap（画在自绘控件里时用）
QPixmap pixmap(Icon which, const QColor &color, int size = 20);

} // namespace Icons

} // namespace Em
