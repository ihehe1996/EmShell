#pragma once

#include "model/Connection.h"

#include <QString>

namespace Em {
namespace RdpLauncher {

// 生成临时 .rdp 文件并拉起系统自带的 mstsc。
//
// 远程桌面的画面在 mstsc 自己的窗口里，EmShell 这边**不建标签、不跟踪进程**：
// 关掉 mstsc 或关掉 EmShell 互不影响。这里只负责把连接参数和密码交给它。
//
// 返回 false 表示启动失败（原因写进 errorOut）；warningOut 用于「开起来了，
// 但密码没能写进去」这类不至于失败的问题 —— 此时 mstsc 会自己弹密码框。
bool launch(const Connection &connection, QString *errorOut = nullptr,
            QString *warningOut = nullptr);

// 自检用，不启动 mstsc：
//   把密码加密成 password 51:b 后面那串十六进制
bool protectPassword(const QString &password, QString *hex, QString *errorOut);
//   生成 .rdp 文本（不含 BOM）。protectedPassword 传空表示不写密码行。
QString buildRdpFileContent(const Connection &connection,
                            const QString &protectedPassword);

} // namespace RdpLauncher
} // namespace Em
