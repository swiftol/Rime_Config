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
robocopy $RuntimeRoot $runtimeTarget /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Runtime copy failed: $LASTEXITCODE" }

$excludeDirectories = @('.git','build','sync','clipboard','installer','src','outputs','work')
$excludeFiles = @('user.yaml','installation.yaml','custom_phrase.txt','common_phrase_data.lua','*.bak','*.backup','*.log')
$arguments = @($repo,$configTarget,'/MIR','/NFL','/NDL','/NJH','/NJS','/NP','/XD') +
    ($excludeDirectories | ForEach-Object { Join-Path $repo $_ }) + @('/XF') + $excludeFiles
& robocopy @arguments | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Config copy failed: $LASTEXITCODE" }

Copy-Item (Join-Path $publish 'RimeSettings.exe') (Join-Path $settingsTarget 'RimeSettings.exe') -Force

$privatePatterns = @('*.userdb*','user.yaml','installation.yaml','custom_phrase.txt','common_phrase_data.lua','*.bak','*.backup','*.log')
foreach ($pattern in $privatePatterns) {
    $found = Get-ChildItem $payload -Recurse -Force -Filter $pattern -ErrorAction SilentlyContinue
    if ($found) { throw "Private data detected in payload: $($found.FullName -join ', ')" }
}

$compiler = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
if (!(Test-Path $compiler)) { throw "Inno Setup compiler not found: $compiler" }
& $compiler (Join-Path $PSScriptRoot 'RimeChineseJapanese.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed.' }
Write-Host (Join-Path $PSScriptRoot 'output\Rime-Chinese-Japanese-1.0.0-Setup.exe')
