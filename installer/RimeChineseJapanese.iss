#define MyAppName "雾凇拼音·中日混输输入法"
#define MyAppVersion "1.1.0"
#define MyAppPublisher "swiftol"
#ifndef MyOutputBaseFilename
#define MyOutputBaseFilename "Rime-Chinese-Japanese-1.1.0-Setup"
#endif

[Setup]
AppId={{AE395F84-DC9B-4DBF-95A9-0B4B178829C8}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\RimeChineseJapanese
DefaultGroupName={#MyAppName}
UninstallDisplayName={#MyAppName}
OutputDir=output
OutputBaseFilename={#MyOutputBaseFilename}
Compression=lzma2/fast
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
RestartIfNeededByRun=no
SetupLogging=yes
UsePreviousAppDir=no
DisableProgramGroupPage=yes

[Languages]
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"

[Files]
Source: "payload\runtime\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "payload\config\*"; DestDir: "{app}\config"; Excludes: ".git\*,build\*,sync\*,clipboard\*,*.userdb\*,*.userdb.txt,*.userdb.kct,user.yaml,installation.yaml,custom_phrase.txt,custom_japanese_fuzzy.tsv,custom_chinese_fuzzy.tsv,common_phrase_data.lua,*.log,*.bak,*.backup"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "payload\mozc\*"; DestDir: "{app}\mozc"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "payload\settings\RimeSettings.exe"; DestDir: "{app}\tools"; DestName: "RimeSettings.exe"; Flags: ignoreversion
; The customized tray menu launches the localized filename.  Keep the ASCII
; name for existing shortcuts and install an identical localized copy for the
; tray command.
Source: "payload\settings\RimeSettings.exe"; DestDir: "{app}\tools"; DestName: "中日方案设置.exe"; Flags: ignoreversion
Source: "README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "RimeUserBootstrap.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "RimeCandidateSelfTest.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "MozcV2Launcher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "cleanup-old-runtime.ps1"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
Type: filesandordirs; Name: "{app}\*"

[Icons]
Name: "{commonprograms}\{#MyAppName}\中日方案设置"; Filename: "{app}\tools\RimeSettings.exe"
Name: "{commonprograms}\{#MyAppName}\用户配置目录"; Filename: "{sys}\explorer.exe"; Parameters: "shell:AppData\Rime"
Name: "{commondesktop}\中日方案设置"; Filename: "{app}\tools\RimeSettings.exe"; Tasks: desktopicon
Name: "{commonstartup}\雾凇中日日语 V2 服务"; Filename: "{app}\MozcV2Launcher.exe"; WorkingDir: "{app}"

[Tasks]
Name: "desktopicon"; Description: "创建中日方案设置桌面快捷方式"; GroupDescription: "快捷方式："; Flags: checkedonce

[Run]
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM WeaselServer.exe"; StatusMsg: "正在停止旧版输入法服务……"; Flags: runhidden waituntilterminated
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoLogo -NoProfile -ExecutionPolicy Bypass -File ""{app}\cleanup-old-runtime.ps1"" -InstallRoot ""{app}"""; StatusMsg: "正在清理旧版本运行文件（保留个人数据）……"; Flags: runhidden waituntilterminated
Filename: "{app}\WeaselSetup.exe"; Parameters: "/s"; StatusMsg: "正在注册输入法……"; Flags: runhidden waituntilterminated; Check: NeedsRegistration
Filename: "{app}\RimeUserBootstrap.exe"; Parameters: """{app}"" --quiet"; StatusMsg: "正在更新配置并编译中日词库，请勿关闭……"; Flags: waituntilterminated runasoriginaluser

[Registry]
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Active Setup\Installed Components\{{F3D82154-C12E-478D-B7DA-D755D5BC39EA}"; ValueType: string; ValueName: "Version"; ValueData: "1,1,0,0"; Flags: uninsdeletekey
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Active Setup\Installed Components\{{F3D82154-C12E-478D-B7DA-D755D5BC39EA}"; ValueType: string; ValueName: "StubPath"; ValueData: """{app}\RimeUserBootstrap.exe"" ""{app}"" --quiet"; Flags: uninsdeletevalue

[UninstallRun]
Filename: "{app}\WeaselSetup.exe"; Parameters: "/u"; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterRimeChineseJapanese"

[Code]
function NeedsRegistration: Boolean;
var
  Root32, Root64: String;
begin
  Root32 := '';
  Root64 := '';
  RegQueryStringValue(HKLM32, 'SOFTWARE\Rime\Weasel', 'WeaselRoot', Root32);
  RegQueryStringValue(HKLM64, 'SOFTWARE\Rime\Weasel', 'WeaselRoot', Root64);
  Result := (CompareText(RemoveBackslashUnlessRoot(Root32), RemoveBackslashUnlessRoot(ExpandConstant('{app}'))) <> 0) or
            (CompareText(RemoveBackslashUnlessRoot(Root64), RemoveBackslashUnlessRoot(ExpandConstant('{app}'))) <> 0);
end;
