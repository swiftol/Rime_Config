param(
    [Parameter(Mandatory = $true)]
    [string]$InstallRoot
)

$ErrorActionPreference = 'Stop'

$rimeDir = Join-Path $env:APPDATA 'Rime'
$templateDir = Join-Path $InstallRoot 'config'
$stateDir = Join-Path $env:LOCALAPPDATA 'RimeChineseJapanese'
$marker = Join-Path $stateDir 'configured-9.0.0.txt'
$deployer = Join-Path $InstallRoot 'WeaselDeployer.exe'
$server = Join-Path $InstallRoot 'WeaselServer.exe'

if (!(Test-Path -LiteralPath $templateDir)) {
    throw "找不到雾凇拼音·中日配置模板：$templateDir"
}
if (!(Test-Path -LiteralPath $deployer)) {
    throw "找不到小狼毫部署器：$deployer"
}

Get-Process WeaselServer -ErrorAction SilentlyContinue |
    Where-Object { $_.SessionId -eq (Get-Process -Id $PID).SessionId } |
    Stop-Process -Force -ErrorAction SilentlyContinue

New-Item -ItemType Directory -Path $stateDir -Force | Out-Null

if (!(Test-Path -LiteralPath $marker) -and (Test-Path -LiteralPath $rimeDir)) {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $backupDir = Join-Path $env:APPDATA "Rime_Backup_before_CNJP_V9_$stamp"
    Copy-Item -LiteralPath $rimeDir -Destination $backupDir -Recurse -Force
    Set-Content -LiteralPath (Join-Path $stateDir 'last-backup.txt') -Value $backupDir -Encoding UTF8
}

New-Item -ItemType Directory -Path $rimeDir -Force | Out-Null
Get-ChildItem -LiteralPath $templateDir -Force | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $rimeDir -Recurse -Force
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

$process = Start-Process -FilePath $deployer -ArgumentList '/deploy' -PassThru -Wait -WindowStyle Hidden
if ($process.ExitCode -ne 0) {
    throw "小狼毫重新部署失败，退出代码：$($process.ExitCode)"
}

Set-Content -LiteralPath $marker -Value (Get-Date -Format 'o') -Encoding UTF8
if (Test-Path -LiteralPath $server) {
    Start-Process -FilePath $server -WindowStyle Hidden
}
