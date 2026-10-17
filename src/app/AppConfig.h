#pragma once

#include <QString>

namespace Em {

// 当前版本号，形如 "1.0.0"。
//
// **不要在代码里硬编码它** —— 它是 CMakeLists.txt 里 project(... VERSION ...)
// 生成的编译期宏。发新版只改那一行，这个值和 exe 里的版本、以及 Release 的 tag
// 三者天然一致。更新器会拿它去和 GitHub release 的 tag 比，写岔了的表现是
// 「有新版却永远查不出来」，界面上一点异常都看不到。
//
// 这里的兜底只是为了「万一没过 CMake」时不至于编不过；真出现 0.0.0-unknown
// 说明构建配置被绕过去了，自检里有一条专门盯这个。
#ifndef EMSHELL_VERSION
#  define EMSHELL_VERSION "0.0.0-unknown"
#endif

inline QString appVersion() { return QString::fromLatin1(EMSHELL_VERSION); }

// 更新源：GitHub 上的「用户名/仓库名」。
//
// 更新走的是 https://api.github.com/repos/{它}/releases/latest，不需要自建服务器。
// 留空的话，「检查更新」会明确告诉你还没配好，而不是假装查过了。
//
// 发 Release 时的约定（两条，都影响能不能升级成功）：
//   1. tag 写成版本号，v 前缀随便（"v1.0.1" 和 "1.0.1" 都认）
//   2. 挂一个附件：优先 .zip（可以整包带上依赖的 DLL），其次是单个 .exe。
//      只挂裸 exe 时，新版本新增的 Qt DLL（比如这次的 Qt6Network.dll）
//      老用户目录里没有，更新完会起不来。
inline QString updateRepo() { return QStringLiteral("ihehe1996/EmShell"); }

// 启动之后隔多久去悄悄查一次，毫秒。
// 等一会儿是为了别和窗口初始化抢时间，也不至于让冷启动更慢。
inline int updateStartupDelayMs() { return 5000; }

} // namespace Em
