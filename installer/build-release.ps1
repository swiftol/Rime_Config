param(
    [Parameter(Mandatory = $true)]
    [string]$RuntimeRoot
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$payload = Join-Path $PSScriptRoot 'payload'
$runtimeTarget = Join-Path $payload 'runtime'
$configTarget = Join-Path $payload 'config'
$settingsTarget = Join-Path $payload 'settings'
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
New-Item -ItemType Directory -Force -Path $runtimeTarget,$configTarget,$settingsTarget | Out-Null
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

$excludeDirectories = @('.git','build','sync','clipboard','installer','src','outputs','work')
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
    'japanese_prefix_translator.lua'
)
foreach ($name in $requiredProductLua) {
    $luaPath = Join-Path $configTarget (Join-Path 'lua' $name)
    if (!(Test-Path -LiteralPath $luaPath)) {
        throw "Required product Lua module is missing from release payload: $luaPath"
    }
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
try {
    $copyArgs = @($configTarget,$selfTestUser,'/MIR','/NFL','/NDL','/NJH','/NJS','/NP')
    & robocopy @copyArgs | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Self-test config copy failed: $LASTEXITCODE" }
    & (Join-Path $PSScriptRoot 'RimeCandidateSelfTest.exe') $runtimeTarget $selfTestUser '--deploy'
    if ($LASTEXITCODE -ne 0) { throw "Release candidate self-test failed: $LASTEXITCODE" }
} finally {
    if (Test-Path -LiteralPath $selfTestUser) { Remove-Item -LiteralPath $selfTestUser -Recurse -Force }
}

$compiler = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
if (!(Test-Path $compiler)) { throw "Inno Setup compiler not found: $compiler" }
& $compiler (Join-Path $PSScriptRoot 'RimeChineseJapanese.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed.' }
Write-Host (Join-Path $PSScriptRoot 'output\Rime-Chinese-Japanese-1.1.0-Setup.exe')
