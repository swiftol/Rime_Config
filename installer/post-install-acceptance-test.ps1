$ErrorActionPreference = 'Continue'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$desktop = [Environment]::GetFolderPath('Desktop')
$report = Join-Path $desktop "Rime-CNJP-Acceptance-$stamp.txt"
$failures = New-Object System.Collections.Generic.List[string]

function Out-Line([string]$text) {
    Write-Host $text
    Add-Content -LiteralPath $report -Encoding UTF8 -Value $text
}
function Fail([string]$code, [string]$detail) {
    $failures.Add($code)
    Out-Line ("FAIL [{0}] {1}" -f $code,$detail)
}
function Pass([string]$code, [string]$detail) {
    Out-Line ("PASS [{0}] {1}" -f $code,$detail)
}
function Read-Reg([Microsoft.Win32.RegistryHive]$hive,[Microsoft.Win32.RegistryView]$view,[string]$subkey,[string]$name) {
    try {
        $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey($hive,$view)
        $key=$base.OpenSubKey($subkey)
        $value=if($key){[string]$key.GetValue($name)}else{''}
        if($key){$key.Dispose()};$base.Dispose();return $value
    } catch { return '' }
}

Out-Line 'Rime Chinese-Japanese post-install acceptance test'
Out-Line ("Time={0:o}" -f (Get-Date))
Out-Line ("User={0}\{1}" -f $env:USERDOMAIN,$env:USERNAME)
$expectedUser=Join-Path $env:APPDATA 'Rime'

Out-Line ''
Out-Line '=== Registry and paths ==='
$root32=Read-Reg ([Microsoft.Win32.RegistryHive]::LocalMachine) ([Microsoft.Win32.RegistryView]::Registry32) 'SOFTWARE\Rime\Weasel' 'WeaselRoot'
$root64=Read-Reg ([Microsoft.Win32.RegistryHive]::LocalMachine) ([Microsoft.Win32.RegistryView]::Registry64) 'SOFTWARE\Rime\Weasel' 'WeaselRoot'
$user32=Read-Reg ([Microsoft.Win32.RegistryHive]::CurrentUser) ([Microsoft.Win32.RegistryView]::Registry32) 'SOFTWARE\Rime\Weasel' 'RimeUserDir'
$user64=Read-Reg ([Microsoft.Win32.RegistryHive]::CurrentUser) ([Microsoft.Win32.RegistryView]::Registry64) 'SOFTWARE\Rime\Weasel' 'RimeUserDir'
$registeredRoots=@(@($root32,$root64)|Where-Object{$_}|ForEach-Object{$_.TrimEnd('\')}|Sort-Object -Unique)
if($registeredRoots.Count -eq 1){
    $expectedRuntime=$registeredRoots[0]
    Pass 'REG_RUNTIME' "Registry32=$root32; Registry64=$root64; EffectiveRuntime=$expectedRuntime"
}elseif($registeredRoots.Count -eq 0){
    $expectedRuntime='C:\Program Files\RimeChineseJapanese'
    Fail 'REG_RUNTIME' "Both registry views are empty; fallback=$expectedRuntime"
}else{
    $expectedRuntime=$registeredRoots[0]
    Fail 'REG_RUNTIME' "Registry views disagree: Registry32=$root32; Registry64=$root64"
}
if($user32 -eq $expectedUser -and $user64 -eq $expectedUser){Pass 'REG_USERDIR' "32/64-bit=$expectedUser"}else{Fail 'REG_USERDIR' "Registry32=$user32; Registry64=$user64; expected=$expectedUser"}

$required=@('WeaselServer.exe','WeaselDeployer.exe','WeaselSetup.exe','rime.dll','weasel.dll','weaselx64.dll','RimeCandidateSelfTest.exe','config\rime_ice_japanese.schema.yaml')
$missing=@($required|Where-Object{!(Test-Path -LiteralPath (Join-Path $expectedRuntime $_))})
if($missing.Count -eq 0){Pass 'RUNTIME_FILES' 'All required runtime/config files exist.'}else{Fail 'RUNTIME_FILES' ("Missing: "+($missing -join ', '))}

$requiredLua=@(
    'japanese_fuzzy_filter.lua',
    'japanese_fuzzy_learning.lua',
    'japanese_fuzzy_learning_processor.lua',
    'japanese_prefix_translator.lua'
)
$missingLua=@($requiredLua|Where-Object{!(Test-Path -LiteralPath (Join-Path $expectedUser "lua\$_"))})
if($missingLua.Count -eq 0){
    $luaHashes=$requiredLua|ForEach-Object{
        $path=Join-Path $expectedUser "lua\$_"
        "$_="+(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }
    Pass 'PRODUCT_LUA' ("All 4 required modules exist; "+($luaHashes -join '; '))
}else{Fail 'PRODUCT_LUA' ("Missing from user config: "+($missingLua -join ', '))}

$dataRoot=Join-Path $expectedRuntime 'data'
$requiredData=@('default.yaml','essay.txt','punctuation.yaml','opencc')
$missingData=@($requiredData|Where-Object{!(Test-Path -LiteralPath (Join-Path $dataRoot $_))})
$dataFiles=@(Get-ChildItem -LiteralPath $dataRoot -File -Recurse -ErrorAction SilentlyContinue)
$dataBytes=($dataFiles|Measure-Object -Property Length -Sum).Sum
if($missingData.Count -eq 0 -and $dataFiles.Count -ge 50 -and $dataBytes -ge 10MB){
    Pass 'RUNTIME_DATA' ("Files={0}; Bytes={1}" -f $dataFiles.Count,$dataBytes)
}else{
    Fail 'RUNTIME_DATA' ("Missing={0}; Files={1}; Bytes={2}; expected at least 50 files and 10 MB" -f ($missingData -join ','),$dataFiles.Count,$dataBytes)
}

$dirty=@(Get-ChildItem -LiteralPath $expectedRuntime -File -ErrorAction SilentlyContinue|Where-Object{$_.Name -match '(?i)(backup|before|pre[-_.]|loaded-old|\.next\.|_202608)'}|Select-Object -ExpandProperty Name)
if($dirty.Count -eq 0){Pass 'CLEAN_RUNTIME' 'No development backup binaries found.'}else{Fail 'CLEAN_RUNTIME' ("Unexpected files: "+($dirty -join ', '))}

$oldRoots=@('C:\Program Files (x86)\RimeChineseJapanese','D:\RimeChineseJapanese','H:\RimeChineseJapanese')|Where-Object{Test-Path -LiteralPath $_}
if(@($oldRoots).Count -eq 0){Pass 'OLD_RUNTIME' 'No known legacy CNJP runtime directory remains.'}else{Fail 'OLD_RUNTIME' ("Legacy directories: "+($oldRoots -join ', '))}

Out-Line ''
Out-Line '=== Running service ==='
$servers=@(Get-CimInstance Win32_Process -Filter "Name='WeaselServer.exe'" -ErrorAction SilentlyContinue)
$expectedServer=Join-Path $expectedRuntime 'WeaselServer.exe'
$wrong=@($servers|Where-Object{$_.ExecutablePath -and $_.ExecutablePath.TrimEnd('\') -ne $expectedServer.TrimEnd('\')})
if($servers.Count -ge 1 -and $wrong.Count -eq 0){
    $serverDetails=$servers|ForEach-Object{"PID={0}/Session={1}" -f $_.ProcessId,$_.SessionId}
    Pass 'SERVER' ("Count={0}; Processes={1}; Path={2}" -f $servers.Count,($serverDetails -join ','),$expectedServer)
    if($servers.Count -gt 1){Out-Line 'WARN [SERVER_COUNT] Multiple server processes use the same registered runtime; this can be normal across Windows sessions. Candidate testing will determine whether the engine is healthy.'}
}
elseif($servers.Count -eq 0){Fail 'SERVER' 'WeaselServer.exe is not running.'}
else{Fail 'SERVER' ("ServerCount={0}; Paths={1}" -f $servers.Count,(($servers.ExecutablePath|Sort-Object -Unique)-join '; '))}

Out-Line ''
Out-Line '=== Real engine candidate test ==='
$tester=Join-Path $expectedRuntime 'RimeCandidateSelfTest.exe'
if(Test-Path -LiteralPath $tester){
    $stdout=Join-Path $env:TEMP "rime-accept-$stamp.out"
    $stderr=Join-Path $env:TEMP "rime-accept-$stamp.err"
    try{
        $p=Start-Process -FilePath $tester -ArgumentList @($expectedRuntime,$expectedUser) -Wait -PassThru -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $out=if(Test-Path $stdout){Get-Content -LiteralPath $stdout -Raw -Encoding UTF8}else{''}
        $err=if(Test-Path $stderr){Get-Content -LiteralPath $stderr -Raw -Encoding UTF8}else{''}
        Out-Line ("SelfTestExitCode={0}" -f $p.ExitCode)
        if($p.ExitCode -eq 0){($out -split "`r?`n"|Where-Object{$_ -match '^SCHEMA=|^CANDIDATE_COUNT=|^CANDIDATE_[1-3]='})|ForEach-Object{Out-Line $_}}
        else{($out -split "`r?`n"|Where-Object{$_})|ForEach-Object{Out-Line ("SelfTestOutput="+$_)}}
        if($err){Out-Line ("SelfTestError="+($err -replace "`r?`n",' '))}
        $nihao=([char]0x4F60)+([char]0x597D)
        if($p.ExitCode -eq 0 -and $out -match ("(?m)^CANDIDATE_1="+[regex]::Escape($nihao)+"\s*$")){Pass 'CANDIDATES' 'nihao -> first candidate is NIHAO (Chinese characters).'}
        elseif($p.ExitCode -eq 0 -and $out -match '(?m)^CANDIDATE_COUNT=([1-9][0-9]*)'){Pass 'CANDIDATES' 'nihao produced candidates.'}
        else{Fail 'CANDIDATES' ("Engine self-test failed with exit code {0}." -f $p.ExitCode)}
    }catch{Fail 'CANDIDATES' $_.Exception.Message}
    Remove-Item $stdout,$stderr -Force -ErrorAction SilentlyContinue
}else{Fail 'CANDIDATES' 'Installed self-test executable is missing.'}

Out-Line ''
Out-Line '=== PE import resolution test ==='
$probe=Join-Path $PSScriptRoot 'RimeImportProbe.exe'
if(Test-Path -LiteralPath $probe){
    $probeOutput=& $probe (Join-Path $expectedRuntime 'rime.dll') 2>&1
    $probeExit=$LASTEXITCODE
    $probeOutput|ForEach-Object{Out-Line ("ImportProbe="+$_)}
    if($probeExit -eq 0){Pass 'PE_IMPORTS' 'Every direct imported DLL and function resolved.'}
    else{Fail 'PE_IMPORTS' ("Import probe exit code {0}." -f $probeExit)}
}else{Fail 'PE_IMPORTS' 'RimeImportProbe.exe is missing from the test package.'}

Out-Line ''
if($failures.Count -eq 0){Out-Line 'FINAL_RESULT=PASS';Out-Line 'The installation passed all automated checks.'; $exitCode=0}
else{Out-Line 'FINAL_RESULT=FAIL';Out-Line ("FAILED_CHECKS="+($failures -join ','));$exitCode=1}
Out-Line ("Report={0}" -f $report)
Write-Host ''
Write-Host ("FINAL_RESULT={0}" -f $(if($exitCode -eq 0){'PASS'}else{'FAIL'}))
Write-Host ("Report={0}" -f $report)
exit $exitCode
