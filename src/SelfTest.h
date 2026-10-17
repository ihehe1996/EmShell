#pragma once

#include <QStringList>

class QWidget;

namespace Em {

// 命令行自检。带 --selftest 时跑终端解析器断言，带 --ptytest 时真起一个
// ConPTY 会话验证本地终端链路。
//
//   EmShell.exe --selftest                     只验 VT 解析器，秒出结果
//   EmShell.exe --ptytest                      真开一个 shell，往里敲条命令再读回来
//   EmShell.exe --sshtest 用户@主机 [端口]      真连一台服务器走一遍完整流程
//
// SSH 测试的密码从环境变量 EMSHELL_SSH_PASSWORD 读，不写进任何文件。
//
// 因为程序是 -mwindows 构建的，没有控制台，结果会写到当前目录的 selftest.log。
bool isSelfTestRequest(const QStringList &arguments);
int  runSelfTest(const QStringList &arguments);

// 把某个窗口的控件树几何写进文件。用于排查布局问题：
// 在 GUI 正常运行里也能调（设了 EMSHELL_UI_TRACE 环境变量时），
// 这样「截图看到的」和「量出来的」就是同一个进程，不会互相打架。
void dumpUiGeometry(QWidget *root, const QString &path);

// 设了 EMSHELL_UI_TRACE 就装一个定时器，几秒后把几何写到 uitrace.log
void installUiTracerIfRequested(QWidget *root);

} // namespace Em
