$ErrorActionPreference = 'Continue'

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$desktop = [Environment]::GetFolderPath('Desktop')
$report = Join-Path $desktop ("Rime-CNJP-Candidate-Diagnostic_{0}.txt" -f $stamp)
$lines = New-Object System.Collections.Generic.List[string]

function Add-Line([string]$text = '') {
    $lines.Add($text)
    Write-Host $text
}

function Add-FileState([string]$label, [string]$path) {
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $item = Get-Item -LiteralPath $path
        Add-Line ("OK   {0}: {1} bytes; modified={2:o}; path={3}" -f $label, $item.Length, $item.LastWriteTime, $path)
    } else {
        Add-Line ("FAIL {0}: missing; path={1}" -f $label, $path)
    }
}

Add-Line 'Rime Chinese-Japanese empty-candidate diagnostic'
Add-Line ("Time={0:o}" -f (Get-Date))
Add-Line ("User={0}\{1}" -f $env:USERDOMAIN, $env:USERNAME)
Add-Line ("APPDATA={0}" -f $env:APPDATA)
Add-Line ("LOCALAPPDATA={0}" -f $env:LOCALAPPDATA)

Add-Line ''
Add-Line '=== Registry ==='
$registeredDirs = New-Object System.Collections.Generic.List[string]
foreach ($view in @('Registry32', 'Registry64')) {
    try {
        $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
            [Microsoft.Win32.RegistryHive]::CurrentUser,
            [Microsoft.Win32.RegistryView]::$view)
        $key = $base.OpenSubKey('Software\Rime\Weasel')
        $value = if ($key) { $key.GetValue('RimeUserDir') } else { $null }
        Add-Line ("{0} RimeUserDir={1}" -f $view, $value)
        if ($value) { $registeredDirs.Add([string]$value) }
        if ($key) { $key.Dispose() }
        $base.Dispose()
    } catch {
        Add-Line ("FAIL {0} registry: {1}" -f $view, $_.Exception.Message)
    }
}

$defaultRime = Join-Path $env:APPDATA 'Rime'
$rime = if ($registeredDirs.Count -gt 0) { $registeredDirs[0] } else { $defaultRime }
if (($registeredDirs | Select-Object -Unique).Count -gt 1) {
    Add-Line '[ERROR] Registry32 and Registry64 point to different user directories.'
}
if (-not [string]::Equals($rime, $defaultRime, [StringComparison]::OrdinalIgnoreCase)) {
    Add-Line ("[WARNING] Effective user directory is not the V1.0 default: {0}" -f $rime)
}
$build = Join-Path $rime 'build'
Add-Line ''
Add-Line ("=== User directory: {0} ===" -f $rime)
Add-FileState 'source default' (Join-Path $rime 'default.yaml')
Add-FileState 'source schema' (Join-Path $rime 'rime_ice_japanese.schema.yaml')
Add-FileState 'source dictionary entry' (Join-Path $rime 'rime_ice.dict.yaml')
Add-FileState 'source Chinese base dictionary' (Join-Path $rime 'cn_dicts\base.dict.yaml')
Add-FileState 'source translation dictionary' (Join-Path $rime 'cn_dicts\translations.dict.yaml')
Add-FileState 'source Japanese mozc dictionary' (Join-Path $rime 'japanese.mozc.dict.yaml')
Add-FileState 'source Japanese JMdict dictionary' (Join-Path $rime 'japanese.jmdict.dict.yaml')

Add-Line ''
Add-Line '=== Compiled candidate pipeline ==='
Add-FileState 'compiled default' (Join-Path $build 'default.yaml')
Add-FileState 'compiled schema' (Join-Path $build 'rime_ice_japanese.schema.yaml')
Add-FileState 'compiled exact Chinese prism' (Join-Path $build 'rime_ice_japanese_chinese_exact.prism.bin')
Add-FileState 'compiled Chinese table' (Join-Path $build 'rime_ice.table.bin')
Add-FileState 'compiled Japanese prism' (Join-Path $build 'japanese.prism.bin')
Add-FileState 'compiled Japanese table' (Join-Path $build 'japanese.table.bin')

if (Test-Path -LiteralPath $build) {
    $bins = @(Get-ChildItem -LiteralPath $build -Filter '*.bin' -File -ErrorAction SilentlyContinue)
    Add-Line ("BIN count={0}; totalMB={1:N1}" -f $bins.Count, (($bins | Measure-Object Length -Sum).Sum / 1MB))
    foreach ($file in $bins | Sort-Object Name) {
        Add-Line ("BIN {0} {1} bytes modified={2:o}" -f $file.Name, $file.Length, $file.LastWriteTime)
    }
}

Add-Line ''
Add-Line '=== Runtime processes ==='
Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -in @('WeaselServer.exe', 'WeaselDeployer.exe', 'WeaselSetup.exe') } |
    ForEach-Object { Add-Line ("PID={0} Name={1} Path={2} CommandLine={3}" -f $_.ProcessId, $_.Name, $_.ExecutablePath, $_.CommandLine) }

Add-Line ''
Add-Line '=== Configuration summaries ==='
foreach ($name in @('default.custom.yaml', 'installation.yaml', 'user.yaml')) {
    $path = Join-Path $rime $name
    if (-not (Test-Path -LiteralPath $path)) { continue }
    Add-Line ("FILE {0}" -f $path)
    Get-Content -LiteralPath $path -ErrorAction SilentlyContinue |
        Where-Object { $_ -match 'schema|distribution|last_build|ascii_mode' } |
        Select-Object -First 100 |
        ForEach-Object { Add-Line ("  {0}" -f $_) }
}

$installLog = Join-Path $env:LOCALAPPDATA 'RimeChineseJapanese\install.log'
Add-Line ''
Add-Line '=== Installer log ==='
if (Test-Path -LiteralPath $installLog) {
    Get-Content -LiteralPath $installLog -Tail 300 -ErrorAction SilentlyContinue |
        ForEach-Object { Add-Line $_ }
} else {
    Add-Line ("Missing installer log: {0}" -f $installLog)
}

$lines | Set-Content -LiteralPath $report -Encoding UTF8
Write-Host ''
Write-Host ("Diagnostic report created: {0}" -f $report)
Write-Host 'Send this TXT file to the developer.'
Read-Host 'Press Enter to close'
