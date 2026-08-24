param([Parameter(Mandatory = $true)][string]$InstallRoot)
$ErrorActionPreference = 'Stop'
$stateDir = Join-Path $env:ProgramData 'RimeChineseJapanese'
$logFile = Join-Path $stateDir 'runtime-cleanup.log'
New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
function Write-CleanupLog([string]$Message) { Add-Content -LiteralPath $logFile -Encoding UTF8 -Value ("{0}  {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Message) }
function Get-OldWeaselRoot {
    foreach ($key in @('HKLM:\SOFTWARE\WOW6432Node\Rime\Weasel', 'HKLM:\SOFTWARE\Rime\Weasel')) {
        if (Test-Path -LiteralPath $key) { $value=(Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue).WeaselRoot; if($value){return [string]$value} }
    }
    return $null
}
function Test-SafeWeaselRoot([string]$Path) {
    if(!$Path){return $false}; try{$full=[IO.Path]::GetFullPath($Path).TrimEnd('\')}catch{return $false}
    if($full -match '^[A-Za-z]:$' -or [IO.Path]::GetFileName($full) -notmatch '^weasel-[0-9]'){return $false}
    return (Test-Path -LiteralPath (Join-Path $full 'WeaselServer.exe')) -and (Test-Path -LiteralPath (Join-Path $full 'rime.dll'))
}
function Register-DeleteOnReboot([string]$Path) {
    $command='"{0}" /d /c rd /s /q "{1}"' -f $env:ComSpec,$Path
    New-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\RunOnce' -Name 'RimeCNJPRemoveOldRuntime' -PropertyType String -Value $command -Force|Out-Null
    Write-CleanupLog "旧目录仍有被占用文件，已登记下次开机自动删除：$Path"
}
$oldRoot=Get-OldWeaselRoot; $newRoot=[IO.Path]::GetFullPath($InstallRoot).TrimEnd('\'); Write-CleanupLog "旧目录：$oldRoot；新目录：$newRoot"
if($oldRoot -and (Test-SafeWeaselRoot $oldRoot)){$oldFull=[IO.Path]::GetFullPath($oldRoot).TrimEnd('\');if(![string]::Equals($oldFull,$newRoot,[StringComparison]::OrdinalIgnoreCase)){$oldSetup=Join-Path $oldFull 'WeaselSetup.exe';if(Test-Path -LiteralPath $oldSetup){try{Start-Process -FilePath $oldSetup -ArgumentList '/u' -Wait -WindowStyle Hidden}catch{}};try{Remove-Item -LiteralPath $oldFull -Recurse -Force}catch{Register-DeleteOnReboot $oldFull}}}
$uninstallKey='HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Weasel';if(Test-Path -LiteralPath $uninstallKey){$u=[string](Get-ItemProperty -LiteralPath $uninstallKey -ErrorAction SilentlyContinue).UninstallString;if(!$oldRoot -or $u -like "*$oldRoot*"){Remove-Item -LiteralPath $uninstallKey -Recurse -Force -ErrorAction SilentlyContinue}}
