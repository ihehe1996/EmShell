# EmShell 开发文档

面向修改代码的人。普通用户请阅读 [README.md](README.md)。

## 构建

环境：Qt 6.8.3（MinGW 64 位）、CMake 3.21+、Ninja、MinGW 13。

```bash
cd /d/QT/EmShell      # 构建前先回项目根目录
export PATH="/d/Program Files/QT/6.8.3/mingw_64/bin:/d/Program Files/QT/Tools/mingw1310_64/bin:/d/Program Files/QT/Tools/CMake_64/bin:/d/Program Files/QT/Tools/Ninja:$PATH"

cmake --preset mingw-debug       # 只在 CMakeLists 改动后需要
cmake --build --preset mingw-debug
```

产物是 `build/debug/EmShell.exe`。构建前先关掉正在运行的 EmShell，否则 exe 被占用会链接失败。

直接双击需要 Qt DLL，可以在产物目录跑一次：

```bash
"D:/Program Files/QT/6.8.3/mingw_64/bin/windeployqt.exe" build/debug/EmShell.exe
```

`third_party/libssh2/` 是已经编译好的静态库，正常不需要重建；换版本见 `third_party/README.md`。

## 架构

分层清晰，每层只依赖下面一层：

```
model/      连接、分组、历史、密钥的数据与持久化（JSON + QSettings）
  ↑
terminal/   TerminalSession（抽象）→ LocalSession（ConPTY）
ssh/        TerminalSession（抽象）→ SshSession（libssh2）
update/     检查 GitHub Release、下载校验、替换文件
  ↑
ui/         页面与控件，只跟 TerminalSession 打交道
```

- `terminal/TerminalSession.h` 是本地 shell 和 SSH 的共同接口，`TerminalView` 完全不知道底下是什么会话。
- `TerminalScreen` 是纯逻辑的 VT/xterm 解析器，不依赖 Widget；`feed()` 是增量的，转义序列和 UTF-8 多字节字符被切断在两次调用之间也能接上。
- `SshSession` 跑在独立工作线程：主线程通过待发队列 / 原子变量 / 信号通信；握手和认证用阻塞模式，建好 shell 后切非阻塞 + `select()` 轮询。主机密钥确认需要主线程弹窗时，用「发信号 + 条件变量等」同步一次，**`emit` 必须在锁外**。
- 列表里的状态和延迟由 `ui/ConnectionProbe` 对 `host:port` 做异步 TCP 探测，最多 6 并发、单个 2.5 秒超时，结果缓存 60 秒。它只代表端口可达，不代表 SSH 认证能通过。
- 终端的右键菜单在 `TerminalView::contextMenuEvent`，复用已有的 `copySelection()` / `pasteFromClipboard()` / `selectAll()`；快捷键是 Ctrl+Shift+C / Ctrl+V（或 Ctrl+Shift+V）/ Ctrl+Shift+A（Ctrl+C 要留给终端发信号）。
- **单行粘贴不要用括号粘贴（bracketed paste）**：bash/readline 会把括号里的内容用反色高亮，中文是宽字符，连起来就是一条白条，直到回车才消失。`pasteFromClipboard()` 只在文本包含换行时才加 `\x1b[200~...\x1b[201~`，单行直接发送。
- 系统托盘在 `MainWindow::createTrayIcon()`。`main.cpp` 设了 `setQuitOnLastWindowClosed(false)`，`closeEvent` 在没有「退出」请求时把窗口 `hide()` 掉，真正退出只走托盘菜单。没有托盘的环境下 `closeEvent` 会自己调 `quit()`，不会留下一个没有窗口的进程。

### 版本号

版本号只有一个来源：`CMakeLists.txt` 的 `project(EmShell VERSION ...)`，通过编译期宏 `EMSHELL_VERSION` 交给 `src/app/AppConfig.h` 的 `appVersion()`。窗口标题、设置页和更新器都读它，**不要在任何地方再写一遍**。

## 自动更新与发版

侧栏「工具 → 软件更新」可以手动检查；程序启动 5 秒后也会静默检查一次，只有发现新版本才弹窗。

更新源在 `src/app/AppConfig.h` 的 `updateRepo()`，走 GitHub 的 `releases/latest`，不需要自建服务器。

发版步骤：

1. 修改 `CMakeLists.txt` 的 `project(EmShell VERSION x.y.z)`
2. 运行 `tools/build-installer.ps1`。它会编译 release、windeployqt、做一次脱离 Qt 环境的自检、用 Inno Setup 打安装程序，并用系统 tar 打便携 zip。
3. 在 GitHub 创建 Release，tag 用 `vx.y.z`，把两个产物都挂成附件：
   - `dist/EmShell-vx.y.z-install.exe`
   - `dist/EmShell-x.y.z-win64.zip`

安装程序需要 Inno Setup 6：`winget install JRSoftware.InnoSetup`。脚本会从 `CMakeCache.txt` 里问出 Qt 和 MinGW 的路径，不需要再写死。

**更新器只会挑 zip。** 安装程序的文件名里带 `install`，`pickUpdateAsset()` 会跳过它；否则会把用户的主程序替换成安装程序本身。zip 是整包铺到安装目录的，必须包含 exe 和所有 Qt DLL。

安装脚本 `installer/EmShell.iss` 里有两条不能改坏的设定：

- `AppId` 是程序在 Windows 里的身份，发出去之后不要再改，否则覆盖安装会变成两份
- `[Dirs] Permissions: users-modify` 让安装目录可写，这样装到 `Program Files` 之后自动更新也不需要提权

只打便携 zip 可以用 `tools/package-release.ps1`。

替换正在运行的程序由 `resources/apply-update.ps1` 完成：退出程序后备份旧文件、覆盖新文件、启动新版本并确认存活；失败会回滚并重新启动旧版本。临时目录和日志留在 `%TEMP%\EmShellUpdate-<版本>`。

## 自检

程序是 `-mwindows` 构建，没有控制台，结果写到当前工作目录的 `selftest.log`。

```bash
cd build/debug

./EmShell.exe --selftest          # VT 解析器断言，32 项
./EmShell.exe --updatetest        # 更新：版本比较 / release 解析 / 铺暂存目录
./EmShell.exe --updatecheck       # 更新：真联网问一次 GitHub
./EmShell.exe --updateui          # 更新对话框截图
./EmShell.exe --dialogui          # 新建连接对话框截图
./EmShell.exe --settingsui        # 设置页截图
./EmShell.exe --probeui           # 连通性检测截图
./EmShell.exe --termui            # 终端页截图
./EmShell.exe --ptytest           # 本地 ConPTY 会话（默认 shell）
EMSHELL_PTY_SHELL='C:/WINDOWS/system32/cmd.exe' ./EmShell.exe --ptytest   # 指定 shell
./EmShell.exe --uidump 1380 880   # 打印控件树几何

EMSHELL_SSH_PASSWORD='密码' ./EmShell.exe --sshtest  user@host 22
EMSHELL_SSH_PASSWORD='密码' ./EmShell.exe --sshnative user@host 22
EMSHELL_SSH_PASSWORD='密码' ./EmShell.exe --sshgui   user@host 22
```

自更新的「换文件」不开在自检里（会替换本程序），由桩程序单独验：

```bash
python tools/test-updater.py
```

调界面时可以设置 `EMSHELL_UI_TRACE=1`，3 秒后写 `uitrace.log` 和 `uitrace.png`。

## 界面约定

- 颜色只从 `Theme::color("token")` 取；QSS 里写 `@token`，加载时替换成当前配色。不要写死色值。
- 深色是默认配色，浅色也保留。加配色只需在 `src/app/Theme.cpp` 的 `paletteEntries()` 加一行。
- QSS 的类选择器匹配不到自定义类，统一用属性选择器（`QFrame[role="card"]`）；动态属性改了要 `repolish()`。
- 纯 `QWidget` 上 QSS 的 `background` 不生效，必须 `setAttribute(Qt::WA_StyledBackground, true)`。
- 窄容器里的文字用 `ElidedLabel`，普通 `QLabel` 的最小宽度会把容器撑破。
- 背景图画在 `MainWindow` 的 `BackgroundHost` 里，Root 本身就是宿主；页面保持透明，各种表面用带 alpha 的 token，保证背景图不会被某一块彻底盖住。终端背景是全透明的（`term-bg` alpha 为 0），`TerminalView` 因此不能开 `WA_OpaquePaintEvent`。
- 表格（连接列表、最近使用）的表头和行共用同一组列宽 stretch 常量；所有可拉伸列的横向 `sizeHint` / `minimumSizeHint` 都压成 0（`ColumnLabel` / `ColumnCell` / `ColumnElidedLabel`），否则表头和行会错列。表头右侧的占位必须是固定宽度 QWidget，不能用 `addSpacing()`。

## 几个容易踩的坑

1. **ConPTY 必须显式设置 `STARTF_USESTDHANDLES` + NULL**，否则子进程继承父进程的 stdio：stdin 立刻 EOF，输出也读不到。`LocalSession::start()` 里还有一句看似多余的 `ResizePseudoConsole()`，不能删——ConPTY 在第一次 resize 前可能一直不往管道写。
2. **ConPTY 声明被宏挡着**：`NTDDI_VERSION >= 0x0A000006`。相关宏要放在 CMake 的编译选项里，不能等头文件先引入 `windows.h`。
3. **无边框窗口的拖动/缩放交给系统**：在 `MainWindow::nativeEvent` 里应答 `WM_NCHITTEST`，边缘返回 `HTLEFT` 等，标题栏区域返回 `HTCAPTION`，按钮区域返回 `HTCLIENT`。注意 lParam 是物理像素，125% 缩放下要用 `devicePixelRatioF()` 换算。
4. **HBox 里的 QScrollArea 会把邻居挤出去**：它的 sizeHint 跟着内部控件走，`setMinimumWidth()` 压不住，要把横向 size policy 设成 `Ignored`。
5. **libssh2 的三个坑**：
   - `set_blocking()` 不会把 socket 设成非阻塞，要自己 `ioctlsocket(FIONBIO)`，否则工作线程卡死、程序关不掉；
   - `libssh2_knownhost_checkp()` 的 typemask 必须带上密钥类型，否则会误报密钥不一致；
   - 持锁时不要 `emit`，否则槽函数回调同一把锁会死锁。
6. **QSS 的 `min-height` 会废掉 `setFixedSize()`**：最小尺寸大于最大尺寸时 Qt 会清掉最大尺寸，方形按钮变长方形，代码看起来却完全正确。
7. **终端渲染按 run 合并绘制**：属性相同的连续窄字符合成一次 `drawText`；全角字符单独画并用 baseline 定位，避免字体混排时行距跳动。
8. **`QStandardPaths::findExecutable()` 返回正斜杠路径，cmd.exe 不认。**
   把 `C:/WINDOWS/system32/cmd.exe` 原样交给 `CreateProcessW`（且 `lpApplicationName` 为空）时，cmd 会解析错，启动后打印「子目录或文件 .exe 已经存在。」并且不进入提示符。PowerShell 恰好不炸，所以只在选了 cmd 时出现。
   `LocalSession::start()` 现在对真实可执行文件路径做三件事：转成 `QDir::toNativeSeparators()`、加引号、把 `lpApplicationName` 明确传进去；像退回系统 ssh 客户端的 `ssh -p ...` 这种命令串则保持原样。

## 安全说明（实现层面）

- 连接密码和密钥口令写入 `connections.json` 前使用 Windows DPAPI（`CryptProtectData`）加密，密文绑定当前 Windows 账户。换机器 / 换账户后解不出来，`Secret::unprotect()` 返回空串，需要重新输入。
- 连接密码在内存里仍是明文；终端内容和命令历史没有单独加密；没有主密码机制；配置文件本身没有完整性校验。
- 主机密钥会与 `~/.ssh/known_hosts` 校验：首次连接需要确认指纹，对不上直接中止。

## 下一步

1. SFTP 文件传输（libssh2 的 SFTP 接口已经链进来）
2. 端口转发（本地 / 远端 / 动态）
3. SSH 认证级检测（当前状态检测只到 TCP 端口）
4. 让 libssh2 的主机密钥协商优先 ed25519，减少和系统 ssh 的指纹重复确认
5. 会话录制与回放
6. 从 PuTTY / Xshell / `~/.ssh/config` 导入连接
7. 终端内搜索
