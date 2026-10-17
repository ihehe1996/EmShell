# 构建 EmShell 的两个发布产物：安装程序 + 便携 zip。
#
# 一次跑完：编译 release -> windeployqt 铺依赖 -> 冒烟测试 -> Inno Setup 打安装包
# -> 用系统 tar 打便携 zip。
#
# 为什么要两个：
#   - 安装程序给想装到 Program Files、有开始菜单和卸载项的人
#   - 便携 zip 给「解压到哪就在哪跑」的人，**自更新器要的也是它**
#     （GitHub Release 上两个都挂着时，更新器会跳过名字里带 install/setup 的文件）
#
# 版本号来自 build/release/version.txt，由 CMakeLists.txt 的 project(VERSION)
# 在 configure 时写出 —— 全项目只有那一处。
#
# 用法：
#   powershell -File tools\build-installer.ps1
#   powershell -File tools\build-installer.ps1 -SkipBuild
#
# 需要 Inno Setup 6：winget install JRSoftware.InnoSetup

[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [string]$Preset = "mingw-release"
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "build\release"
$StagingDir = Join-Path $RepoRoot "build\installer\staging"
$OutputDir = Join-Path $RepoRoot "dist"
$IssFile = Join-Path $RepoRoot "installer\EmShell.iss"

function Fail($message) {
    Write-Host "错误：$message" -ForegroundColor Red
    exit 1
}

# ---- 1. 找 Inno Setup 的命令行编译器 ----
function Find-Iscc {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe"
    )
    foreach ($path in $candidates) {
        if (Test-Path -LiteralPath $path) { return $path }
    }
    $onPath = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    return $null
}

function Find-QtBin {
    $cache = Join-Path $BuildDir "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cache)) { return $null }

    $match = Select-String -Path $cache -Pattern '^Qt6_DIR:PATH=(.+)$' | Select-Object -First 1
    if (-not $match) { return $null }

    # <qt>/lib/cmake/Qt6 -> <qt>
    $qtDir = $match.Matches[0].Groups[1].Value.Trim()
    $qtRoot = Split-Path (Split-Path (Split-Path $qtDir -Parent) -Parent) -Parent
    $bin = Join-Path $qtRoot "bin"
    if (Test-Path -LiteralPath (Join-Path $bin "windeployqt.exe")) { return $bin }
    return $null
}

function Find-MingwBin {
    # 从 CMakeCache 问编译器在哪，再把它的 bin 塞进 PATH：
    # windeployqt 的 --compiler-runtime 要靠 PATH 才找得到 libgcc/libstdc++/libwinpthread。
    $cache = Join-Path $BuildDir "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cache)) { return $null }

    $match = Select-String -Path $cache -Pattern '^CMAKE_CXX_COMPILER:[A-Z]+=(.+)$' | Select-Object -First 1
    if (-not $match) { return $null }

    $dir = Split-Path -Parent $match.Matches[0].Groups[1].Value.Trim()
    if (Test-Path -LiteralPath (Join-Path $dir "libgcc_s_seh-1.dll")) { return $dir }
    return $null
}

$Iscc = Find-Iscc
if (-not $Iscc) {
    Fail "找不到 ISCC.exe（Inno Setup 的命令行编译器）。装一下：winget install JRSoftware.InnoSetup"
}

# ---- 2. 编译 ----
if (-not (Test-Path -LiteralPath (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Host "第一次构建，先 configure ..." -ForegroundColor Cyan
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { Fail "cmake configure 失败" }
}

if (-not $SkipBuild) {
    Write-Host "构建 $Preset ..." -ForegroundColor Cyan
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { Fail "构建失败" }
}

$exe = Join-Path $BuildDir "EmShell.exe"
if (-not (Test-Path -LiteralPath $exe)) { Fail "没找到 $exe，先跑一次构建" }

# ---- 3. 版本号（CMake 是唯一来源）----
$versionFile = Join-Path $BuildDir "version.txt"
if (-not (Test-Path -LiteralPath $versionFile)) {
    Fail "没找到 version.txt。它由 CMake configure 生成，重新 configure 一次。"
}
$version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
if ($version -notmatch '^\d+\.\d+') { Fail "version.txt 里的版本号不正常：'$version'" }
Write-Host "版本 $version" -ForegroundColor Cyan

# ---- 4. 铺依赖到暂存目录 ----
$QtBin = Find-QtBin
if (-not $QtBin) {
    Fail "从 CMakeCache 里问不出 Qt 的 bin 目录，或者那里没有 windeployqt.exe"
}

$MingwBin = Find-MingwBin
if ($MingwBin) {
    $env:PATH = "$MingwBin;$env:PATH"
} else {
    Write-Host "  警告：没问出 MinGW 的 bin 目录，运行时 DLL 要靠 PATH 碰运气" -ForegroundColor Yellow
}

Write-Host "铺 Qt 依赖 ..." -ForegroundColor Cyan
if (Test-Path -LiteralPath $StagingDir) { Remove-Item -LiteralPath $StagingDir -Recurse -Force }
New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null
Copy-Item -LiteralPath $exe -Destination $StagingDir

& (Join-Path $QtBin "windeployqt.exe") `
    --release --no-translations --no-opengl-sw --compiler-runtime `
    --no-system-d3d-compiler --no-system-dxc-compiler `
    (Join-Path $StagingDir "EmShell.exe")
if ($LASTEXITCODE -ne 0) { Fail "windeployqt 失败" }

# windeployqt 漏拷运行时 DLL 时只警告一句，必须自己查
foreach ($dll in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll",
                   "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Svg.dll",
                   "Qt6Network.dll")) {
    if (-not (Test-Path -LiteralPath (Join-Path $StagingDir $dll))) {
        Fail "暂存目录里缺 $dll —— 这个包在别人机器上会起不来"
    }
}
foreach ($plugin in @("platforms\qwindows.dll", "tls\qschannelbackend.dll")) {
    if (-not (Test-Path -LiteralPath (Join-Path $StagingDir $plugin))) {
        Fail "暂存目录里缺 $plugin —— 缺了它程序/更新功能起不来"
    }
}

# 用不含 Qt 的最小 PATH 跑一次自检：开发机上 Qt 一直在 PATH 里，
# 少拷一个 DLL 也能跑；把 PATH 剥干净才能验出「换台机器能不能用」。
Write-Host "验证包能独立运行 ..." -ForegroundColor Cyan
$probeDir = Join-Path $RepoRoot "build\installer\deploy-check"
if (Test-Path -LiteralPath $probeDir) { Remove-Item -LiteralPath $probeDir -Recurse -Force }
New-Item -ItemType Directory -Path $probeDir -Force | Out-Null
$savedPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $probe = Start-Process -FilePath (Join-Path $StagingDir "EmShell.exe") `
                           -ArgumentList "--selftest" `
                           -WorkingDirectory $probeDir -Wait -PassThru -NoNewWindow
    if ($probe.ExitCode -ne 0) {
        Fail "脱离 Qt 环境跑自检失败（退出码 $($probe.ExitCode)）。多半是依赖没拷全，看 $probeDir\selftest.log"
    }
} finally {
    $env:PATH = $savedPath
    Remove-Item -LiteralPath $probeDir -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "  能独立运行（无 Qt 的 PATH 下自检通过）" -ForegroundColor DarkGray

# ---- 5. 清掉上一轮产物 ----
if (Test-Path -LiteralPath $OutputDir) {
    Get-ChildItem -LiteralPath $OutputDir -File |
        Where-Object { $_.Name -match '^EmShell-.*(-install\.exe|-win64\.zip)$' } |
        Remove-Item -Force
}

# ---- 6. 打安装程序 ----
Write-Host "打安装程序 ..." -ForegroundColor Cyan
& $Iscc "/DAppVersion=$version" "/DSourceDir=$StagingDir" "/DOutputDir=$OutputDir" $IssFile
if ($LASTEXITCODE -ne 0) { Fail "Inno Setup 编译失败" }

# ---- 7. 压便携 zip ----
# 自更新器优先挑 zip，所以这个不是可选项。
# 用系统 tar 打 zip：Compress-Archive / .NET ZipFile 写出来的条目用反斜杠，
# 标准解压工具会把 platforms\qwindows.dll 当成一个文件名，程序起不来。
Write-Host "压便携 zip ..." -ForegroundColor Cyan
$zipPath = Join-Path $OutputDir "EmShell-$version-win64.zip"
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }

$tar = Join-Path $env:SystemRoot "System32\tar.exe"
if (-not (Test-Path -LiteralPath $tar)) {
    Fail "找不到 $tar。这个功能需要 Windows 10 1803 起自带的 tar.exe。"
}
$items = Get-ChildItem -LiteralPath $StagingDir -Force | ForEach-Object { $_.Name }
& $tar -a -cf $zipPath -C $StagingDir $items
if ($LASTEXITCODE -ne 0) { Fail "打包 zip 失败（tar 退出码 $LASTEXITCODE）" }

# ---- 8. 汇报 ----
$setupPath = Join-Path $OutputDir "EmShell-v$version-install.exe"
foreach ($artifact in @($setupPath, $zipPath)) {
    if (-not (Test-Path -LiteralPath $artifact)) { Fail "产物没生成：$artifact" }
}

Write-Host ""
Write-Host "产物：" -ForegroundColor Green
foreach ($artifact in @($setupPath, $zipPath)) {
    $mb = [math]::Round((Get-Item -LiteralPath $artifact).Length / 1MB, 1)
    Write-Host ("  {0}  ({1} MB)" -f $artifact, $mb)
}
Write-Host ""
Write-Host "发 Release 时两个都传成附件，tag 写 v$version。" -ForegroundColor DarkGray
Write-Host "更新器会挑 zip；名字里带 install/setup 的安装程序会被跳过。" -ForegroundColor DarkGray
