#pragma once

#include "model/Connection.h"
#include "ui/SideBar.h"

#include <QWidget>

class QLabel;
class QMenu;
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

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void wireSignals();

    // 原生标题栏的底色 / 文字色（Win11 的 DWM 属性）。
    // 只在用原生标题栏（kUseCustomTitleBar == false）时调。
    void applyNativeTitleBarTheme();

    void switchPage(AppPage page);
    // 重新读一次背景图设置（启动时 + 设置页改了之后）
    void reloadBackground();
    // 侧栏点了某个分组：切到它自己的页面（同一个 ConnectionsPage 实例换身份）
    void openGroup(const QString &groupId);
    void handleGroupsChanged();

    // 手动检查：打开对话框让它自己查
    void checkForUpdates();
    void openConnection(const Connection &connection);
    void newConnection();
    void editConnection(const QString &id);
    void showStatus(const QString &text, bool error = false);

    // 系统托盘。没有托盘的环境（极少数）下退化成普通窗口：关闭即退出。
    void createTrayIcon();
    void showFromTray();

    ConnectionStore *m_store = nullptr;
    KeyStore *m_keys = nullptr;

    TitleBar *m_titleBar = nullptr;
    SideBar *m_sideBar = nullptr;
    BackgroundHost *m_backgroundHost = nullptr;
    QStackedWidget *m_stack = nullptr;

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

    QWidget *m_statusBar = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTimer *m_statusTimer = nullptr;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    bool m_quitRequested = false;   // 托盘菜单点了「退出」，closeEvent 别再藏窗口
    bool m_trayHintShown = false;   // 「已最小化到托盘」只提示一次
};

} // namespace Em
