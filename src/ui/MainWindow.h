#pragma once

#include "model/Connection.h"
#include "ui/SideBar.h"

#include <QWidget>

class QStackedWidget;
class QSystemTrayIcon;
class QTimer;

namespace Em {

class BackgroundHost;
class ConnectionStore;
class ConnectionsPage;
class HistoryPage;
class KeyStore;
class KeysPage;
class SettingsPage;
class SideBar;
class TerminalPage;
class TitleBar;
class UpdateChecker;
class UpdatePage;

// 主窗口。
//
// 窗口是无边框的，标题栏自己画。拖动、双击最大化、边缘缩放这些仍然交给
// Windows 处理 —— 靠 nativeEvent 里应答 WM_NCHITTEST，这样系统的那套
// 吸附、贴边、多显示器缩放行为一个都不丢。
class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(ConnectionStore *store, KeyStore *keys, QWidget *parent = nullptr);

    // 启动时静默查一次。只有真发现新版本才弹窗；查失败 / 已是最新都不打扰。
    // main() 用 QTimer 延迟调它，所以必须是 public。
    void startSilentUpdateCheck();

    // 打开一条保存过的连接。SSH 会切到终端页开标签；RDP 是外部 mstsc 窗口，
    // 不建标签、也不切页。自检也走这一条真实路径。
    void openConnection(const Connection &connection);

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void wireSignals();

    // 原生标题栏的底色 / 文字色（Win11 的 DWM 属性）。
    // 只在用原生标题栏（kUseCustomTitleBar == false）时调。
    void applyNativeTitleBarTheme();

    void switchPage(AppPage page);
    // 把标签栏左侧那条补条对齐到当前标签栏（只在终端页、有标签时显示）
    void updateTabBandFill();
    // 重新读一次背景图设置（启动时 + 设置页改了之后）
    void reloadBackground();
    // 侧栏点了某个分组：切到它自己的页面（同一个 ConnectionsPage 实例换身份）
    void openGroup(const QString &groupId);
    void handleGroupsChanged();

    // 手动检查：打开对话框让它自己查
    void checkForUpdates();
    void newConnection();
    void editConnection(const QString &id);

    // 系统托盘。没有托盘的环境（极少数）下退化成普通窗口：关闭即退出。
    void createTrayIcon();
    // 右键托盘图标时弹**系统原生菜单**（不用 QMenu，避免被全局 QSS 套上自定义样式）
    void showTrayMenu();
    void showFromTray();

    ConnectionStore *m_store = nullptr;
    KeyStore *m_keys = nullptr;

    TitleBar *m_titleBar = nullptr;
    SideBar *m_sideBar = nullptr;
    BackgroundHost *m_backgroundHost = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_edgeShadow = nullptr;      // 侧栏右侧那条 12px 阴影
    QWidget *m_tabBandFill = nullptr;     // 盖住阴影顶部的标签栏补条

    ConnectionsPage *m_connections = nullptr;
    ConnectionsPage *m_favorites = nullptr;
    ConnectionsPage *m_groupPage = nullptr;   // 分组页：按点的分组重新配置
    QString m_currentGroupId;                 // 非空 = 当前停在某个分组页
    HistoryPage *m_history = nullptr;
    TerminalPage *m_terminal = nullptr;
    KeysPage *m_keysPage = nullptr;
    SettingsPage *m_settings = nullptr;
    UpdatePage *m_updatePage = nullptr;
    UpdateChecker *m_silentChecker = nullptr;

    QSystemTrayIcon *m_tray = nullptr;
    bool m_quitRequested = false;   // 托盘菜单点了「退出」，closeEvent 别再藏窗口
    bool m_trayHintShown = false;   // 「已最小化到托盘」只提示一次

    // 最大化窗口按住标题栏拖动的手动处理（原因见 MainWindow.cpp 的 nativeEvent）。
    // 按下时先记住位置并捕获鼠标，等光标真的动了再还原窗口，避免单击也把窗口还原掉。
    bool m_captionPressPending = false;
    QPoint m_captionPressPos;
};

} // namespace Em
