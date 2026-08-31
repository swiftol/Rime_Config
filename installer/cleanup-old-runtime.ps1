param([Parameter(Mandatory = $true)][string]$InstallRoot)
$ErrorActionPreference = 'Continue'
$stateDir = Join-Path $env:ProgramData 'RimeChineseJapanese'
$logFile = Join-Path $stateDir 'runtime-cleanup.log'
New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
function Log([string]$text) { Add-Content -LiteralPath $logFile -Encoding UTF8 -Value ("{0}  {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'),$text) }
function Normalize([string]$path) { if(!$path){return ''}; try{return [IO.Path]::GetFullPath($path).TrimEnd('\')}catch{return ''} }
function Is-ProductRoot([string]$path) {
    $full=Normalize $path; if(!$full){return $false}
    $leaf=[IO.Path]::GetFileName($full)
    return ($leaf -eq 'RimeChineseJapanese' -or $leaf -match '^weasel-[0-9]') -and
        (Test-Path -LiteralPath (Join-Path $full 'WeaselSetup.exe'))
}
function Run-Bounded([string]$exe,[string]$args,[int]$seconds) {
    try {
        $p=Start-Process -FilePath $exe -ArgumentList $args -PassThru -WindowStyle Hidden
        if(!$p.WaitForExit($seconds*1000)){ try{$p.Kill()}catch{}; Log "Timed out: $exe $args"; return -1 }
        Log "Exit $($p.ExitCode): $exe $args"; return $p.ExitCode
    } catch { Log "Failed: $exe $args : $($_.Exception.Message)"; return -2 }
}
$newRoot=Normalize $InstallRoot
$roots=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach($view in @([Microsoft.Win32.RegistryView]::Registry32,[Microsoft.Win32.RegistryView]::Registry64)){
    try{
        $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,$view)
        $key=$base.OpenSubKey('SOFTWARE\Rime\Weasel')
        if($key){$value=[string]$key.GetValue('WeaselRoot'); if($value){[void]$roots.Add((Normalize $value))};$key.Dispose()}
        $base.Dispose()
    }catch{}
}
foreach($known in @('C:\Program Files\RimeChineseJapanese','C:\Program Files (x86)\RimeChineseJapanese','D:\RimeChineseJapanese','H:\RimeChineseJapanese')){
    if(Test-Path -LiteralPath $known){[void]$roots.Add((Normalize $known))}
}
Get-Process WeaselServer,WeaselDeployer,WeaselSetup -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 700
foreach($root in $roots){
    if(!$root -or [string]::Equals($root,$newRoot,[StringComparison]::OrdinalIgnoreCase)){continue}
    if(!(Is-ProductRoot $root)){Log "Skipped unrecognized root: $root";continue}
    Run-Bounded (Join-Path $root 'WeaselSetup.exe') '/u' 12 | Out-Null
    Get-Process WeaselServer,WeaselDeployer,WeaselSetup -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    try{Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction Stop;Log "Removed old runtime: $root"}catch{Log "Could not remove old runtime: $root : $($_.Exception.Message)"}
}
Log "Legacy cleanup finished; current runtime: $newRoot"
