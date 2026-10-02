param(
    [Parameter(Mandatory = $true)]
    [string]$RuntimeRoot,
    [string]$OutputBaseFilename = 'Rime-Chinese-Japanese-1.1.0-Setup'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$payload = Join-Path $PSScriptRoot 'payload'
$runtimeTarget = Join-Path $payload 'runtime'
$configTarget = Join-Path $payload 'config'
$settingsTarget = Join-Path $payload 'settings'
$mozcTarget = Join-Path $payload 'mozc'
$publish = Join-Path $PSScriptRoot 'publish-settings'

if (!(Test-Path (Join-Path $RuntimeRoot 'WeaselServer.exe'))) { throw "Invalid runtime: $RuntimeRoot" }
if (!(Test-Path (Join-Path $RuntimeRoot 'data'))) { throw "Runtime data directory is missing: $RuntimeRoot\data" }

& (Join-Path $PSScriptRoot 'build-bootstrap.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Bootstrap build failed.' }

dotnet publish (Join-Path $repo 'src\RimeSettings\RimeSettings.csproj') -c Release -o $publish
if ($LASTEXITCODE -ne 0) { throw 'Settings build failed.' }

# payload is a generated directory under installer; rebuilding it from zero
# prevents excluded backup files left by an older build from entering a release.
if (Test-Path -LiteralPath $payload) { Remove-Item -LiteralPath $payload -Recurse -Force }
New-Item -ItemType Directory -Force -Path $runtimeTarget,$configTarget,$settingsTarget,$mozcTarget | Out-Null
# Release only the active runtime.  The development runtime intentionally keeps
# many historical binaries and local tools; copying it wholesale made previous
# installers large and could leave ambiguous/stale modules on users' machines.
$runtimeFiles = @(
    '7-zip-license.txt','7z.dll','7z.exe','COPYING-curl.txt',
    'curl-ca-bundle.crt','curl.exe','LICENSE.txt','README.txt',
    'rime-install-config.bat','rime-install.bat','start_service.bat',
    'stop_service.bat','rime.dll','weasel.dll','weasel.ime',
    'WeaselDeployer.exe','WeaselServer.exe','WeaselSetup.exe',
    'weaselx64.dll','weaselx64.ime','WinSparkle.dll'
)
foreach ($name in $runtimeFiles) {
    $source = Join-Path $RuntimeRoot $name
    if (!(Test-Path -LiteralPath $source)) { throw "Required runtime file is missing: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $runtimeTarget $name) -Force
}
Copy-Item -LiteralPath (Join-Path $RuntimeRoot 'data') -Destination (Join-Path $runtimeTarget 'data') -Recurse -Force

# The Mozc V2 schema is a separate local Japanese scheme.  Ship its bridge,
# converter data and licenses as a self-contained directory; no user profile
# or learned data lives here.
$mozcSource = Join-Path $repo 'mozc-runtime'
if (!(Test-Path -LiteralPath $mozcSource)) { throw "Mozc runtime source is missing: $mozcSource" }
Copy-Item -Path (Join-Path $mozcSource '*') -Destination $mozcTarget -Recurse -Force
$requiredMozcFiles = @(
    'MozcBridge.exe', 'converter\converter_main.exe', 'romanji-hiragana.tsv',
    'data_manager\oss\mozc.data', 'licenses\MOZC-LICENSE.txt',
    'licenses\MOZC-DICTIONARY-NOTICE.txt'
)
foreach ($name in $requiredMozcFiles) {
    $path = Join-Path $mozcTarget $name
    if (!(Test-Path -LiteralPath $path)) { throw "Required Mozc V2 runtime file is missing: $path" }
}

$excludeDirectories = @('.git','build','sync','clipboard','installer','src','outputs','work','mozc-runtime')
$excludeFiles = @('user.yaml','installation.yaml','custom_phrase.txt','custom_japanese_fuzzy.tsv','custom_chinese_fuzzy.tsv','common_phrase_data.lua','*.bak','*.backup','*.log')
$arguments = @($repo,$configTarget,'/MIR','/NFL','/NDL','/NJH','/NJS','/NP','/XD') +
    ($excludeDirectories | ForEach-Object { Join-Path $repo $_ }) + @('/XF') + $excludeFiles
& robocopy @arguments | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Config copy failed: $LASTEXITCODE" }

# Fail the release build before Inno Setup when a schema references one of our
# product Lua components but its source file was not copied.  A previous
# installer contained japanese_fuzzy_filter.lua while omitting the three
# modules below; librime-lua then returned an empty candidate stream at runtime.
$requiredProductLua = @(
    'japanese_fuzzy_filter.lua',
    'japanese_fuzzy_learning.lua',
    'japanese_fuzzy_learning_processor.lua',
    'japanese_prefix_translator.lua',
    'mozc_v2_translator.lua',
    'japanese_sentence_lexicon.lua',
    'mozc_v2_prefix_translator.lua',
    'chinese_q_prefix_completion.lua',
    'google_cloud_candidate_filter.lua',
    'google_cloud_learning.lua',
    'chinese_abbreviation_learning_processor.lua'
)
foreach ($name in $requiredProductLua) {
    $luaPath = Join-Path $configTarget (Join-Path 'lua' $name)
    if (!(Test-Path -LiteralPath $luaPath)) {
        throw "Required product Lua module is missing from release payload: $luaPath"
    }
}
$qPrefixData = Join-Path $configTarget 'chinese_q_prefix_completion.tsv'
if (!(Test-Path -LiteralPath $qPrefixData)) {
    throw "Chinese q-prefix data is missing from release payload: $qPrefixData"
}

# Every schema dependency must ship as an actual schema.  Switch names are not
# dependencies: listing one here makes the deployer search for a nonexistent
# file and can leave an apparently successful but incomplete build.
$mainSchema = Join-Path $configTarget 'rime_ice_japanese.schema.yaml'
$inDependencies = $false
foreach ($line in Get-Content -LiteralPath $mainSchema -Encoding UTF8) {
    if ($line -match '^\s{2}dependencies:\s*$') { $inDependencies = $true; continue }
    if ($inDependencies -and $line -match '^\S') { break }
    if ($inDependencies -and $line -match '^\s{4}-\s+([A-Za-z0-9_-]+)\s*$') {
        $dependencySchema = Join-Path $configTarget ($Matches[1] + '.schema.yaml')
        if (!(Test-Path -LiteralPath $dependencySchema)) {
            throw "Schema dependency is missing from release payload: $dependencySchema"
        }
    }
}

$mozcSchema = Join-Path $configTarget 'rime_ice_japanese_mozc.schema.yaml'
if (!(Test-Path -LiteralPath $mozcSchema)) {
    throw "Mozc V2 schema is missing from release payload: $mozcSchema"
}

$runtimeData = Join-Path $runtimeTarget 'data'
$requiredRuntimeData = @('default.yaml','essay.txt','punctuation.yaml','opencc')
foreach ($name in $requiredRuntimeData) {
    $dataPath = Join-Path $runtimeData $name
    if (!(Test-Path -LiteralPath $dataPath)) {
        throw "Required runtime data is missing from release payload: $dataPath"
    }
}
$runtimeDataFiles = Get-ChildItem -LiteralPath $runtimeData -Recurse -File
if ($runtimeDataFiles.Count -lt 50 -or ($runtimeDataFiles | Measure-Object Length -Sum).Sum -lt 10000000) {
    throw "Runtime data payload is unexpectedly incomplete: $runtimeData"
}

Copy-Item (Join-Path $publish 'RimeSettings.exe') (Join-Path $settingsTarget 'RimeSettings.exe') -Force

$privatePatterns = @('*.userdb*','user.yaml','installation.yaml','custom_phrase.txt','custom_japanese_fuzzy.tsv','custom_chinese_fuzzy.tsv','common_phrase_data.lua','*.bak','*.backup','*.log')
foreach ($pattern in $privatePatterns) {
    $found = Get-ChildItem $payload -Recurse -Force -Filter $pattern -ErrorAction SilentlyContinue
    if ($found) { throw "Private data detected in payload: $($found.FullName -join ', ')" }
}

# Compile and exercise the exact release payload in an isolated user directory.
# This catches missing Lua modules and invalid component exports before an EXE
# can be produced.  It never reads or writes the developer's real Rime user dir.
$selfTestUser = Join-Path $PSScriptRoot 'payload-selftest-user'
if (Test-Path -LiteralPath $selfTestUser) { Remove-Item -LiteralPath $selfTestUser -Recurse -Force }
New-Item -ItemType Directory -Force -Path $selfTestUser | Out-Null
$bridgeProcess = $null
$mozcProfile = Join-Path $PSScriptRoot 'payload-selftest-mozc-profile'
try {
    $copyArgs = @($configTarget,$selfTestUser,'/MIR','/NFL','/NDL','/NJH','/NJS','/NP')
    & robocopy @copyArgs | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Self-test config copy failed: $LASTEXITCODE" }
    & (Join-Path $PSScriptRoot 'RimeCandidateSelfTest.exe') $runtimeTarget $selfTestUser '--deploy'
    if ($LASTEXITCODE -ne 0) { throw "Release candidate self-test failed: $LASTEXITCODE" }

    # Exercise the exact Mozc files placed in the installer.  Use an isolated
    # profile/mailbox so neither the developer's Mozc state nor an older local
    # installation can hide a missing release dependency.
    $mozcMailbox = Join-Path $selfTestUser 'mozc_v2_mailbox'
    if (Test-Path -LiteralPath $mozcProfile) { Remove-Item -LiteralPath $mozcProfile -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $mozcProfile,$mozcMailbox | Out-Null
    $bridge = Join-Path $mozcTarget 'MozcBridge.exe'
    $converter = Join-Path $mozcTarget 'converter\converter_main.exe'
    $romanTable = Join-Path $mozcTarget 'romanji-hiragana.tsv'
    $bridgeArguments = @(
        ('"' + $converter + '"'), ('"' + $mozcTarget + '"'),
        ('"' + $mozcProfile + '"'), ('"' + $romanTable + '"'), ('"' + $mozcMailbox + '"')
    )
    $bridgeProcess = Start-Process -FilePath $bridge -ArgumentList $bridgeArguments -PassThru -WindowStyle Hidden
    $ready = Join-Path $mozcMailbox 'bridge.ready'
    for ($i = 0; $i -lt 50 -and !(Test-Path -LiteralPath $ready); $i++) { Start-Sleep -Milliseconds 100 }
    if (!(Test-Path -LiteralPath $ready)) { throw 'Mozc V2 release bridge did not become ready.' }

    $coffeeOutput = & (Join-Path $PSScriptRoot 'RimeCandidateSelfTest.exe') `
        $runtimeTarget $selfTestUser '--schema=rime_ice_japanese_mozc' 'koqhiq' 2>&1
    $coffeeOutput | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0 -or (($coffeeOutput -join "`n") -notmatch '(?m)^CANDIDATE_1=コーヒー\r?$')) {
        throw 'Mozc V2 release test failed: koqhiq did not rank コーヒー first.'
    }
    $prefixOutput = & (Join-Path $PSScriptRoot 'RimeCandidateSelfTest.exe') `
        $runtimeTarget $selfTestUser '--schema=rime_ice_japanese_mozc' `
        '--option=japanese_prefix_completion_disabled=0' 'sappor' 2>&1
    $prefixOutput | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0 -or (($prefixOutput -join "`n") -notmatch '(?m)^CANDIDATE_1=札幌\r?$')) {
        throw 'Mozc V2 release test failed: sappor did not rank 札幌 first.'
    }
    $chineseOutput = & (Join-Path $PSScriptRoot 'RimeCandidateSelfTest.exe') `
        $runtimeTarget $selfTestUser '--schema=rime_ice_japanese_mozc' 'chanpin' 2>&1
    $chineseOutput | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0 -or (($chineseOutput -join "`n") -notmatch '(?m)^CANDIDATE_1=产品\r?$')) {
        throw 'Mozc mixed release test failed: chanpin did not rank 产品 first.'
    }
    $qPrefixOutput = & (Join-Path $PSScriptRoot 'RimeCandidateSelfTest.exe') `
        $runtimeTarget $selfTestUser '--schema=rime_ice_japanese_mozc' 'yiq' 2>&1
    $qPrefixOutput | ForEach-Object { Write-Host $_ }
    $qPrefixText = $qPrefixOutput -join "`n"
    if ($LASTEXITCODE -ne 0 -or $qPrefixText -notmatch '(?m)^CANDIDATE_[0-9]+=一起\r?$' -or
        $qPrefixText -notmatch '(?m)^CANDIDATE_[0-9]+=以前\r?$') {
        throw 'Chinese q-prefix release test failed: yiq has no useful Chinese completion.'
    }
    $qChainOutput = & (Join-Path $PSScriptRoot 'RimeCandidateSelfTest.exe') `
        $runtimeTarget $selfTestUser '--schema=rime_ice_japanese_mozc' 'yiqd' 2>&1
    $qChainOutput | ForEach-Object { Write-Host $_ }
    $qChainText = $qChainOutput -join "`n"
    if ($LASTEXITCODE -ne 0 -or $qChainText -notmatch '(?m)^CANDIDATE_[0-9]+=以前的\r?$' -or
        $qChainText -notmatch '(?m)^CANDIDATE_[0-9]+=一切都\r?$') {
        throw 'Chinese q-prefix chain release test failed: yiqd lost Chinese candidates.'
    }
    $associationOffOutput = & (Join-Path $PSScriptRoot 'RimeCandidateSelfTest.exe') `
        $runtimeTarget $selfTestUser '--schema=rime_ice_japanese_mozc' `
        '--option=japanese_prefix_completion_disabled=1' 'chany' 2>&1
    $associationOffOutput | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0 -or (($associationOffOutput -join "`n") -notmatch '(?m)^CANDIDATE_1=产业\r?$') -or
        (($associationOffOutput -join "`n") -match '(?m)^COMMENT_[1-8]=.*\[RIME_LANG:JA\]')) {
        throw 'Mozc association-off test failed: unfinished chany still emitted Mozc candidates.'
    }
    $associationOnOutput = & (Join-Path $PSScriptRoot 'RimeCandidateSelfTest.exe') `
        $runtimeTarget $selfTestUser '--schema=rime_ice_japanese_mozc' `
        '--option=japanese_prefix_completion_disabled=0' 'chany' 2>&1
    $associationOnOutput | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0 -or (($associationOnOutput -join "`n") -notmatch '(?m)^CANDIDATE_1=ちゃん家\r?$')) {
        throw 'Mozc association-on test failed: chany produced no Mozc candidates.'
    }
} finally {
    if ($bridgeProcess -and !$bridgeProcess.HasExited) { Stop-Process -Id $bridgeProcess.Id -Force }
    if (Test-Path -LiteralPath $mozcProfile) { Remove-Item -LiteralPath $mozcProfile -Recurse -Force }
    if (Test-Path -LiteralPath $selfTestUser) { Remove-Item -LiteralPath $selfTestUser -Recurse -Force }
}

$compiler = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
if (!(Test-Path $compiler)) { throw "Inno Setup compiler not found: $compiler" }
& $compiler ("/DMyOutputBaseFilename=" + $OutputBaseFilename) (Join-Path $PSScriptRoot 'RimeChineseJapanese.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed.' }
Write-Host (Join-Path $PSScriptRoot ("output\" + $OutputBaseFilename + ".exe"))
