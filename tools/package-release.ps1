# 打一个可以直接挂到 GitHub Release 的 zip。
#
# 为什么必须是 zip、而且必须带 DLL：自动更新是整包铺到安装目录的。
# 只挂裸 exe 的话，新版本新增的 Qt 模块（比如这次开始用的 Qt6Network.dll）
# 老用户目录里没有，更新完双击就起不来 —— 而用户几乎不可能想到问题出在更新上。
#
# 注意：本文件必须保存成「UTF-8 带 BOM」。Windows PowerShell 5.1 在没有 BOM 时
# 会按 ANSI 读，中文注释和字符串会被撕碎，脚本直接语法错误。
#
# 用法（在 Qt 的 MinGW 命令行里，或者确保 PATH 里有 windeployqt）：
#   powershell -ExecutionPolicy Bypass -File tools\package-release.ps1
#   powershell ... -File tools\package-release.ps1 -BuildDir build\release -OutDir dist
#
# 发版流程：
#   1. 改 CMakeLists.txt 的 project(... VERSION x.y.z)
#   2. 构建 release（或 debug，脚本两种都认）
#   3. 跑这个脚本
#   4. GitHub → Releases → Create a new release，tag 填 vx.y.z，
#      把 dist\EmShell-x.y.z-win64.zip 挂成附件

param(
    [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "build\debug"),
    [string]$QtBin = "D:\Program Files\QT\6.8.3\mingw_64\bin",
    [string]$OutDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "dist")
)

$ErrorActionPreference = 'Stop'

$exe = Join-Path $BuildDir "EmShell.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "找不到 $exe。先构建一次。"
}

$versionFile = Join-Path $BuildDir "version.txt"
if (-not (Test-Path -LiteralPath $versionFile)) {
    throw "找不到 $versionFile。重新跑一次 cmake --preset ... 就会生成。"
}
$version = (Get-Content -LiteralPath $versionFile -Raw).Trim()

$windeployqt = Join-Path $QtBin "windeployqt.exe"
if (-not (Test-Path -LiteralPath $windeployqt)) {
    $found = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if (-not $found) {
        throw "找不到 windeployqt.exe（试过 $windeployqt 和 PATH）"
    }
    $windeployqt = $found.Source
}

$stage = Join-Path $env:TEMP "EmShell-package-$version"
Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null

try {
    Copy-Item -LiteralPath $exe -Destination $stage

    # --no-translations：这个程序没有需要 Qt 翻译的界面，能省几 MB。
    # 其余依赖（platforms、styles、Qt6Network）由 windeployqt 自己判断。
    & $windeployqt --no-translations (Join-Path $stage "EmShell.exe")
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt 失败（退出码 $LASTEXITCODE）"
    }

    # 程序自己的图标资源已经编进 exe；Qt 的 DLL 和插件必须一起打进去。
    #
    # **用系统的 tar.exe 打 zip，别用 Compress-Archive**：Windows PowerShell 5.1
    # 的 Compress-Archive（以及 .NET 的 ZipFile）写出来的条目分隔符是反斜杠，
    # 标准解压工具会把 `platforms\qwindows.dll` 当成一个文件名，Qt 找不到平台
    # 插件，双击直接起不来。bsdtar 写的是标准正斜杠。
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $zip = Join-Path $OutDir "EmShell-$version-win64.zip"
    Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue

    $tar = Join-Path $env:SystemRoot "System32\tar.exe"
    if (-not (Test-Path -LiteralPath $tar)) {
        throw "找不到 $tar。这个功能需要 Windows 10 1803 起自带的 tar.exe。"
    }
    $items = Get-ChildItem -LiteralPath $stage -Force | ForEach-Object { $_.Name }
    & $tar -a -cf $zip -C $stage $items
    if ($LASTEXITCODE -ne 0) {
        throw "打包失败（tar 退出码 $LASTEXITCODE）"
    }

    Write-Host "已生成：$zip"
    Write-Host "把这个文件挂到 GitHub Release（tag 用 v$version）即可。"
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}
