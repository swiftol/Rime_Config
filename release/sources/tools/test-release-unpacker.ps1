$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'TestRunLifecycle.ps1')
if(!(Test-ManagedRunActive $PSCommandPath)){
 Invoke-ManagedTest -Script $PSCommandPath -Parameters @{} -Purpose 'Verify pinned public baseline installer extraction without installing or modifying real user settings' -Expected 'Public v2.0.5 installer SHA matches; console extraction exits zero; extracted launcher/config present'
 return
}
$run=Get-ManagedTestRun
$base=Join-Path $run.Project 'outputs/zhongri-release/ZhongriInputMethod-2.0.5-Setup.exe'
Add-TestRunInput $run $base 'public v2.0.5 baseline installer'
if((Get-FileHash $base).Hash -ne '48D13CE8DA7DCAA6677106992E12316AA51B4927A9F7BD94A07C2818D370BA81'){throw 'Baseline installer differs from public release'}
Assert-TestCopySpace $run @((Join-Path $run.Project 'outputs/package-stage-20261002-145528'))
$fixture=New-TestRunDirectory $run 'extract'
$revision=(Invoke-RestMethod 'https://api.github.com/repos/jrathlev/InnoUnpacker-Windows-GUI/commits/master').sha
$url="https://raw.githubusercontent.com/jrathlev/InnoUnpacker-Windows-GUI/$revision/innounp-2/bin/innounp-2.zip"
$zip=Join-Path $run.TempDirectory 'unpacker.zip'
Invoke-WebRequest $url -OutFile $zip
Expand-Archive -LiteralPath $zip -DestinationPath (Join-Path $run.TempDirectory 'unpacker')
$exe=Get-ChildItem (Join-Path $run.TempDirectory 'unpacker') -Recurse -Filter innounp.exe -File |Select-Object -First 1
if(!$exe){$exe=Get-ChildItem (Join-Path $run.TempDirectory 'unpacker') -Recurse -Filter '*.exe' -File|Select-Object -First 1}
Invoke-TestRunCommand $run $exe.FullName @('-x','-y',('-d'+$fixture),$base)
$launcher=Get-ChildItem $fixture -Recurse -File -Filter ZhongriLauncher.exe | Select-Object -First 1
if(!$launcher){throw 'No launcher in extracted baseline'}
$root=$launcher.DirectoryName
if(!(Test-Path (Join-Path $root 'config/rime_ice_japanese_mozc.schema.yaml'))){throw 'No schema in extracted baseline'}
$toolRoot=Join-Path $run.Project 'outputs/release-tools'
New-Item -ItemType Directory -Path $toolRoot -Force|Out-Null
Copy-Item $zip (Join-Path $toolRoot 'innounp-2.zip')
@{Url=$url;SHA256=(Get-FileHash $zip).Hash;Exe=$exe.Name;ExtractedRootRelative=$root.Substring($fixture.Length).TrimStart('\')}|ConvertTo-Json|Set-Content (Join-Path $toolRoot 'unpacker.json') -Encoding utf8
$run.Data.Outputs.Add(@{Path=$toolRoot;Role='retained verified release preparation tool, not disposable test environment'})
"PASS public baseline console extraction; root relative=$($root.Substring($fixture.Length)); tool SHA saved"
