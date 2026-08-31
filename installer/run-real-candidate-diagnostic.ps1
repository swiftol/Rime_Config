$ErrorActionPreference = 'Continue'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$desktop = [Environment]::GetFolderPath('Desktop')
$report = Join-Path $desktop "Rime-CNJP-Real-Candidate-Diagnostic_$stamp.txt"
function Add-Line([string]$Text) { $Text | Tee-Object -FilePath $report -Append }
Add-Line 'Rime CNJP real candidate read-only diagnostic'
Add-Line ("Time={0:o}" -f (Get-Date))
Add-Line ("User={0}" -f [Environment]::UserName)
$os = Get-CimInstance Win32_OperatingSystem
Add-Line ("Windows={0};Version={1};Build={2};Architecture={3}" -f $os.Caption,$os.Version,$os.BuildNumber,$os.OSArchitecture)
$server = Get-CimInstance Win32_Process -Filter "Name='WeaselServer.exe'" | Select-Object -First 1
$runtime = ''
if ($server -and $server.ExecutablePath) { $runtime = Split-Path $server.ExecutablePath -Parent }
elseif (Test-Path 'C:\Program Files\RimeChineseJapanese\WeaselServer.exe') { $runtime = 'C:\Program Files\RimeChineseJapanese' }
$userDir = Join-Path $env:APPDATA 'Rime'
$serverText = 'not running'
if ($server) { $serverText = "PID=$($server.ProcessId) $($server.ExecutablePath)" }
Add-Line ("Runtime={0}" -f $runtime)
Add-Line ("UserDir={0}" -f $userDir)
Add-Line ("Server={0}" -f $serverText)
$rimeDll = if ($runtime) { Join-Path $runtime 'rime.dll' } else { '' }
if ($rimeDll -and (Test-Path $rimeDll)) {
    $item = Get-Item $rimeDll
    Add-Line ("RimeDll={0};Bytes={1};SHA256={2}" -f $item.FullName,$item.Length,(Get-FileHash $rimeDll -Algorithm SHA256).Hash)
    $streams = Get-Item -LiteralPath $rimeDll -Stream * -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Stream
    Add-Line ("RimeDllStreams={0}" -f (($streams -join ',')))
    $signature = Get-AuthenticodeSignature -LiteralPath $rimeDll
    Add-Line ("RimeDllSignature={0};Signer={1}" -f $signature.Status,$signature.SignerCertificate.Subject)
}
$tester = Join-Path $PSScriptRoot 'RimeCandidateSelfTest.exe'
if (!$runtime -or !(Test-Path (Join-Path $runtime 'rime.dll'))) {
    Add-Line 'RESULT=RUNTIME_OR_RIME_DLL_NOT_FOUND'; Write-Host "Report=$report"; exit 2
}
if (!(Test-Path $tester)) { Add-Line 'RESULT=SELF_TEST_EXE_MISSING'; Write-Host "Report=$report"; exit 3 }
Add-Line ''
Add-Line '=== REAL ENGINE TEST: select rime_ice_japanese, type nihao ==='
$stdout = Join-Path $env:TEMP "rime-selftest-$stamp.out.txt"
$stderr = Join-Path $env:TEMP "rime-selftest-$stamp.err.txt"
try {
    $process = Start-Process -FilePath $tester -ArgumentList @($runtime, $userDir) -Wait -PassThru -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $out = if (Test-Path $stdout) { Get-Content $stdout -Raw -Encoding UTF8 } else { '' }
    $err = if (Test-Path $stderr) { Get-Content $stderr -Raw -Encoding UTF8 } else { '' }
    Add-Line ("ExitCode={0}" -f $process.ExitCode); Add-Line $out; Add-Line '=== LIBRIME LOG ==='; Add-Line $err
    if ($process.ExitCode -eq 0 -and $out -match 'CANDIDATE_1=') { Add-Line 'RESULT=ENGINE_OK_UI_OR_SERVICE_LAYER_FAULT' }
    else { Add-Line 'RESULT=ENGINE_CANDIDATE_PIPELINE_FAILED' }
} catch { Add-Line ("RESULT=DIAGNOSTIC_EXCEPTION: {0}" -f $_.Exception.Message) }
Add-Line ''
Add-Line '=== RECENT CODE INTEGRITY / APP CONTROL EVENTS ==='
$since = (Get-Date).AddMinutes(-30)
foreach ($logName in @('Microsoft-Windows-CodeIntegrity/Operational','Microsoft-Windows-AppLocker/EXE and DLL')) {
    Add-Line ("--- {0} ---" -f $logName)
    try {
        $events = Get-WinEvent -FilterHashtable @{LogName=$logName; StartTime=$since} -ErrorAction Stop |
            Where-Object { $_.Message -match 'rime|weasel|RimeCandidateSelfTest' } |
            Select-Object -First 20
        if ($events) {
            foreach ($event in $events) {
                Add-Line ("Event={0};Time={1:o};Message={2}" -f $event.Id,$event.TimeCreated,($event.Message -replace "`r?`n",' '))
            }
        } else { Add-Line 'No matching events.' }
    } catch { Add-Line ("Log unavailable: {0}" -f $_.Exception.Message) }
}
Remove-Item $stdout,$stderr -Force -ErrorAction SilentlyContinue
Write-Host "Diagnostic complete. Report=$report"
