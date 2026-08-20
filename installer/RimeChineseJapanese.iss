#define MyAppName "雾凇拼音·中日"
#define MyAppVersion "9.0.0"
#define MyAppPublisher "swiftol"

[Setup]
AppId={{AE395F84-DC9B-4DBF-95A9-0B4B178829C8}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\RimeChineseJapanese
DefaultGroupName={#MyAppName}
UninstallDisplayName={#MyAppName}
OutputDir=output
OutputBaseFilename=Rime-Chinese-Japanese-V9-AllUsers-Setup
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
UsePreviousAppDir=no
DisableProgramGroupPage=yes

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Files]
Source: "payload\runtime\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "payload\config\*"; DestDir: "{app}\config"; Excludes: ".git\*,build\*,sync\*,clipboard\*,*.userdb\*,*.userdb.txt,*.userdb.kct,user.yaml,installation.yaml,custom_phrase.txt,common_phrase_data.lua,*.log,*.bak,*.backup"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "payload\settings\RimeSettings.exe"; DestDir: "{app}\tools"; DestName: "RimeSettings.exe"; Flags: ignoreversion
Source: "README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "user-bootstrap.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "open-user-rime.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{commonprograms}\{#MyAppName}\中日方案设置"; Filename: "{app}\tools\RimeSettings.exe"
Name: "{commonprograms}\{#MyAppName}\用户配置目录"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{app}\open-user-rime.ps1"""
Name: "{commondesktop}\中日方案设置"; Filename: "{app}\tools\RimeSettings.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "创建中日方案设置桌面快捷方式"; GroupDescription: "快捷方式："; Flags: checkedonce

[Run]
Filename: "{app}\WeaselSetup.exe"; Parameters: "/s"; StatusMsg: "正在注册输入法……"; Flags: runhidden waituntilterminated
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\user-bootstrap.ps1"" -InstallRoot ""{app}"""; StatusMsg: "正在为当前 Windows 用户安装中日配置……"; Flags: runhidden waituntilterminated runasoriginaluser

[Registry]
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Active Setup\Installed Components\{{F3D82154-C12E-478D-B7DA-D755D5BC39EA}"; ValueType: string; ValueName: "Version"; ValueData: "9,0,0,0"; Flags: uninsdeletekey
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Active Setup\Installed Components\{{F3D82154-C12E-478D-B7DA-D755D5BC39EA}"; ValueType: string; ValueName: "StubPath"; ValueData: """{sys}\WindowsPowerShell\v1.0\powershell.exe"" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{app}\user-bootstrap.ps1"" -InstallRoot ""{app}"""; Flags: uninsdeletevalue

[UninstallRun]
Filename: "{app}\WeaselSetup.exe"; Parameters: "/u"; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterRimeChineseJapanese"
