#include "SelfTest.h"

#include "app/AppConfig.h"
#include "app/Theme.h"
#include "model/ConnectionStore.h"
#include "model/KeyStore.h"
#include "ssh/SshSession.h"
#include "terminal/LocalSession.h"
#include "terminal/TerminalScreen.h"
#include "terminal/TerminalView.h"
#include "ui/MainWindow.h"
#include "ui/SideBar.h"
#include "ui/ConnectionDialog.h"
#include "ui/SettingsPage.h"
#include "ui/TerminalPage.h"
#include "ui/UpdateDialog.h"
#include "ui/UpdatePage.h"
#include "ui/Widgets.h"
#include "update/ReleaseInfo.h"
#include "update/UpdateChecker.h"
#include "update/UpdateInstaller.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>

#include <utility>

namespace Em {

namespace {

int g_pass = 0;
int g_fail = 0;
QStringList g_log;

void log(const QString &line)
{
    g_log << line;

    // 同时立刻落到磁盘。自检里如果有哪一步把程序挂住了，看文件就知道走到哪了，
    // 不用等整个流程跑完（以前是只在最后写一次，一挂就什么线索都没有）。
    QFile f(QStringLiteral("selftest.log"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << line << "\n";
    }
}

void check(bool ok, const QString &what, const QString &detail = QString())
{
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
        g_log << QStringLiteral("  FAIL: %1%2")
                     .arg(what, detail.isEmpty() ? QString() : QStringLiteral(" -> ") + detail);
    }
}

void flushLog()
{
    log(QStringLiteral("== 通过 %1 项，失败 %2 项 ==").arg(g_pass).arg(g_fail));

    QFile out(QStringLiteral("selftest.log"));
    if (out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream ts(&out);
        for (const QString &line : g_log)
            ts << line << "\n";
    }
    for (const QString &line : g_log)
        qInfo().noquote() << line;
}

// ---------------------------------------------------------------------------
// VT 解析器断言
// ---------------------------------------------------------------------------
int runParserTest()
{
    log(QStringLiteral("== TerminalScreen 解析器自检 =="));

    {
        TerminalScreen s(80, 24);
        s.feed("hello");
        check(s.lineText(0) == QStringLiteral("hello"), QStringLiteral("普通文本写入"));
        check(s.cursorX() == 5 && s.cursorY() == 0, QStringLiteral("光标位置"),
              QStringLiteral("x=%1 y=%2").arg(s.cursorX()).arg(s.cursorY()));
    }
    {
        TerminalScreen s(80, 24);
        s.feed("\x1b[5;10H");
        check(s.cursorX() == 9 && s.cursorY() == 4, QStringLiteral("CUP 定位（行;列）"),
              QStringLiteral("x=%1 y=%2").arg(s.cursorX()).arg(s.cursorY()));
    }
    {
        TerminalScreen s(80, 24);
        s.feed("\x1b[31mR");
        const Cell &c = s.lineAt(0)[0];
        check(isIndexedColor(c.fg) && indexedValue(c.fg) == 1, QStringLiteral("SGR 31 红色"));
        check(c.ch == U'R', QStringLiteral("SGR 之后的字符"));
    }
    {
        TerminalScreen s(80, 24);
        s.feed("\x1b[38;5;196mA\x1b[38;2;10;20;30mB");
        const Cell &a = s.lineAt(0)[0];
        const Cell &b = s.lineAt(0)[1];
        check(isIndexedColor(a.fg) && indexedValue(a.fg) == 196, QStringLiteral("256 色前景"));
        check(!isIndexedColor(b.fg) && !isDefaultColor(b.fg), QStringLiteral("真彩色前景"));
    }
    {
        TerminalScreen s(80, 24);
        s.feed("\x1b[1;31mX");
        const Cell &c = s.lineAt(0)[0];
        check((c.flags & CellBold) != 0, QStringLiteral("粗体位"));
        // 屏幕里存的仍是基色 1，粗体提亮是在渲染层做的，
        // 因为只有画的时候才知道要不要按粗体映射
        check(indexedValue(c.fg) == 1, QStringLiteral("粗体红色在屏幕里仍是基色"));
    }
    {
        TerminalScreen s(80, 24);
        s.feed("abcdef\x1b[2J");
        check(s.lineText(0).isEmpty(), QStringLiteral("CSI 2J 清屏"));
    }
    {
        TerminalScreen s(80, 5);
        for (int i = 0; i < 12; ++i)
            s.feed(QByteArray("line") + QByteArray::number(i) + "\r\n");
        check(s.scrollbackCount() == 8, QStringLiteral("行满后进入回滚缓冲"),
              QStringLiteral("count=%1").arg(s.scrollbackCount()));
        check(s.lineText(0) == QStringLiteral("line0"), QStringLiteral("回滚第一行内容"),
              s.lineText(0));
    }
    {
        // 启动时 TerminalView 会先按默认小尺寸缩一次网格，再随布局涨回来。
        // 缩掉的那些空白行不能进回滚缓冲，否则往上滚会看到一大片空屏。
        TerminalScreen s(80, 24);
        s.resize(12, 2);
        s.resize(139, 48);
        check(s.scrollbackCount() == 0, QStringLiteral("缩小窗口不把空白行推进回滚缓冲"),
              QStringLiteral("count=%1").arg(s.scrollbackCount()));
    }
    {
        TerminalScreen s(10, 6);
        s.feed("a\r\nb\r\nc\r\nd\r\ne\r\nf");
        s.feed("\x1b[2;4r");      // 只滚第 2 到第 4 行
        s.feed("\x1b[4;1H\n");
        check(s.lineText(0) == QStringLiteral("a"), QStringLiteral("滚动区外第一行不动"),
              s.lineText(0));
        check(s.lineText(1) == QStringLiteral("c"), QStringLiteral("滚动区内向上滚"),
              s.lineText(1));
    }
    {
        TerminalScreen s(20, 24);
        s.feed(QStringLiteral("中文").toUtf8());
        check(s.lineText(0) == QStringLiteral("中文"), QStringLiteral("中文解码"), s.lineText(0));
        check(s.cursorX() == 4, QStringLiteral("中文占 4 列"),
              QStringLiteral("x=%1").arg(s.cursorX()));
        check((s.lineAt(0)[0].flags & CellWide) != 0, QStringLiteral("宽字符标记"));
        check(s.lineAt(0)[1].ch == U'\0', QStringLiteral("宽字符右半格是占位"));
    }
    {
        TerminalScreen s(80, 24);
        s.feed("\x1b[3");
        s.feed("1mZ");
        const Cell &c = s.lineAt(0)[0];
        check(c.ch == U'Z' && indexedValue(c.fg) == 1, QStringLiteral("跨批次的转义序列"));
    }
    {
        TerminalScreen s(80, 24);
        const QByteArray cn = QStringLiteral("汉").toUtf8();
        s.feed(cn.left(1));
        s.feed(cn.mid(1));
        check(s.lineText(0) == QStringLiteral("汉"), QStringLiteral("跨批次的多字节字符"),
              s.lineText(0));
    }
    {
        TerminalScreen s(20, 24);
        s.feed("main");
        s.feed("\x1b[?1049h");
        s.feed("alt");
        check(s.isAlternateScreen(), QStringLiteral("进入备用屏幕"));
        check(s.lineText(0) == QStringLiteral("alt"), QStringLiteral("备用屏幕是独立缓冲区"),
              s.lineText(0));
        s.feed("\x1b[?1049l");
        check(!s.isAlternateScreen(), QStringLiteral("退出备用屏幕"));
        check(s.lineText(0) == QStringLiteral("main"), QStringLiteral("主屏幕内容恢复"),
              s.lineText(0));
    }
    {
        TerminalScreen s(80, 24);
        s.feed("\x1b[3;7H");
        s.feed("\x1b[6n");
        const QByteArray r = s.takeResponse();
        check(r == "\x1b[3;7R", QStringLiteral("DSR 光标位置应答"), QString::fromLatin1(r));
    }
    {
        TerminalScreen s(20, 24);
        s.feed("0123456789\x1b[1;4H\x1b[K");
        check(s.lineText(0) == QStringLiteral("012"), QStringLiteral("CSI K 擦到行尾"),
              s.lineText(0));
    }
    {
        TerminalScreen s(20, 24);
        s.feed("abcdef\x1b[1;3H\x1b[2P");
        check(s.lineText(0) == QStringLiteral("abef"), QStringLiteral("CSI P 删除字符"),
              s.lineText(0));
    }
    {
        TerminalScreen s(20, 24);
        s.feed("\x1b[?25l");
        check(!s.cursorVisible(), QStringLiteral("隐藏光标"));
        s.feed("\x1b[?25h");
        check(s.cursorVisible(), QStringLiteral("显示光标"));
    }
    {
        TerminalScreen s(20, 24);
        s.feed("\x1b]0;my-title\x07");
        check(s.title() == QStringLiteral("my-title"), QStringLiteral("OSC 标题"), s.title());
    }
    {
        TerminalScreen s(10, 24);
        s.feed("0123456789AB");
        check(s.lineText(0) == QStringLiteral("0123456789"), QStringLiteral("第一行填满"),
              s.lineText(0));
        check(s.lineText(1) == QStringLiteral("AB"), QStringLiteral("自动换到第二行"),
              s.lineText(1));
    }

    flushLog();
    return g_fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 真起一个 ConPTY 会话
// ---------------------------------------------------------------------------
int runPtyTest()
{
    log(QStringLiteral("== ConPTY 会话自检 =="));

    TerminalScreen screen(80, 24);
    // 默认用 LocalSession::defaultShell()；排查具体 shell 时用环境变量覆盖
    LocalSession session(qEnvironmentVariable("EMSHELL_PTY_SHELL"));

    qint64 totalBytes = 0;
    int chunkCount = 0;
    QObject::connect(&session, &TerminalSession::dataReceived,
                     [&screen, &totalBytes, &chunkCount](const QByteArray &d) {
                         totalBytes += d.size();
                         ++chunkCount;
                         screen.feed(d);
                     });

    QString error;
    if (!session.start(80, 24, &error)) {
        log(QStringLiteral("  FAIL: 启动会话失败 -> %1").arg(error));
        flushLog();
        return 1;
    }
    log(QStringLiteral("  会话已启动：%1").arg(LocalSession::defaultShell()));

    QTimer::singleShot(3000, [&session] { session.write("echo EMSHELL_PTY_OK\r"); });
    QTimer::singleShot(9000, [] { qApp->quit(); });
    qApp->exec();

    bool found = false;
    for (int i = 0; i < screen.totalLines(); ++i) {
        if (screen.lineText(i).contains(QStringLiteral("EMSHELL_PTY_OK"))) {
            found = true;
            break;
        }
    }
    log(QStringLiteral("  收到 %1 块数据，共 %2 字节").arg(chunkCount).arg(totalBytes));
    check(found, QStringLiteral("命令输出回到终端"));

    log(QStringLiteral("  ---- 屏幕内容 ----"));
    for (int i = 0; i < screen.totalLines(); ++i) {
        const QString t = screen.lineText(i);
        if (!t.isEmpty())
            log(QStringLiteral("  | %1").arg(t));
    }

    session.shutdown();
    flushLog();
    return g_fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 真连一台服务器，走一遍完整流程：主机密钥确认 -> 输密码 -> 跑命令 -> 读回结果
//
// 密码从环境变量 EMSHELL_SSH_PASSWORD 读，只活在内存里，不落任何文件。
// ---------------------------------------------------------------------------
int runSshTest(const QStringList &arguments)
{
    log(QStringLiteral("== SSH 连接自检 =="));

    const int flagIndex = arguments.indexOf(QStringLiteral("--sshtest"));
    if (flagIndex < 0 || flagIndex + 1 >= arguments.size()) {
        log(QStringLiteral("  用法：EmShell.exe --sshtest 用户@主机 [端口]"));
        flushLog();
        return 2;
    }

    const QString target = arguments.at(flagIndex + 1);
    const int port = (flagIndex + 2 < arguments.size())
                         ? arguments.at(flagIndex + 2).toInt()
                         : 22;
    const QString password = QString::fromUtf8(qgetenv("EMSHELL_SSH_PASSWORD"));

    log(QStringLiteral("  目标：%1:%2").arg(target).arg(port));
    log(QStringLiteral("  密码：%1").arg(password.isEmpty()
                                            ? QStringLiteral("（环境变量里没给，只测到密码提示为止）")
                                            : QStringLiteral("已从环境变量读入")));

    // accept-new：第一次连的机器自动记下指纹，known_hosts 里已有的照常校验。
    // 测试要无人值守，所以不能用默认的交互式指纹确认。
    const QString command = QStringLiteral(
        "ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=15 -p %1 %2")
        .arg(port).arg(target);

    TerminalScreen screen(100, 30);
    LocalSession session;

    QObject::connect(&session, &TerminalSession::dataReceived,
                     [&screen](const QByteArray &d) { screen.feed(d); });

    QString error;
    if (!session.start(100, 30, &error)) {
        log(QStringLiteral("  FAIL: 起会话失败 -> %1").arg(error));
        flushLog();
        return 1;
    }
    // 让本地 shell 去跑这条 ssh 命令
    session.write((command + QStringLiteral("\r")).toUtf8());

    auto screenHas = [&screen](const char *needle) {
        for (int i = 0; i < screen.totalLines(); ++i) {
            if (screen.lineText(i).contains(QLatin1String(needle), Qt::CaseInsensitive))
                return true;
        }
        return false;
    };

    bool sentPassword = false;
    bool sentEcho = false;
    bool sawSuccess = false;
    int waitedMs = 0;

    // 每 400ms 看一眼屏幕上出现了什么，据此决定下一步动作
    QTimer poll;
    poll.setInterval(400);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        waitedMs += 400;

        if (screenHas("password:") && !password.isEmpty() && !sentPassword) {
            session.write(password.toUtf8() + "\r");
            sentPassword = true;
            log(QStringLiteral("  -> 已提交密码"));
            return;
        }

        // 密码提交后，等 shell 提示符回来，再敲一条命令
        if (sentPassword && !sentEcho && waitedMs > 2000) {
            const bool atPrompt = screenHas("$") || screenHas("#") || screenHas(">");
            if (atPrompt || waitedMs > 9000) {
                session.write("echo EMSHELL_SSH_OK\r");
                sentEcho = true;
                log(QStringLiteral("  -> 已发送验证命令"));
            }
        }

        if (sentEcho && screenHas("EMSHELL_SSH_OK")) {
            sawSuccess = true;
            qApp->quit();
        }

        if (waitedMs > 45000) {
            log(QStringLiteral("  … 45 秒还没拿到结果，放弃"));
            qApp->quit();
        }
    });
    poll.start();

    QTimer::singleShot(48000, [] { qApp->quit(); });
    qApp->exec();
    poll.stop();
    session.shutdown();

    check(sawSuccess, QStringLiteral("SSH 登录并执行命令"));

    log(QStringLiteral("  ---- 远端屏幕内容 ----"));
    for (int i = 0; i < screen.totalLines(); ++i) {
        const QString t = screen.lineText(i);
        if (!t.isEmpty())
            log(QStringLiteral("  | %1").arg(t));
    }

    flushLog();
    return g_fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// UI 布局量测：把控件树几何打出来。
// 排查「右边一栏被挤出窗口」这类问题时，比盯着截图猜靠谱得多。
// ---------------------------------------------------------------------------
void collectWidgetTree(QWidget *w, int depth, int maxDepth, QStringList &out)
{
    if (!w || depth > maxDepth)
        return;

    const QRect g = w->geometry();
    const QSize hint = w->sizeHint();
    const QSize minHint = w->minimumSizeHint();
    const QSizePolicy sp = w->sizePolicy();

    out << QStringLiteral("%1%2%3  geo=%4,%5 %6x%7  hint=%8x%9  minHint=%10x%11  pol=%12/%13")
               .arg(QString(depth * 2, QLatin1Char(' ')))
               .arg(QString::fromLatin1(w->metaObject()->className()))
               .arg(w->objectName().isEmpty() ? QString()
                                              : QStringLiteral("#") + w->objectName())
               .arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height())
               .arg(hint.width()).arg(hint.height())
               .arg(minHint.width()).arg(minHint.height())
               .arg(int(sp.horizontalPolicy()))
               .arg(int(sp.verticalPolicy()));

    // 滚动区把内部控件单独拎出来看，否则滚动内容的尺寸看不到
    if (const auto *area = qobject_cast<const QScrollArea *>(w)) {
        if (area->widget())
            collectWidgetTree(area->widget(), depth + 1, maxDepth, out);
        return;
    }

    const QList<QWidget *> kids = w->findChildren<QWidget *>(Qt::FindDirectChildrenOnly);
    for (QWidget *child : kids)
        collectWidgetTree(child, depth + 1, maxDepth, out);
}

} // namespace

void dumpUiGeometry(QWidget *root, const QString &path)
{
    if (!root)
        return;

    QStringList lines;
    const QScreen *screen = root->screen();
    lines << QStringLiteral("窗口 width=%1 height=%2  devicePixelRatio=%3  屏幕 dpr=%4  逻辑 DPI=%5")
                 .arg(root->width())
                 .arg(root->height())
                 .arg(root->devicePixelRatioF())
                 .arg(screen ? screen->devicePixelRatio() : 0.0)
                 .arg(screen ? screen->logicalDotsPerInch() : 0.0);
    lines << QStringLiteral("---- 控件树 ----");
    // 12 层够到连接表行里的每个格子（滚动区本身就要吃掉好几层）
    collectWidgetTree(root, 0, 12, lines);

    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream ts(&out);
        for (const QString &line : lines)
            ts << line << "\n";
    }
    for (const QString &line : lines)
        qInfo().noquote() << line;
}

void installUiTracerIfRequested(QWidget *root)
{
    if (!qEnvironmentVariableIsSet("EMSHELL_UI_TRACE"))
        return;

    QTimer::singleShot(3000, root, [root] {
        dumpUiGeometry(root, QStringLiteral("uitrace.log"));

        // 顺手存一张窗口截图。
        //
        // 别用 PowerShell 的 PrintWindow 抓这个窗口 —— 它拿到的窗口矩形是
        // 按 DPI 虚拟化过的（本机 125% 缩放，真实物理尺寸是逻辑尺寸的 1.25 倍），
        // 位图按虚拟尺寸分配，渲染时右边会被裁掉一截，看着像布局溢出，其实是
        // 截图截漏了。QWidget::grab() 自己走 Qt 的绘制路径，没有这个问题。
        root->grab().save(QStringLiteral("uitrace.png"));
    });
}

namespace {

int runUiDump(const QStringList &arguments)
{
    log(QStringLiteral("== UI 布局量测 =="));

    int width = 1380;
    int height = 880;
    const int wIndex = arguments.indexOf(QStringLiteral("--uidump"));
    if (wIndex + 1 < arguments.size())
        width = arguments.at(wIndex + 1).toInt();
    if (wIndex + 2 < arguments.size())
        height = arguments.at(wIndex + 2).toInt();

    ConnectionStore store;
    store.load();
    KeyStore keys;
    keys.load();

    MainWindow window(&store, &keys);
    window.resize(width, height);
    window.show();

    QTimer::singleShot(1500, [&] {
        dumpUiGeometry(&window, QStringLiteral("selftest.log.tree"));
        log(QStringLiteral("---- 控件树 ----"));
        QFile f(QStringLiteral("selftest.log.tree"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            log(QString::fromUtf8(f.readAll()));
        qApp->quit();
    });
    QTimer::singleShot(4000, [] { qApp->quit(); });

    qApp->exec();
    flushLog();
    return 0;
}

// ---------------------------------------------------------------------------
// 原生 SSH（libssh2）会话测试：不经过系统 ssh 客户端，直接用 SshSession。
//   --sshnative 用户@主机 [端口]
// 密码从环境变量 EMSHELL_SSH_PASSWORD 读。
// ---------------------------------------------------------------------------
int runSshNativeTest(const QStringList &arguments)
{
    // 从头开始写，别把上一次的残留接在后面
    QFile::remove(QStringLiteral("selftest.log"));
    log(QStringLiteral("== 原生 SSH 会话自检（libssh2）=="));

    const int flagIndex = arguments.indexOf(QStringLiteral("--sshnative"));
    if (flagIndex < 0 || flagIndex + 1 >= arguments.size()) {
        log(QStringLiteral("  用法：EmShell.exe --sshnative 用户@主机 [端口]"));
        flushLog();
        return 2;
    }

    QString target = arguments.at(flagIndex + 1);
    const int port = (flagIndex + 2 < arguments.size())
                         ? arguments.at(flagIndex + 2).toInt()
                         : 22;

    SshSession::Options options;
    options.port = port;
    const int at = target.indexOf(QLatin1Char('@'));
    if (at > 0) {
        options.username = target.left(at);
        options.host = target.mid(at + 1);
    } else {
        options.host = target;
    }
    options.password = QString::fromUtf8(qgetenv("EMSHELL_SSH_PASSWORD"));

    log(QStringLiteral("  目标：%1@%2:%3").arg(options.username, options.host)
            .arg(options.port));
    log(QStringLiteral("  known_hosts：%1")
            .arg(QDir::homePath() + QStringLiteral("/.ssh/known_hosts")));

    TerminalScreen screen(100, 30);
    SshSession session(options);

    QObject::connect(&session, &TerminalSession::dataReceived,
                     [&screen](const QByteArray &d) { screen.feed(d); });

    QObject::connect(&session, &SshSession::stateChanged,
                     [](SshSession::State s, const QString &detail) {
                         static const char *names[] = {
                             "空闲", "连接中", "握手中", "校验主机密钥",
                             "认证中", "打开终端", "就绪", "失败", "已关闭"
                         };
                         log(QStringLiteral("  [状态] %1  %2")
                                 .arg(QString::fromUtf8(names[int(s)]), detail));
                     });

    // 测试模式：指纹一律接受，但要把指纹打出来让人看得见。
    // 真实界面里这一步是弹框让用户判断的，不会自动放行。
    QObject::connect(&session, &SshSession::hostKeyUnknown,
                     [&session](const QString &fingerprint, const QString &keyType) {
                         log(QStringLiteral("  [主机密钥] %1  %2  -> 测试模式自动接受")
                                 .arg(keyType, fingerprint));
                         session.answerHostKeyPrompt(true);
                         log(QStringLiteral("  [主机密钥] 已把选择交回工作线程"));
                     });

    QString error;
    if (!session.start(100, 30, &error)) {
        log(QStringLiteral("  FAIL: 启动失败 -> %1").arg(error));
        flushLog();
        return 1;
    }

    auto screenHas = [&screen](const char *needle) {
        for (int i = 0; i < screen.totalLines(); ++i) {
            if (screen.lineText(i).contains(QLatin1String(needle), Qt::CaseInsensitive))
                return true;
        }
        return false;
    };

    bool sentCommand = false;
    bool sawSuccess = false;
    int waitedMs = 0;

    QTimer poll;
    poll.setInterval(300);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        waitedMs += 300;

        if (!sentCommand && session.state() == SshSession::State::Ready
            && waitedMs > 800) {
            session.write("echo EMSHELL_NATIVE_OK\r");
            sentCommand = true;
            log(QStringLiteral("  -> 已发送验证命令"));
        }

        if (sentCommand && screenHas("EMSHELL_NATIVE_OK")) {
            sawSuccess = true;
            qApp->quit();
        }

        if (session.state() == SshSession::State::Failed) {
            log(QStringLiteral("  FAIL: %1").arg(session.lastError()));
            qApp->quit();
        }

        if (waitedMs > 40000) {
            log(QStringLiteral("  … 40 秒没结果，放弃"));
            qApp->quit();
        }
    });
    poll.start();

    QTimer::singleShot(43000, [] { qApp->quit(); });
    qApp->exec();
    poll.stop();

    check(sawSuccess, QStringLiteral("原生 SSH 登录并执行命令"));

    log(QStringLiteral("  ---- 远端屏幕内容 ----"));
    for (int i = 0; i < screen.totalLines(); ++i) {
        const QString t = screen.lineText(i);
        if (!t.isEmpty())
            log(QStringLiteral("  | %1").arg(t));
    }

    session.shutdown();
    flushLog();
    return g_fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 走完整界面路径的 SSH 测试：真的把连接喂给 TerminalPage，看终端里画出什么。
//   --sshgui 用户@主机 [端口]
// 密码从 EMSHELL_SSH_PASSWORD 读。顺便存一张窗口截图，肉眼也能确认。
// ---------------------------------------------------------------------------
int runUiSshTest(const QStringList &arguments)
{
    QFile::remove(QStringLiteral("selftest.log"));
    log(QStringLiteral("== 界面路径 SSH 自检 =="));

    const int flagIndex = arguments.indexOf(QStringLiteral("--sshgui"));
    if (flagIndex < 0 || flagIndex + 1 >= arguments.size()) {
        log(QStringLiteral("  用法：EmShell.exe --sshgui 用户@主机 [端口]"));
        flushLog();
        return 2;
    }

    const QString target = arguments.at(flagIndex + 1);
    const int port = (flagIndex + 2 < arguments.size()) ? arguments.at(flagIndex + 2).toInt() : 22;
    const QString password = QString::fromUtf8(qgetenv("EMSHELL_SSH_PASSWORD"));

    Connection connection;
    const int at = target.indexOf(QLatin1Char('@'));
    connection.username = at > 0 ? target.left(at) : QString();
    connection.host = at > 0 ? target.mid(at + 1) : target;
    connection.port = port;
    connection.name = QStringLiteral("自检 %1").arg(connection.host);
    connection.password = password;
    connection.savePassword = true;
    connection.auth = AuthType::Password;

    log(QStringLiteral("  目标：%1").arg(connection.identity()));

    ConnectionStore store;
    store.load();
    KeyStore keys;
    keys.load();

    MainWindow window(&store, &keys);
    window.resize(1380, 880);
    window.show();

    auto *terminal = window.findChild<TerminalPage *>();
    if (!terminal) {
        log(QStringLiteral("  FAIL: 找不到 TerminalPage"));
        flushLog();
        return 1;
    }

    // 必须先把 stack 切到终端页。不切的话这个页面从来没有被布局过，
    // 尺寸是默认的小值，申请出来的 pty 只有十来列宽。
    if (auto *stack = window.findChild<QStackedWidget *>())
        stack->setCurrentWidget(terminal);

    bool sawSuccess = false;
    bool sentCommand = false;
    int waitedMs = 0;

    QTimer poll;
    poll.setInterval(300);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        waitedMs += 300;

        if (waitedMs == 600)
            terminal->openConnection(connection);

        auto *view = terminal->findChild<TerminalView *>();
        if (view) {
            if (!sentCommand && waitedMs > 4000 && view->session()
                && !view->sessionEnded()) {
                log(QStringLiteral("  终端尺寸：%1 × %2")
                        .arg(view->screen()->cols()).arg(view->screen()->rows()));
                view->session()->write("echo EMSHELL_NATIVE_OK\r");
                sentCommand = true;
                log(QStringLiteral("  -> 已通过界面发送验证命令"));
            }

            if (sentCommand) {
                for (int i = 0; i < view->screen()->totalLines(); ++i) {
                    const QString t = view->screen()->lineText(i);
                    // 命令回显本身也含这个串，所以要求它单独出现才算数
                    if (t.trimmed() == QLatin1String("EMSHELL_NATIVE_OK"))
                        sawSuccess = true;
                }
            }
        }

        if (waitedMs > 24000)
            qApp->quit();
    });
    poll.start();

    QTimer::singleShot(26000, [] { qApp->quit(); });
    qApp->exec();
    poll.stop();

    // 把终端里画出来的东西导出来看
    if (TerminalView *view = terminal->findChild<TerminalView *>()) {
        TerminalScreen *screen = view->screen();
        QStringList lines;
        for (int i = 0; i < screen->totalLines(); ++i) {
            const QString t = screen->lineText(i);
            if (!t.isEmpty())
                lines << QStringLiteral("  | %1").arg(t);
        }
        log(QStringLiteral("  ---- 终端内容 ----"));
        for (const QString &l : lines)
            log(l);
    }

    check(sawSuccess, QStringLiteral("界面里连上并出现远端输出"));
    window.grab().save(QStringLiteral("uitrace.png"));

    flushLog();
    return g_fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 逐页截图：按侧边栏的顺序把每个页面切一遍，各存一张 PNG。
// 做界面的时候用，比一页页手动点开看快。
//   --uishots [宽 高]
// ---------------------------------------------------------------------------
int runUiShots(const QStringList &arguments)
{
    QFile::remove(QStringLiteral("selftest.log"));
    log(QStringLiteral("== 逐页截图 =="));

    int width = 1380;
    int height = 880;
    const int wIndex = arguments.indexOf(QStringLiteral("--uishots"));
    if (wIndex + 1 < arguments.size())
        width = arguments.at(wIndex + 1).toInt();
    if (wIndex + 2 < arguments.size())
        height = arguments.at(wIndex + 2).toInt();

    ConnectionStore store;
    store.load();
    KeyStore keys;
    keys.load();

    MainWindow window(&store, &keys);
    window.resize(width, height);
    window.show();

    // 找侧边栏上那排导航按钮。点它们而不是直接改 QStackedWidget 的索引，
    // 这样走的是真实路径（切页 + 高亮当前项都会发生）。
    QVector<NavButton *> navButtons;
    if (auto *bar = window.findChild<SideBar *>()) {
        for (NavButton *b : bar->findChildren<NavButton *>()) {
            if (b->isVisible())
                navButtons << b;
        }
    }

    log(QStringLiteral("  找到 %1 个导航项，窗口 %2x%3")
            .arg(navButtons.size()).arg(width).arg(height));

    if (navButtons.isEmpty()) {
        log(QStringLiteral("  FAIL: 没找到导航按钮"));
        flushLog();
        return 1;
    }

    int index = 0;
    QTimer step;
    step.setInterval(900);
    QObject::connect(&step, &QTimer::timeout, [&] {
        if (index >= navButtons.size()) {
            step.stop();
            qApp->quit();
            return;
        }

        NavButton *button = navButtons.at(index);
        // 导航项的文字画在子 QLabel 里，QPushButton::text() 是空的
        const QString name = button->labelText().trimmed();
        ++index;

        button->click();

        // 给页面一点时间把内容布局好再截
        QTimer::singleShot(450, &window, [&window, index, name] {
            const QString file = QStringLiteral("uipage%1_%2.png").arg(index).arg(name);
            if (window.grab().save(file))
                log(QStringLiteral("  已保存 %1").arg(file));
        });
    });
    step.start();

    QTimer::singleShot(20000, [] { qApp->quit(); });
    qApp->exec();

    flushLog();
    return 0;
}

// ---------------------------------------------------------------------------
// 更新功能：版本号解析、release JSON、挑附件、铺暂存目录。
//
// 单独一个 --updatetest，不塞进 --selftest：
//   - 那边钉着 32 项解析器断言，混进来会让「32」这个数失去意义；
//   - 这里会真写 %TEMP%、真调 tar.exe，失败原因和终端解析完全不是一类。
//
// 真正换文件那一步（launchSwap）**刻意不在这里跑** —— 自检就跑在本程序里，
// 换掉 exe 等于把自己删了。那份脚本由 tools/test-updater.py 拿桩程序验。
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 「检查更新」对话框的外观截图。喂一份假结果，不联网 ——
// 用来确认深色主题下 release notes、按钮和进度条的颜色是对的。
//   --updateui
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 「新建连接」对话框的外观截图。用真实的 ConnectionDialog，只是抓完就关。
//   --dialogui
// ---------------------------------------------------------------------------
int runConnectionDialogShot()
{
    QFile::remove(QStringLiteral("selftest.log"));
    log(QStringLiteral("== 新建连接对话框截图 =="));

    bool captured = false;

    QTimer::singleShot(0, [&captured] {
        // 等对话框自己布局完再抓
        QTimer::singleShot(700, [] {
            QWidget *modal = QApplication::activeModalWidget();
            if (!modal) {
                log(QStringLiteral("  FAIL: 没等到模态对话框"));
                qApp->quit();
                return;
            }
            if (auto *scroll = modal->findChild<QScrollArea *>()) {
                log(QStringLiteral("  对话框 %1x%2，视口高 %3，可滚动 %4 px")
                        .arg(modal->width()).arg(modal->height())
                        .arg(scroll->viewport()->height())
                        .arg(scroll->verticalScrollBar()->maximum()));
            }
            if (modal->grab().save(QStringLiteral("connectiondialog.png")))
                log(QStringLiteral("  已保存 connectiondialog.png"));
            if (auto *dialog = qobject_cast<QDialog *>(modal))
                dialog->reject();
        });

        ConnectionDialog::createNew(nullptr, nullptr, nullptr, nullptr);
        captured = true;
        qApp->quit();
    });

    QTimer::singleShot(8000, [] { qApp->quit(); });
    qApp->exec();

    check(captured, QStringLiteral("对话框正常打开并关闭"));
    flushLog();
    return g_fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 软件更新页截图。这一页没有交互分支，截一张就够。
//   --updatepageui
// ---------------------------------------------------------------------------
int runUpdatePageShot()
{
    QFile::remove(QStringLiteral("selftest.log"));
    log(QStringLiteral("== 软件更新页截图 =="));

    ConnectionStore store;
    store.load();
    KeyStore keys;
    keys.load();

    MainWindow window(&store, &keys);
    window.resize(1380, 880);
    window.show();

    if (auto *page = window.findChild<UpdatePage *>()) {
        if (auto *stack = window.findChild<QStackedWidget *>())
            stack->setCurrentWidget(page);
    }

    QTimer::singleShot(800, &window, [&window] {
        if (window.grab().save(QStringLiteral("updatepageui.png")))
            log(QStringLiteral("  已保存 updatepageui.png"));
        qApp->quit();
    });

    QTimer::singleShot(6000, [] { qApp->quit(); });
    qApp->exec();

    flushLog();
    return g_fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 设置页截图：基本设置 / 外观设置各一张，顺带验证「选项卡能切」。
//
// 背景图用一张现造的测试图，并通过 EMSHELL_BACKGROUND_IMAGE 临时覆盖 ——
// **不碰用户的 QSettings**，跑完删掉测试图、清掉环境变量。
//   --settingsui
// ---------------------------------------------------------------------------
int runSettingsShot()
{
    QFile::remove(QStringLiteral("selftest.log"));
    log(QStringLiteral("== 设置页截图 =="));

    const QString background = QDir::temp().filePath(QStringLiteral("emshell-test-background.png"));
    {
        QImage image(1600, 1000, QImage::Format_RGB32);
        QPainter painter(&image);
        QLinearGradient gradient(0, 0, image.width(), image.height());
        gradient.setColorAt(0.0, QColor(27, 42, 74));
        gradient.setColorAt(0.5, QColor(43, 75, 111));
        gradient.setColorAt(1.0, QColor(74, 43, 95));
        painter.fillRect(image.rect(), gradient);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        for (int i = 0; i < 12; ++i) {
            painter.setBrush(QColor(255, 255, 255, 12 + i * 3));
            painter.drawEllipse(QPoint(200 + i * 120, 150 + (i % 4) * 180),
                                60 + i * 4, 60 + i * 4);
        }
        painter.end();
        image.save(background);
    }

    qputenv("EMSHELL_BACKGROUND_IMAGE", background.toUtf8());
    qputenv("EMSHELL_BACKGROUND_DIM", "20");

    ConnectionStore store;
    store.load();
    KeyStore keys;
    keys.load();

    MainWindow window(&store, &keys);
    window.resize(1380, 880);
    window.show();

    if (auto *page = window.findChild<SettingsPage *>()) {
        if (auto *stack = window.findChild<QStackedWidget *>())
            stack->setCurrentWidget(page);
    }

    if (auto *bar = window.findChild<SideBar *>())
        log(QStringLiteral("  sidebar glass=%1").arg(bar->property("glass").toBool()));

    QTimer::singleShot(800, &window, [&window] {
        if (window.grab().save(QStringLiteral("settingsui_basic.png")))
            log(QStringLiteral("  已保存 settingsui_basic.png"));

        if (auto *tab = window.findChild<QPushButton *>(QStringLiteral("AppearanceTab")))
            tab->click();

        QTimer::singleShot(500, &window, [&window] {
            if (window.grab().save(QStringLiteral("settingsui_appearance.png")))
                log(QStringLiteral("  已保存 settingsui_appearance.png"));
            qApp->quit();
        });
    });

    QTimer::singleShot(6000, [] { qApp->quit(); });
    qApp->exec();

    QFile::remove(background);
    qunsetenv("EMSHELL_BACKGROUND_IMAGE");
    qunsetenv("EMSHELL_BACKGROUND_DIM");

    flushLog();
    return g_fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 连通性检测截图：等探测跑完（最多几秒），把窗口抓下来。
//   --probeui
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 终端页截图：开一个本地终端，等它画出来，抓窗口。
//   --termui
// 配合 EMSHELL_BACKGROUND_IMAGE / DIM 用，检查终端背景是不是真的透出了图。
// ---------------------------------------------------------------------------
int runTerminalShot()
{
    QFile::remove(QStringLiteral("selftest.log"));
    log(QStringLiteral("== 终端页截图 =="));

    ConnectionStore store;
    store.load();
    KeyStore keys;
    keys.load();

    MainWindow window(&store, &keys);
    window.resize(1280, 800);
    window.show();

    auto *terminal = window.findChild<TerminalPage *>();
    if (terminal) {
        if (auto *stack = window.findChild<QStackedWidget *>())
            stack->setCurrentWidget(terminal);
        QTimer::singleShot(500, terminal, [terminal] { terminal->openLocalSession(); });
    }

    QTimer::singleShot(3800, &window, [&window] {
        if (window.grab().save(QStringLiteral("termui.png")))
            log(QStringLiteral("  已保存 termui.png"));
        qApp->quit();
    });
    QTimer::singleShot(8000, [] { qApp->quit(); });
    qApp->exec();

    flushLog();
    return g_fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 终端标签栏截图：一次开几个标签，用来检查多标签的间距、滚动和关闭按钮位置。
//   --tabui [宽 高 [标签数]]
// 配合 EMSHELL_THEME=light|dark 用。
// ---------------------------------------------------------------------------
int runTabShot(int width, int height, int count)
{
    QFile::remove(QStringLiteral("selftest.log"));
    log(QStringLiteral("== 终端标签栏截图 =="));

    ConnectionStore store;
    store.load();
    KeyStore keys;
    keys.load();

    MainWindow window(&store, &keys);
    window.resize(width, height);
    window.show();

    auto *terminal = window.findChild<TerminalPage *>();
    if (terminal) {
        if (auto *stack = window.findChild<QStackedWidget *>())
            stack->setCurrentWidget(terminal);
        QTimer::singleShot(500, terminal, [terminal, count] {
            for (int i = 0; i < count; ++i)
                terminal->openLocalSession();
        });
        // 第一个标签换成很长的名字，检查宽度和关闭按钮还对不对
        QTimer::singleShot(900, &window, [&window] {
            if (auto *tabs = window.findChild<QTabWidget *>())
                tabs->setTabText(0, QStringLiteral("生产环境 - 数据库主节点"));
        });
        // 切回第一个，检查轨道上的「口子」有没有跟着选中项走
        QTimer::singleShot(3000, &window, [&window] {
            if (auto *tabs = window.findChild<QTabWidget *>())
                tabs->setCurrentIndex(0);
        });
    }

    QTimer::singleShot(4200, &window, [&window] {
        dumpUiGeometry(&window, QStringLiteral("selftest.log.tree"));
        log(QStringLiteral("---- 控件树 ----"));
        QFile f(QStringLiteral("selftest.log.tree"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            log(QString::fromUtf8(f.readAll()));
        if (window.grab().save(QStringLiteral("tabui.png")))
            log(QStringLiteral("  已保存 tabui.png"));
        qApp->quit();
    });
    QTimer::singleShot(9000, [] { qApp->quit(); });
    qApp->exec();

    flushLog();
    return g_fail == 0 ? 0 : 1;
}

int runProbeShot()
{
    QFile::remove(QStringLiteral("selftest.log"));
    log(QStringLiteral("== 连通性检测截图 =="));

    ConnectionStore store;
    store.load();
    KeyStore keys;
    keys.load();

    MainWindow window(&store, &keys);
    window.resize(1280, 800);
    window.show();

    // 13 条连接、并发 6、单条最多 2.5 秒 —— 8 秒足够跑完两批以上
    QTimer::singleShot(8000, &window, [&window] {
        if (window.grab().save(QStringLiteral("probeui.png")))
            log(QStringLiteral("  已保存 probeui.png"));
        qApp->quit();
    });
    QTimer::singleShot(12000, [] { qApp->quit(); });
    qApp->exec();

    flushLog();
    return g_fail == 0 ? 0 : 1;
}

int runUpdateDialogShot()
{
    QFile::remove(QStringLiteral("selftest.log"));
    log(QStringLiteral("== 检查更新对话框截图 =="));

    UpdateChecker::Result result;
    result.ok = true;
    result.updateAvailable = true;
    result.current = Version{{1, 0, 0, 0}};
    result.latest = Version{{1, 2, 0, 0}};
    result.release.tagName = QStringLiteral("v1.2.0");
    result.release.title = QStringLiteral("EmShell 1.2.0");
    result.release.notes = QStringLiteral(
        "## 更新内容\n\n"
        "- 新增分组颜色和侧栏排序\n"
        "- 终端支持 Ctrl + Shift + 滚轮缩放\n"
        "- 修复无边框窗口在 125% 缩放下的拖动偏移\n\n"
        "> 这是一个用于界面自检的示例说明。");
    result.release.pageUrl =
        QStringLiteral("https://github.com/ihehe1996/EmShell/releases/tag/v1.2.0");
    result.release.publishedAt = QDateTime::currentDateTime();

    ReleaseAsset asset;
    asset.name = QStringLiteral("EmShell-1.2.0-win64.zip");
    asset.downloadUrl = QStringLiteral("https://example.com/EmShell-1.2.0-win64.zip");
    asset.size = 8 * 1024 * 1024;
    result.release.assets.append(asset);

    UpdateDialog dialog;
    dialog.showResult(result);
    dialog.show();

    QTimer::singleShot(800, &dialog, [&dialog] {
        if (dialog.grab().save(QStringLiteral("updatedialog.png")))
            log(QStringLiteral("  已保存 updatedialog.png"));
        qApp->quit();
    });

    QTimer::singleShot(5000, [] { qApp->quit(); });
    qApp->exec();

    flushLog();
    return g_fail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 真联网查一次，用来确认 TLS / GitHub API / 版本比较这条链路是通的。
//   --updatecheck
// 它不下载、不替换任何文件，仓库还没发 Release 时也应该给出明确提示。
// ---------------------------------------------------------------------------
int runUpdateCheck()
{
    QFile::remove(QStringLiteral("selftest.log"));
    log(QStringLiteral("== 联网检查更新 =="));
    log(QStringLiteral("  更新服务：已配置"));

    UpdateChecker checker;
    bool finished = false;

    QObject::connect(&checker, &UpdateChecker::checkFinished, &checker,
                     [&](const UpdateChecker::Result &result) {
        finished = true;
        if (!result.ok) {
            log(QStringLiteral("  查询失败（这本身也是有效结果）：%1").arg(result.error));
        } else {
            log(QStringLiteral("  最新 %1，本机 %2，%3")
                    .arg(result.latest.toString(), result.current.toString(),
                         result.updateAvailable ? QStringLiteral("有新版本")
                                                : QStringLiteral("已是最新")));
        }
        qApp->quit();
    });

    QTimer::singleShot(0, &checker, [&checker] { checker.check(); });
    QTimer::singleShot(25000, [] { qApp->quit(); });
    qApp->exec();

    check(finished, QStringLiteral("检查请求有结果（成功或明确报错都算）"));
    flushLog();
    return g_fail == 0 ? 0 : 1;
}

int runUpdateTest()
{
    QFile::remove(QStringLiteral("selftest.log"));
    log(QStringLiteral("== 更新功能自检 =="));

    // ---- 版本号解析与比较 ----
    {
        const auto v = [](const char *text) {
            Version parsed;
            const bool ok = parseVersion(QString::fromLatin1(text), &parsed);
            return std::make_pair(ok, parsed.toString());
        };

        const auto withV = v("v1.2.3");
        const auto noV = v("1.2.3");
        const auto shortForm = v("1.2");
        check(withV.first && noV.first && shortForm.first
                  && withV.second == QStringLiteral("1.2.3")
                  && noV.second == QStringLiteral("1.2.3")
                  && shortForm.second == QStringLiteral("1.2.0"),
              QStringLiteral("版本：v 前缀被剥掉，缺的段按 0 补"),
              QStringLiteral("v1.2.3→%1  1.2.3→%2  1.2→%3")
                  .arg(withV.second, noV.second, shortForm.second));

        Version fourPart, threePart;
        const bool fourOk = parseVersion(QStringLiteral("8.9.8.1"), &fourPart);
        const bool threeOk = parseVersion(QStringLiteral("8.9.8"), &threePart);
        check(fourOk && threeOk && fourPart.toString() == QStringLiteral("8.9.8.1")
                  && threePart.toString() == QStringLiteral("8.9.8")
                  && isNewerVersion(fourPart, threePart),
              QStringLiteral("版本：四段式 tag 也认，且比三段式新"),
              QStringLiteral("8.9.8.1 显示为 %1，8.9.8 显示为 %2")
                  .arg(fourPart.toString(), threePart.toString()));

        Version rc, plain;
        const bool rcOk = parseVersion(QStringLiteral("0.2.0-rc1"), &rc);
        const bool plainOk = parseVersion(QStringLiteral("0.2.0"), &plain);
        check(rcOk && plainOk && compareVersions(rc, plain) == 0,
              QStringLiteral("版本：预发布后缀不影响比较（已知取舍）"),
              QStringLiteral("0.2.0-rc1 与 0.2.0 判为同一版本"));

        Version junk;
        check(!parseVersion(QStringLiteral("latest"), &junk)
                  && !parseVersion(QStringLiteral(""), &junk)
                  && !parseVersion(QStringLiteral("v"), &junk),
              QStringLiteral("版本：解析不出来的串被拒绝"));

        check(!isNewerVersion(plain, plain)
                  && isNewerVersion(plain, Version{{0, 1, 0, 0}})
                  && !isNewerVersion(Version{{0, 1, 0, 0}}, plain),
              QStringLiteral("版本：相等不算有更新"));

        // 编进来的版本号本身。发版时只改 CMakeLists 的 project(VERSION)，
        // 要是宏没传进来落到兜底值，更新器会拿 0.0.0-unknown 去比，
        // 表现是「永远查不出更新」，界面上一点异常都看不到。
        Version compiled;
        check(parseVersion(appVersion(), &compiled)
                  && !appVersion().contains(QStringLiteral("unknown")),
              QStringLiteral("版本：编译进来的版本号合法，没落到兜底值"),
              QStringLiteral("appVersion() = %1（来自 CMakeLists 的 project VERSION）")
                  .arg(appVersion()));
    }

    // ---- 解析 GitHub release 响应 ----
    {
        const QByteArray payload = QByteArrayLiteral(R"({
          "html_url": "https://github.com/example/EmShell/releases/tag/v8.9.8.1",
          "tag_name": "v8.9.8.1",
          "name": "EmShell release 8.9.8.1",
          "draft": false,
          "prerelease": false,
          "published_at": "2026-09-24T15:21:27Z",
          "message": "ignored: 有 tag_name 时不该被当成错误",
          "assets": [
            { "name": "checksums.sha256", "content_type": "application/octet-stream",
              "size": 1271, "digest": "sha256:78bc138EFF0675589b7887ed3114d30acdcd807f19f36b3ba4095b44173d1e36",
              "browser_download_url": "https://example.com/v8.9.8.1/checksums.sha256" },
            { "name": "EmShell-8.9.8.1.exe", "content_type": "application/x-msdownload",
              "size": 6632744, "digest": "sha256:a53c5116b55ac93fedd7808e71096badecbf10a55f2be9b28b741591b8917a73",
              "browser_download_url": "https://example.com/v8.9.8.1/EmShell-8.9.8.1.exe" },
            { "name": "EmShell-8.9.8.1.zip", "content_type": "application/x-zip-compressed",
              "size": 8090883,
              "browser_download_url": "https://example.com/v8.9.8.1/EmShell-8.9.8.1.zip" }
          ]
        })");

        ReleaseInfo info;
        QString parseError;
        const bool parsed = parseGitHubRelease(payload, &info, &parseError);

        check(parsed && info.tagName == QStringLiteral("v8.9.8.1") && info.assets.size() == 3,
              QStringLiteral("更新：能解析 GitHub 的 release 响应"),
              parsed ? QStringLiteral("tag=%1，附件 %2 个，发布页 %3")
                           .arg(info.tagName)
                           .arg(info.assets.size())
                           .arg(info.pageUrl.isEmpty() ? QStringLiteral("缺") : QStringLiteral("有"))
                     : parseError);

        const ReleaseAsset *picked = parsed ? pickUpdateAsset(info) : nullptr;
        check(picked && picked->name.endsWith(QStringLiteral(".zip")),
              QStringLiteral("更新：优先挑 .zip（它才能带上新增的依赖 DLL）"),
              picked ? QStringLiteral("挑中了 %1").arg(picked->name)
                     : QStringLiteral("一个都没挑中"));

        // 安装程序也是 .exe，被挑中的话会把用户的主程序换成安装程序本身
        {
            const QByteArray installerOnly = QByteArrayLiteral(R"({
              "tag_name": "v1.0.1",
              "html_url": "https://example.com",
              "assets": [
                { "name": "EmShell-1.0.1-setup.exe", "size": 3000000,
                  "browser_download_url": "https://example.com/EmShell-1.0.1-setup.exe" }
              ]
            })");

            ReleaseInfo onlyInstaller;
            QString ignoredError;
            const bool ok = parseGitHubRelease(installerOnly, &onlyInstaller, &ignoredError);
            const ReleaseAsset *pickedInstaller = ok ? pickUpdateAsset(onlyInstaller) : nullptr;

            check(ok && pickedInstaller == nullptr,
                  QStringLiteral("更新：只有安装程序时不挑它（挑错会把主程序换成安装程序）"),
                  ok ? (pickedInstaller ? QStringLiteral("居然挑中了 %1").arg(pickedInstaller->name)
                                        : QStringLiteral("正确跳过，宁可不更新"))
                     : ignoredError);

            const QByteArray both = QByteArrayLiteral(R"({
              "tag_name": "v1.0.1",
              "html_url": "https://example.com",
              "assets": [
                { "name": "EmShell-1.0.1-install.exe", "size": 3000000,
                  "browser_download_url": "https://example.com/EmShell-1.0.1-install.exe" },
                { "name": "EmShell-1.0.1.zip", "size": 8000000,
                  "browser_download_url": "https://example.com/EmShell-1.0.1.zip" }
              ]
            })");

            ReleaseInfo bothInfo;
            const bool bothOk = parseGitHubRelease(both, &bothInfo, &ignoredError);
            const ReleaseAsset *pickedBoth = bothOk ? pickUpdateAsset(bothInfo) : nullptr;

            check(bothOk && pickedBoth
                      && pickedBoth->name == QStringLiteral("EmShell-1.0.1.zip"),
                  QStringLiteral("更新：便携 zip 和安装程序同时在时，挑 zip"),
                  pickedBoth ? QStringLiteral("挑中了 %1").arg(pickedBoth->name)
                             : QStringLiteral("一个都没挑中"));
        }

        const ReleaseAsset *first = parsed && !info.assets.isEmpty() ? &info.assets.first() : nullptr;
        check(first && first->sha256 == QStringLiteral(
                           "78bc138eff0675589b7887ed3114d30acdcd807f19f36b3ba4095b44173d1e36"),
              QStringLiteral("更新：digest 里的 sha256 前缀被剥掉并统一成小写"),
              first ? (first->sha256.isEmpty() ? QStringLiteral("没解析出来")
                                               : first->sha256.left(16) + QStringLiteral("…"))
                    : QStringLiteral("没有附件"));

        ReleaseInfo ignored;
        QString notFoundError;
        check(!parseGitHubRelease(QByteArrayLiteral(R"({"message":"Not Found"})"),
                                  &ignored, &notFoundError)
                  && notFoundError.contains(QStringLiteral("Not Found")),
              QStringLiteral("更新：仓库不存在的响应被当成错误，而不是「没有新版本」"),
              notFoundError);

        check(!parseGitHubRelease(QByteArrayLiteral("<html>502 Bad Gateway</html>"),
                                  &ignored, &notFoundError),
              QStringLiteral("更新：非 JSON 响应被拒绝"),
              notFoundError);

        // 404 的两种可能对应完全不同的处理办法，提示必须分得开
        {
            const QString repo = QStringLiteral("someone/EmShell");
            const QString missing = repoNotFoundMessage(repo);
            const QString noRelease = noReleaseYetMessage(repo);

            check(!missing.isEmpty() && !noRelease.isEmpty() && missing != noRelease
                      && !missing.contains(repo) && !noRelease.contains(repo)
                      && !missing.contains(QStringLiteral("updateRepo"))
                      && !noRelease.contains(QStringLiteral("GitHub")),
                  QStringLiteral("更新：404 的两种情况给出不同的提示，且不暴露更新源"));
        }
    }

    // ---- 铺暂存目录（纯文件操作，不联网也不动真正的 exe）----
    {
        const QString sandbox =
            QDir::temp().filePath(QStringLiteral("EmShell-selftest-stage"));
        QDir(sandbox).removeRecursively();
        QDir().mkpath(sandbox);

        const auto writeFake = [](const QString &path, const QByteArray &head) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            file.write(head);
            file.write(QByteArray(64, '\0'));
            return true;
        };

        // 下载下来的文件必须落在 workDirFor(版本) 里面 —— 和真的更新流程一模一样。
        // 测的摆放方式一旦和产品不一样，某些错就永远测不出来。
        const auto downloadTo = [](const QString &version, const QString &name,
                                   const QByteArray &head) {
            const QString dir = UpdateInstaller::workDirFor(version);
            QDir().mkpath(dir);
            const QString path = QDir(dir).filePath(name);
            QFile file(path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write(head);
                file.write(QByteArray(64, '\0'));
            }
            return path;
        };

        // --- 分支一：下载到的是个裸 exe ---
        const QString bareExe =
            downloadTo(QStringLiteral("9.9.9-bare"), QStringLiteral("EmShell.exe"),
                       QByteArrayLiteral("MZ"));

        UpdateInstaller::Prepared prepared;
        QString stageError;
        const bool bareOk = UpdateInstaller::stage(bareExe, QStringLiteral("9.9.9-bare"),
                                                   &prepared, &stageError);
        check(bareOk && QFileInfo::exists(
                            QDir(prepared.stageDir).filePath(QStringLiteral("EmShell.exe"))),
              QStringLiteral("更新：裸 exe 能被铺进暂存目录并通过 PE 校验"),
              bareOk ? QStringLiteral("暂存目录 %1").arg(QDir::toNativeSeparators(prepared.stageDir))
                     : stageError);

        // --- 分支二：下载到的是个 zip（用产品同一套 tar 解析来打包）---
        const QString zipSrc = QDir(sandbox).filePath(QStringLiteral("zipsrc"));
        QDir().mkpath(zipSrc);
        writeFake(QDir(zipSrc).filePath(QStringLiteral("EmShell.exe")), QByteArrayLiteral("MZ"));
        writeFake(QDir(zipSrc).filePath(QStringLiteral("Qt6Network.dll")), QByteArrayLiteral("MZ"));

        const QString zipPath =
            downloadTo(QStringLiteral("9.9.9-zip"), QStringLiteral("EmShell-9.9.9.zip"),
                       QByteArray());
        QProcess tar;
        tar.setProgram(UpdateInstaller::systemToolPath(QStringLiteral("tar.exe")));
        tar.setWorkingDirectory(zipSrc);
        tar.setArguments({QStringLiteral("-a"), QStringLiteral("-cf"),
                          QFileInfo(zipPath).absoluteFilePath(),
                          QStringLiteral("EmShell.exe"), QStringLiteral("Qt6Network.dll")});
        tar.start();
        const bool tarOk = tar.waitForFinished(60000) && tar.exitCode() == 0;

        UpdateInstaller::Prepared zipPrepared;
        const bool zipOk = tarOk
                           && UpdateInstaller::stage(zipPath, QStringLiteral("9.9.9-zip"),
                                                     &zipPrepared, &stageError);
        check(zipOk && QFileInfo::exists(
                           QDir(zipPrepared.stageDir).filePath(QStringLiteral("EmShell.exe")))
                  && QFileInfo::exists(
                           QDir(zipPrepared.stageDir).filePath(QStringLiteral("Qt6Network.dll"))),
              QStringLiteral("更新：zip 能解开并定位到主程序（顺带验证 tar 可用）"),
              zipOk ? QStringLiteral("主程序和依赖 DLL 都在 %1")
                          .arg(QDir::toNativeSeparators(zipPrepared.stageDir))
                    : (tarOk ? stageError : QStringLiteral("打包测试 zip 就失败了")));

        // --- 反面：不是可执行文件的「exe」必须被挡下来 ---
        const QString htmlFile =
            downloadTo(QStringLiteral("9.9.9-bad"), QStringLiteral("EmShell.exe"),
                       QByteArrayLiteral("<html>"));

        UpdateInstaller::Prepared badPrepared;
        const bool badRejected = !UpdateInstaller::stage(htmlFile, QStringLiteral("9.9.9-bad"),
                                                        &badPrepared, &stageError);
        check(badRejected,
              QStringLiteral("更新：开头不是 MZ 的文件被拒绝，不会拿去替换程序"),
              badRejected ? QStringLiteral("已拒绝") : QStringLiteral("居然通过了"));

        // --- 反面：zip 里没有主程序 ---
        const QString emptyZip =
            downloadTo(QStringLiteral("9.9.9-noexe"), QStringLiteral("EmShell-empty.zip"),
                       QByteArray());
        QProcess tar2;
        tar2.setProgram(UpdateInstaller::systemToolPath(QStringLiteral("tar.exe")));
        tar2.setWorkingDirectory(zipSrc);
        tar2.setArguments({QStringLiteral("-a"), QStringLiteral("-cf"),
                           QFileInfo(emptyZip).absoluteFilePath(),
                           QStringLiteral("Qt6Network.dll")});
        tar2.start();
        tar2.waitForFinished(60000);

        UpdateInstaller::Prepared noExePrepared;
        const bool noExeRejected = !UpdateInstaller::stage(emptyZip, QStringLiteral("9.9.9-noexe"),
                                                          &noExePrepared, &stageError);
        check(noExeRejected,
              QStringLiteral("更新：包里没有主程序时报错，不瞎猜一个 exe 顶上"),
              noExeRejected ? QStringLiteral("已拒绝") : QStringLiteral("居然通过了"));

        // 自检造出来的工作目录要自己收干净，别在用户的 %TEMP% 里留东西
        for (const char *version : {"9.9.9-bare", "9.9.9-zip", "9.9.9-bad", "9.9.9-noexe"})
            QDir(UpdateInstaller::workDirFor(QString::fromLatin1(version))).removeRecursively();
        QDir(sandbox).removeRecursively();
    }

    log(QStringLiteral("  换文件那一步不在自检里跑（会替换本程序），"
                       "由 tools/test-updater.py 用桩程序单独验"));

    flushLog();
    return g_fail == 0 ? 0 : 1;
}

} // namespace

bool isSelfTestRequest(const QStringList &arguments)
{
    return arguments.contains(QStringLiteral("--selftest"))
           || arguments.contains(QStringLiteral("--updatetest"))
           || arguments.contains(QStringLiteral("--updateui"))
           || arguments.contains(QStringLiteral("--dialogui"))
           || arguments.contains(QStringLiteral("--settingsui"))
           || arguments.contains(QStringLiteral("--updatepageui"))
           || arguments.contains(QStringLiteral("--probeui"))
           || arguments.contains(QStringLiteral("--termui"))
           || arguments.contains(QStringLiteral("--tabui"))
           || arguments.contains(QStringLiteral("--updatecheck"))
           || arguments.contains(QStringLiteral("--ptytest"))
           || arguments.contains(QStringLiteral("--sshtest"))
           || arguments.contains(QStringLiteral("--sshnative"))
           || arguments.contains(QStringLiteral("--sshgui"))
           || arguments.contains(QStringLiteral("--uishots"))
           || arguments.contains(QStringLiteral("--uidump"));
}

int runSelfTest(const QStringList &arguments)
{
    if (arguments.contains(QStringLiteral("--termui")))
        return runTerminalShot();
    if (arguments.contains(QStringLiteral("--tabui"))) {
        const int i = arguments.indexOf(QStringLiteral("--tabui"));
        const int w = (i + 1 < arguments.size()) ? arguments.at(i + 1).toInt() : 1280;
        const int h = (i + 2 < arguments.size()) ? arguments.at(i + 2).toInt() : 800;
        const int n = (i + 3 < arguments.size()) ? arguments.at(i + 3).toInt() : 4;
        // 允许传 0：不开标签，检查空状态（顺带确认左侧补条没有露出来）
        return runTabShot(w > 0 ? w : 1280, h > 0 ? h : 800, n >= 0 ? n : 4);
    }
    if (arguments.contains(QStringLiteral("--probeui")))
        return runProbeShot();
    if (arguments.contains(QStringLiteral("--settingsui")))
        return runSettingsShot();
    if (arguments.contains(QStringLiteral("--updatepageui")))
        return runUpdatePageShot();
    if (arguments.contains(QStringLiteral("--dialogui")))
        return runConnectionDialogShot();
    if (arguments.contains(QStringLiteral("--updateui")))
        return runUpdateDialogShot();
    if (arguments.contains(QStringLiteral("--updatecheck")))
        return runUpdateCheck();
    if (arguments.contains(QStringLiteral("--updatetest")))
        return runUpdateTest();
    if (arguments.contains(QStringLiteral("--uishots")))
        return runUiShots(arguments);
    if (arguments.contains(QStringLiteral("--uidump")))
        return runUiDump(arguments);
    if (arguments.contains(QStringLiteral("--sshgui")))
        return runUiSshTest(arguments);
    if (arguments.contains(QStringLiteral("--sshnative")))
        return runSshNativeTest(arguments);
    if (arguments.contains(QStringLiteral("--sshtest")))
        return runSshTest(arguments);
    if (arguments.contains(QStringLiteral("--ptytest")))
        return runPtyTest();
    return runParserTest();
}

} // namespace Em
