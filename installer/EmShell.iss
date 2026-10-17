; EmShell 安装程序（Inno Setup 6）
;
; 不要直接编译这个文件，用 tools/build-installer.ps1 构建：
; 版本号和暂存目录都是命令行传进来的，直接编译会用兜底值，打出一个空包。

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\installer\staging"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#define AppName "EmShell"
#define AppExeName "EmShell.exe"
#define AppPublisher "ihehe1996"
#define AppUrl "https://github.com/ihehe1996/EmShell"
#define AppVersionTag "v" + AppVersion

[Setup]
; AppId 是程序在 Windows 里的身份，**发出去之后不要改**。改了会被当成另一个
; 软件，覆盖安装会并存两份、卸载列表里也会出现两个。
AppId={{D3A6F0E2-9B47-4C81-8E5A-2F7C1B4D6A90}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersionTag}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}
AppUpdatesURL={#AppUrl}
VersionInfoVersion={#AppVersion}

; {autopf} 在 64 位系统上是 C:\Program Files
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
DefaultGroupName={#AppName}

; 装到 Program Files 需要管理员权限。只弹这一次。
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

OutputDir={#OutputDir}
; 名字里带 install：自更新器挑附件时会跳过含 setup/install 的文件，
; 免得把安装程序当成主程序替换掉（它只认 EmShell-<版本>-win64.zip）。
OutputBaseFilename={#AppName}-{#AppVersionTag}-install
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}
SetupLogging=yes

; 安装程序自己的图标。相对路径按本 .iss 所在目录解析。
SetupIconFile=..\resources\icons\app-logo.ico

; 我们用 [Code] 里的 taskkill 直接结束正在运行的 EmShell，
; 不让 Inno 再弹一个「无法自动关闭程序」的提示。
CloseApplications=no
RestartApplications=no

[Languages]
Name: "chinese"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Dirs]
; ★ 安装目录授予 users-modify ★
;
; 自动更新需要直接替换 {app} 里的 exe 和 DLL。装到 Program Files 时，
; 普通权限写不进去，UpdateInstaller::canReplace() 会失败，只能退化成手动下载。
; 这一行让安装之后的自动更新不再需要提权。
;
; 代价：本机其他用户也能修改这个目录里的程序文件。对一个普通权限的
; 连接工具来说，这点让步换来的是「更新不打扰用户」。
Name: "{app}"; Permissions: users-modify

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  // 覆盖安装时先结束正在运行的 EmShell，避免文件被占用。
  Exec('taskkill.exe', '/F /IM {#AppExeName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := '';
end;
