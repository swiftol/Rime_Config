param(
    [Parameter(Mandatory = $true)]
    [string]$InstallRoot
)

$ErrorActionPreference = 'Stop'

$rimeDir = Join-Path $env:APPDATA 'Rime'
$templateDir = Join-Path $InstallRoot 'config'
$stateDir = Join-Path $env:LOCALAPPDATA 'RimeChineseJapanese'
$marker = Join-Path $stateDir 'configured-9.0.2.txt'
$deployer = Join-Path $InstallRoot 'WeaselDeployer.exe'
$server = Join-Path $InstallRoot 'WeaselServer.exe'

New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
$logFile = Join-Path $stateDir 'install.log'
function Write-InstallLog([string]$Message) {
    Add-Content -LiteralPath $logFile -Encoding UTF8 -Value `
        ("{0}  {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Message)
}
Write-InstallLog "开始为用户 $env:USERNAME 安装，安装目录：$InstallRoot"

if (!(Test-Path -LiteralPath $templateDir)) {
    throw "找不到雾凇拼音·中日配置模板：$templateDir"
}
if (!(Test-Path -LiteralPath $deployer)) {
    throw "找不到小狼毫部署器：$deployer"
}

Get-Process WeaselServer -ErrorAction SilentlyContinue |
    Where-Object { $_.SessionId -eq (Get-Process -Id $PID).SessionId } |
    Stop-Process -Force -ErrorAction SilentlyContinue

if (!(Test-Path -LiteralPath $marker) -and (Test-Path -LiteralPath $rimeDir)) {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $backupDir = Join-Path $env:APPDATA "Rime_Backup_before_CNJP_V9_$stamp"
    Write-InstallLog "备份用户数据到：$backupDir（排除可重建的 build 缓存）"
    # 备份目录与 Rime 同在 AppData\Roaming：直接重命名目录几乎瞬间完成，
    # 避免逐个复制数百 MB 旧词库。随后删除可重建且常超过 1 GB 的 build。
    Move-Item -LiteralPath $rimeDir -Destination $backupDir
    $backupBuildDir = Join-Path $backupDir 'build'
    if (Test-Path -LiteralPath $backupBuildDir) {
        Remove-Item -LiteralPath $backupBuildDir -Recurse -Force
    }
    Set-Content -LiteralPath (Join-Path $stateDir 'last-backup.txt') -Value $backupDir -Encoding UTF8

    # 旧配置可能包含旧 schema、Lua 和编译产物。直接在原目录覆盖会造成
    # 新旧版本混用，因此首次升级到 V9 时重建配置目录，再只恢复个人数据。
    Write-InstallLog '清理旧配置主体和旧编译缓存。'
    New-Item -ItemType Directory -Path $rimeDir -Force | Out-Null
}

# 即使是同版本修复安装，也必须清除 build；它只含可重建产物，却可能仍
# 引用旧 DLL / Lua / schema，是版本串用最常见的来源。
$buildDir = Join-Path $rimeDir 'build'
if (Test-Path -LiteralPath $buildDir) {
    Write-InstallLog '删除旧 build 编译缓存。'
    Remove-Item -LiteralPath $buildDir -Recurse -Force
}

Write-InstallLog '开始复制中日配置模板。'
New-Item -ItemType Directory -Path $rimeDir -Force | Out-Null
Get-ChildItem -LiteralPath $templateDir -Force | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $rimeDir -Recurse -Force
}

if ($backupDir -and (Test-Path -LiteralPath $backupDir)) {
    Write-InstallLog '恢复用户词频、同步数据、常用语和用户状态。'
    Get-ChildItem -LiteralPath $backupDir -Force | Where-Object {
        $_.Name -like '*.userdb*' -or
        $_.Name -in @('sync', 'clipboard', 'custom_phrase.txt', 'common_phrase_data.lua', 'user.yaml', 'installation.yaml')
    } | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $rimeDir -Recurse -Force
    }
}

$userFile = Join-Path $rimeDir 'user.yaml'
if (!(Test-Path -LiteralPath $userFile)) {
    $text = "var:`n  option:`n    sentence_translation: false`n  previously_selected_schema: rime_ice_japanese`n"
} else {
    $text = Get-Content -LiteralPath $userFile -Raw -Encoding UTF8
    if ($text -notmatch '(?m)^var:\s*$') {
        $text = $text.TrimEnd() + "`nvar:`n"
    }
    if ($text -match '(?m)^  previously_selected_schema:\s*.*$') {
        $text = [regex]::Replace($text, '(?m)^  previously_selected_schema:\s*.*$', '  previously_selected_schema: rime_ice_japanese')
    } else {
        $text = [regex]::Replace($text, '(?m)^var:\s*$', "var:`n  previously_selected_schema: rime_ice_japanese", 1)
    }
    if ($text -match '(?m)^    sentence_translation:\s*(true|false)\s*$') {
        $text = [regex]::Replace($text, '(?m)^    sentence_translation:\s*(true|false)\s*$', '    sentence_translation: false')
    } elseif ($text -match '(?m)^  option:\s*$') {
        $text = [regex]::Replace($text, '(?m)^  option:\s*$', "  option:`n    sentence_translation: false", 1)
    } else {
        $text = [regex]::Replace($text, '(?m)^var:\s*$', "var:`n  option:`n    sentence_translation: false", 1)
    }
}
Set-Content -LiteralPath $userFile -Value $text -Encoding UTF8 -NoNewline

Write-InstallLog '开始首次编译大型中日词库。'
$process = Start-Process -FilePath $deployer -ArgumentList '/deploy' -PassThru -WindowStyle Hidden
if (!$process.WaitForExit(2700000)) {
    try { $process.Kill() } catch {}
    Write-InstallLog '错误：首次部署超过 45 分钟，已终止。'
    throw '小狼毫首次部署超过 45 分钟。请查看安装日志。'
}
if ($process.ExitCode -ne 0) {
    Write-InstallLog "错误：部署器退出代码 $($process.ExitCode)。"
    throw "小狼毫重新部署失败，退出代码：$($process.ExitCode)"
}

Set-Content -LiteralPath $marker -Value (Get-Date -Format 'o') -Encoding UTF8
Write-InstallLog '配置复制和首次部署完成。'
if (Test-Path -LiteralPath $server) {
    Start-Process -FilePath $server -WindowStyle Hidden
}
