$ErrorActionPreference = 'Stop'

function Get-RimeUserDir {
    foreach ($view in @('Registry64', 'Registry32')) {
        $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
            [Microsoft.Win32.RegistryHive]::CurrentUser,
            [Microsoft.Win32.RegistryView]::$view)
        try {
            $key = $base.OpenSubKey('Software\Rime\Weasel')
            try {
                if ($key) {
                    $value = [string]$key.GetValue('RimeUserDir')
                    if ($value) { return $value }
                }
            } finally { if ($key) { $key.Dispose() } }
        } finally { $base.Dispose() }
    }
    return (Join-Path $env:APPDATA 'Rime')
}

$rime = Get-RimeUserDir
$userYaml = Join-Path $rime 'user.yaml'
$runningServer = Get-CimInstance Win32_Process -Filter "Name='WeaselServer.exe'" -ErrorAction SilentlyContinue |
    Select-Object -First 1
$server = if ($runningServer -and $runningServer.ExecutablePath) {
    $runningServer.ExecutablePath
} else {
    'C:\Program Files\RimeChineseJapanese\WeaselServer.exe'
}

Write-Host ("Rime user directory: {0}" -f $rime)
if (-not (Test-Path -LiteralPath $userYaml)) {
    throw "Missing user.yaml: $userYaml"
}

$backup = $userYaml + '.before_schema_repair_' + (Get-Date -Format 'yyyyMMdd_HHmmss')
Copy-Item -LiteralPath $userYaml -Destination $backup
$text = [IO.File]::ReadAllText($userYaml)
$pattern = '(?m)^(\s*previously_selected_schema:\s*).*$'
if ([Text.RegularExpressions.Regex]::IsMatch($text, $pattern)) {
    $text = [Text.RegularExpressions.Regex]::Replace($text, $pattern, '${1}rime_ice_japanese', 1)
} else {
    throw 'previously_selected_schema was not found in user.yaml'
}
[IO.File]::WriteAllText($userYaml, $text, (New-Object Text.UTF8Encoding($false)))

if (-not (Test-Path -LiteralPath $server)) { throw "Missing server: $server" }
Get-Process WeaselServer -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
Start-Process -FilePath $server -WindowStyle Hidden
Write-Host 'Repair completed. The selected schema is now rime_ice_japanese.'
Write-Host 'Open Notepad and type nihao to verify candidates.'
Read-Host 'Press Enter to close'
