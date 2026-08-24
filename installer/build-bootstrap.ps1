$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'RimeUserBootstrap.cs'
$output = Join-Path $PSScriptRoot 'RimeUserBootstrap.exe'
$framework = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (!(Test-Path -LiteralPath $framework)) { throw "C# compiler not found: $framework" }
& $framework /nologo /optimize+ /target:exe "/out:$output" $source
if ($LASTEXITCODE -ne 0) { throw 'Bootstrap build failed.' }
Write-Host "Built: $output"
