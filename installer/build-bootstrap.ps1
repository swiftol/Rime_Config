$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'RimeUserBootstrap.cs'
$output = Join-Path $PSScriptRoot 'RimeUserBootstrap.exe'
$framework = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (!(Test-Path -LiteralPath $framework)) { throw "C# compiler not found: $framework" }
& $framework /nologo /optimize+ /target:exe "/out:$output" $source
if ($LASTEXITCODE -ne 0) { throw 'Bootstrap build failed.' }
Write-Host "Built: $output"

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw "vswhere not found: $vswhere" }
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Visual Studio C++ build tools were not found.' }
$vsDevCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
$candidateSource = Join-Path $PSScriptRoot 'RimeCandidateSelfTest.cpp'
$candidateOutput = Join-Path $PSScriptRoot 'RimeCandidateSelfTest.exe'
$rimeInclude = Join-Path (Split-Path $PSScriptRoot -Parent) '..\weasel-code\librime\src'
$rimeInclude = [IO.Path]::GetFullPath($rimeInclude)
$compileCommand = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl.exe /nologo /EHsc /O2 /utf-8 /I"{1}" "{2}" /Fe:"{3}"' -f $vsDevCmd,$rimeInclude,$candidateSource,$candidateOutput
& $env:ComSpec /d /s /c $compileCommand
if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $candidateOutput)) { throw 'Candidate self-test build failed.' }
Write-Host "Built: $candidateOutput"
