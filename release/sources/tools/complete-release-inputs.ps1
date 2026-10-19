$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'TestRunLifecycle.ps1')
if(!(Test-ManagedRunActive $PSCommandPath)){
 Invoke-ManagedTest -Script $PSCommandPath -Parameters @{} -Purpose 'Complete corresponding sources, license notices and hash-pinned release trigger' -Expected 'No credentials, histories or generated object caches; exact installed native/UI source and explicit packaging scripts included; payload hashes unchanged'
 return
}
$run=Get-ManagedTestRun
$repo=Join-Path $run.Project 'work/Rime_Config'
$release=Join-Path $repo 'release'
function Copy-PublicFile([string]$from,[string]$to){
 if(Test-Path $to){throw "New release support file already exists: $to"}
 Add-TestRunInput $run $from 'corresponding source / license / packaging input'
 New-Item -ItemType Directory -Path (Split-Path $to) -Force|Out-Null
 Copy-Item -LiteralPath $from -Destination $to
}
$installer=Join-Path $release 'Installer.iss'
if(Test-Path $installer){throw 'Release installer source already prepared'}
$iss=Get-Content (Join-Path $PSScriptRoot 'ZhongriInputMethod.iss') -Raw
$iss=$iss.Replace('..\work\zhongri-weasel\resource\weasel.ico','weasel.ico').Replace('..\work\Rime_Config\installer\ChineseSimplified.isl','ChineseSimplified.isl')
[IO.File]::WriteAllText($installer,$iss,[Text.UTF8Encoding]::new($false))
Copy-PublicFile (Join-Path $run.Project 'work/zhongri-weasel/resource/weasel.ico') (Join-Path $release 'weasel.ico')
Copy-PublicFile (Join-Path $repo 'installer/ChineseSimplified.isl') (Join-Path $release 'ChineseSimplified.isl')
Copy-PublicFile (Join-Path $run.Project 'references/japanese-input-tables/SOURCES.md') (Join-Path $release 'INPUT-SCHEME-SOURCES.md')
Copy-PublicFile (Join-Path $run.Project 'references/japanese-input-tables/kagiroi-LICENSE') (Join-Path $release 'licenses/kagiroi-LICENSE')
$extensions=@('.cs','.csproj','.xaml','.cpp','.c','.h','.hpp','.rc','.vcxproj','.filters','.props','.targets','.sln','.def','.manifest','.ico','.png','.svg','.resx','.cmake','.json')
foreach($pair in @(
 @('work/zhongri-weasel','native'),@('work/zhongri-settings-v2','settings-v2'),@('work/zhongri-settings','settings-core'),@('work/ZhongriTestBench','test-bench'),@('work/weasel-mozc-v2/MozcBridge','mozc-bridge'),@('work/third_party/msime-japanese','sentence-decoder')
)){
 $root=Join-Path $run.Project $pair[0]
 foreach($file in Get-ChildItem $root -Recurse -File){
  $relative=$file.FullName.Substring($root.Length+1)
  if($relative -match '(?i)(^|[\\/])(bin|obj|output|outputs|lib|lib64|deps|\.git|node_modules|publish[^\\/]*|LayoutRegression)[\\/]'){continue}
  if($file.Extension -notin $extensions -and $file.Name -notmatch '^(LICENSE|COPYING)'){continue}
  if($file.Length -gt 4MB){throw "Unexpected large source: $relative"}
  Copy-PublicFile $file.FullName (Join-Path $release ('sources/'+$pair[1]+'/'+$relative))
 }
}
foreach($name in 'ZhongriLauncher.cs','ZhongriInputMethod.iss','prepare-github-release.ps1','complete-release-inputs.ps1','build-release-selftest.cmd','test-release-unpacker.ps1','TestRunLifecycle.ps1'){
 Copy-PublicFile (Join-Path $PSScriptRoot $name) (Join-Path $release ('sources/tools/'+$name))
}
Copy-PublicFile (Join-Path $repo 'installer/RimeCandidateSelfTest.cpp') (Join-Path $release 'sources/tools/RimeCandidateSelfTest.cpp')
$manifestFile=Join-Path $release 'v2.1.0/manifest.json'
$manifest=Get-Content $manifestFile -Raw|ConvertFrom-Json
foreach($change in $manifest.Changes){
 if((Get-FileHash (Join-Path $release ('v2.1.0/runtime-overlay/'+$change.Path))).Hash -ne $change.SHA256){throw 'Prepared payload changed'}
}
@{Version='2.1.0';ManifestSHA256=(Get-FileHash $manifestFile).Hash;PreparationReport=$manifest.ValidationRun;LocalCheckOnlyUntilPushed=$true}|ConvertTo-Json|Set-Content (Join-Path $release 'ready-v2.1.0.json') -Encoding utf8
$run.Data.Outputs.Add(@{Path=$release;Role='public release inputs and source, retained'})
'PASS exact runtime overlay preserved; corresponding sources and licensed origins prepared; trigger file is local only'
