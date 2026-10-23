cmake_minimum_required(VERSION 3.21)

project(EmShell VERSION 1.0.6 LANGUAGES C CXX)

# Windows 资源脚本（exe 的图标）需要 RC 编译器（MinGW 下是 windres）
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 让 CMake 自动处理 Qt 的 moc / rcc / uic
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Svg 用来渲染图标，Network 是「检查更新」用的：拉 GitHub 的 release JSON、
# 下载附件（SSH 用的是 libssh2，不走它）。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Network)

qt_standard_project_setup()

# 程序图标：resources/app.rc 编译进 PE 的资源段。
#
# 单独建一个 OBJECT 库，别把 .rc 直接塞进主目标：主目标带一堆 Qt 的 -I 路径，
# 其中 `D:/Program Files/...` 带空格；windres 调 cpp 时不会给这些路径加引号，
# 会报 `'D:\Program' 不是内部或外部命令`。这个对象库不链接 Qt、也不开
# AUTOMOC/AUTOUIC/AUTORCC，编译规则里没有那些 -I。
if(WIN32)
    enable_language(RC)
    add_library(emshell_icon OBJECT resources/app.rc)
    set_target_properties(emshell_icon PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF)
endif()

qt_add_executable(EmShell WIN32
    src/main.cpp

    src/app/AppConfig.h
    src/app/Theme.h
    src/app/Theme.cpp
    # 配色：一个文件一套（Palette.h 里是登记机制，Theme.cpp 只管取色）
    src/app/palettes/Palette.h
    src/app/palettes/PaletteRegistry.cpp
    src/app/palettes/DarkPalette.cpp
    src/app/palettes/LightPalette.cpp
    src/app/palettes/RedPalette.cpp
    src/app/palettes/GreenPalette.cpp

    src/model/Connection.h
    src/model/Connection.cpp
    src/model/ConnectionStore.h
    src/model/ConnectionStore.cpp
    src/model/ConfigTransfer.h
    src/model/ConfigTransfer.cpp
    src/model/Secret.h
    src/model/Secret.cpp
    src/model/KeyStore.h
    src/model/KeyStore.cpp

    src/terminal/TerminalSession.h
    src/terminal/TerminalSession.cpp
    src/terminal/LocalSession.h
    src/terminal/LocalSession.cpp
    src/terminal/TerminalScreen.h
    src/terminal/TerminalScreen.cpp
    src/terminal/TerminalView.h
    src/terminal/TerminalView.cpp

    src/ssh/SshSession.h
    src/ssh/SshSession.cpp

    src/update/Version.h
    src/update/ReleaseInfo.h
    src/update/ReleaseInfo.cpp
    src/update/UpdateChecker.h
    src/update/UpdateChecker.cpp
    src/update/UpdateInstaller.h
    src/update/UpdateInstaller.cpp

    src/SelfTest.h
    src/SelfTest.cpp

    src/ui/IconProvider.h
    src/ui/IconProvider.cpp
    src/ui/ConnectionProbe.h
    src/ui/ConnectionProbe.cpp
    src/ui/Widgets.h
    src/ui/Widgets.cpp
    src/ui/TitleBar.h
    src/ui/TitleBar.cpp
    src/ui/SideBar.h
    src/ui/SideBar.cpp
    src/ui/ConnectionsPage.h
    src/ui/ConnectionsPage.cpp
    src/ui/ConnectionFormPanel.h
    src/ui/ConnectionFormPanel.cpp
    src/ui/ConnectionDialog.h
    src/ui/ConnectionDialog.cpp
    src/ui/Drawer.h
    src/ui/Drawer.cpp
    src/ui/GroupDialog.h
    src/ui/GroupDialog.cpp
    src/ui/HistoryPage.h
    src/ui/HistoryPage.cpp
    src/ui/KeysPage.h
    src/ui/KeysPage.cpp
    src/ui/LinkDialog.h
    src/ui/LinkDialog.cpp
    src/ui/SettingsPage.h
    src/ui/SettingsPage.cpp
    src/ui/UpdatePage.h
    src/ui/UpdatePage.cpp
    src/ui/UpdateDialog.h
    src/ui/UpdateDialog.cpp
    src/ui/TerminalPage.h
    src/ui/TerminalPage.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp

    resources/resources.qrc
)

target_include_directories(EmShell PRIVATE src)

# 把版本号以编译期宏的形式交给 C++ 那边（见 src/app/AppConfig.h 的 appVersion()）。
#
# 为什么要绕这一下：更新器要拿 exe 里的版本去和 GitHub release 的 tag 比。
# 版本号在 main.cpp 里再写一遍必然会漂，而漂了的表现不是显示难看，是
# 「有新版却永远查不出来」或者「更新完还一直提示同一个版本」。
target_compile_definitions(EmShell PRIVATE EMSHELL_VERSION="${PROJECT_VERSION}")

# 打包脚本（tools/package-release.ps1）要拿版本号给 zip 命名，从这儿读。
# 让 CMake 落一个文件，而不是让脚本去解析 CMakeLists —— 解析文本很脆。
file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${PROJECT_VERSION}\n")

# ---------------------------------------------------------------------------
# libssh2：原生 SSH 会话用。
#
# 第三方库不走系统安装，直接放在 third_party/libssh2/ 里静态链接，
# 这样项目自带依赖，换台机器 clone 下来也能编（用 third_party/build-libssh2.sh
# 重建）。编译时用的是 WinCNG 后端，所以不需要 OpenSSL。
# ---------------------------------------------------------------------------
set(LIBSSH2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libssh2")
if(NOT EXISTS "${LIBSSH2_ROOT}/include/libssh2.h")
    message(FATAL_ERROR
        "找不到 ${LIBSSH2_ROOT}/include/libssh2.h\n"
        "先跑一次：bash third_party/build-libssh2.sh")
endif()

target_include_directories(EmShell PRIVATE "${LIBSSH2_ROOT}/include")

if(WIN32)
    target_link_libraries(EmShell PRIVATE emshell_icon)
endif()

target_link_libraries(EmShell PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::Network
    "${LIBSSH2_ROOT}/lib/libssh2.a"
)

# 本地终端走 Windows ConPTY；crypt32 是密码加密用的 DPAPI；
# ws2_32 和 bcrypt 是 libssh2（WinCNG 后端）要的
if(WIN32)
    target_link_libraries(EmShell PRIVATE kernel32 user32 crypt32 ws2_32 bcrypt dwmapi)

    # ConPTY 的声明在 wincon.h 里被 NTDDI_VERSION 挡着，
    # 必须在任何头文件之前把这些宏定出来（用编译选项而不是源码里 #define，
    # 免得 Qt 的头文件先把 windows.h 引进来了）。
    # 0x0A000006 = Windows 10 1809，也就是 ConPTY 首次可用的版本。
    target_compile_definitions(EmShell PRIVATE
        _WIN32_WINNT=0x0A00
        NTDDI_VERSION=0x0A000006
        WINVER=0x0A00
    )
endif()

# 把 Qt 的 DLL 拷到 exe 旁边，方便直接双击运行
if(WIN32)
    qt_generate_deploy_app_script(
        TARGET EmShell
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()
