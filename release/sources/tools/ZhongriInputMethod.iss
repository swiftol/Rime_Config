#ifndef ProductVersion
#define ProductVersion "2.0.1"
#endif
#ifndef PackageRoot
#define PackageRoot "..\work\zhongri-package"
#endif
[Setup]
AppId={{72288688-D204-4561-9A92-3E331D587883}
AppName=中日输入法
AppVersion={#ProductVersion}
AppVerName=中日输入法 {#ProductVersion}
AppPublisher=中日输入法
DefaultDirName={autopf}\ZhongriInputMethod
DefaultGroupName=中日输入法
OutputDir=..\outputs\zhongri-release
OutputBaseFilename=ZhongriInputMethod-{#ProductVersion}-Setup
SetupIconFile=..\work\zhongri-weasel\resource\weasel.ico
UninstallDisplayIcon={app}\ZhongriLauncher.exe
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
PrivilegesRequired=admin
MinVersion=10.0.17763
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
DisableProgramGroupPage=yes
LicenseFile={#PackageRoot}\LICENSE.txt
[Languages]
Name: "chinesesimp"; MessagesFile: "..\work\Rime_Config\installer\ChineseSimplified.isl"
[Files]
Source: "{#PackageRoot}\*"; DestDir: "{app}"; Excludes: "*.pdb,*.log,*.marker,tools\RimeSettings.exe,tools\中日方案设置.exe,mozc\MozcBridge.exe,mozc\SHA256SUMS.txt"; Flags: ignoreversion recursesubdirs createallsubdirs
[InstallDelete]
; Retired executable entries only; no settings or user dictionaries are removed.
Type: files; Name: "{app}\tools\中日方案设置.exe"
Type: files; Name: "{app}\tools\RimeSettings.exe"
[Registry]
Root: HKLM; Subkey: "Software\ZhongriInputMethod"; ValueType: string; ValueName: "ZhongriRoot"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM32; Subkey: "Software\ZhongriInputMethod"; ValueType: string; ValueName: "ZhongriRoot"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "ZhongriInputMethod"; ValueData: """{app}\ZhongriLauncher.exe"" /start"; Flags: uninsdeletevalue
[Icons]
Name: "{group}\中日输入法"; Filename: "{app}\ZhongriLauncher.exe"
Name: "{group}\中日输入法设置"; Filename: "{app}\tools\Settings-WinUI\ZhongriSettingsV2.exe"
Name: "{group}\中日输入法测试台"; Filename: "{app}\tools\中日输入法测试台.exe"
Name: "{group}\卸载中日输入法"; Filename: "{uninstallexe}"
[Run]
Filename: "{app}\ZhongriLauncher.exe"; Parameters: "/init"; StatusMsg: "正在为当前用户部署独立词库，请稍候…"; Flags: runasoriginaluser waituntilterminated
[UninstallRun]
Filename: "{app}\ZhongriLauncher.exe"; Parameters: "/stop"; Flags: runhidden waituntilterminated; RunOnceId: "StopZhongri"
Filename: "{app}\ZhongriLauncher.exe"; Parameters: "/disable"; Flags: runhidden waituntilterminated; RunOnceId: "DisableZhongriProfile"
Filename: "{sys}\regsvr32.exe"; Parameters: "/s /u ""{app}\zhongrix64.dll"""; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterZhongri64"
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/s /u ""{app}\zhongri.dll"""; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterZhongri32"
; User dictionaries and settings are intentionally retained on uninstall.
[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var Code: Integer;
begin
  if CurStep = ssPostInstall then begin
    if not Exec(ExpandConstant('{sys}\regsvr32.exe'), '/s "' + ExpandConstant('{app}\zhongrix64.dll') + '"', '', SW_HIDE, ewWaitUntilTerminated, Code) then
      RaiseException('无法注册中日输入法（64 位）。');
    if Code <> 0 then RaiseException('中日输入法注册失败（64 位）。');
    if not Exec(ExpandConstant('{syswow64}\regsvr32.exe'), '/s "' + ExpandConstant('{app}\zhongri.dll') + '"', '', SW_HIDE, ewWaitUntilTerminated, Code) then
      RaiseException('无法注册中日输入法（32 位）。');
    if Code <> 0 then RaiseException('中日输入法注册失败（32 位）。');
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Code: Integer; Dir: String; Entry: TFindRec; HasFiles: Boolean;
begin
  Result := '';
  Dir := ExpandConstant('{app}');
  if FileExists(Dir + '\WeaselServer.exe') or FileExists(Dir + '\WeaselSetup.exe') or
     FileExists(Dir + '\weasel.dll') or FileExists(Dir + '\weaselx64.dll') then begin
    Result := '该目录属于原版小狼毫。请为中日输入法选择一个独立的安装目录。';
    Exit;
  end;
  if not FileExists(Dir + '\ZhongriLauncher.exe') then begin
    HasFiles := False;
    if FindFirst(Dir + '\*', Entry) then begin
      repeat
        if (Entry.Name <> '.') and (Entry.Name <> '..') then HasFiles := True;
      until not FindNext(Entry);
      FindClose(Entry);
    end;
    if HasFiles then begin
      Result := '首次安装需要一个空目录，以免覆盖其他软件或个人文件。';
      Exit;
    end;
  end;
  if FileExists(ExpandConstant('{app}\ZhongriLauncher.exe')) then
    Exec(ExpandConstant('{app}\ZhongriLauncher.exe'), '/stop', '', SW_HIDE, ewWaitUntilTerminated, Code);
end;
