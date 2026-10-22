// EmShell —— 服务器连接工具
//
// 程序入口。真正的东西在 ui/MainWindow 里。

#include "SelfTest.h"
#include "app/AppConfig.h"
#include "app/Theme.h"
#include "model/ConnectionStore.h"
#include "model/KeyStore.h"
#include "ui/MainWindow.h"
#include "update/UpdateInstaller.h"

#include <QApplication>
#include <QIcon>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("EmShell"));
    app.setOrganizationName(QStringLiteral("Em"));
    // 版本号来自 CMakeLists.txt 的 project(VERSION)，见 app/AppConfig.h。
    // 更新器拿它和 GitHub release 的 tag 比，别在这里写死。
    app.setApplicationVersion(Em::appVersion());
    // 应用级图标：对话框、任务栏等默认都会从这里取。主窗口另外设了一份，
    // 但 QDialog 不会继承父窗口的图标，只会找 QApplication 的。
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app-logo.png")));

    // 关闭窗口不等于退出：主窗口会藏到系统托盘，从托盘菜单退出。
    // 没有托盘的环境里 closeEvent 会自己调 quit()，不会留下无窗口的进程。
    app.setQuitOnLastWindowClosed(false);

    // 注意别调 setApplicationDisplayName：原生标题栏会把它的值和 windowTitle
    // 拼成「标题 - 显示名」，我们要的就只是窗口标题本身。

    // Fusion 是 Qt 自带的一种不跟系统主题走的样式，各平台表现一致。
    // 用 Windows 原生样式的话，系统那边的一些绘制会盖过 QSS，深色主题就花了。
    app.setStyle(QStringLiteral("Fusion"));

    // 字体和样式表要在自检之前就装好 —— 自检里有几个模式会真的把窗口画出来，
    // 不装样式表的话看到的是 Fusion 默认外观，会被误当成界面出了问题。
    app.setFont(Em::Theme::uiFont(14));
    app.setStyleSheet(Em::Theme::styleSheet());

    // 自检模式：跑完就退
    if (Em::isSelfTestRequest(app.arguments()))
        return Em::runSelfTest(app.arguments());

    // 上次更新失败会留下临时目录（里面有日志），过期的顺手清掉。
    // 放在自检之后：自检不碰真实更新目录。
    Em::UpdateInstaller::cleanStaleWorkDirs();

    Em::ConnectionStore store;
    store.load();

    Em::KeyStore keys;
    keys.load();

    Em::MainWindow window(&store, &keys);
    window.show();

    // 设了 EMSHELL_UI_TRACE 就把运行时的控件几何写一份出来，调布局用
    Em::installUiTracerIfRequested(&window);

    // 启动时查一次更新。查到新版本由 MainWindow 把对话框摆出来；
    // 延迟这一会儿是为了别和窗口初始化抢时间，也不拖慢冷启动。
    QTimer::singleShot(Em:: updateStartupDelayMs(), &window,
                       &Em::MainWindow::startSilentUpdateCheck);

    return app.exec();
}
