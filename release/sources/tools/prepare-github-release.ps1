param([string]$Version='2.1.0')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'TestRunLifecycle.ps1')
if(!(Test-ManagedRunActive $PSCommandPath)){
 Invoke-ManagedTest -Script $PSCommandPath -Parameters $PSBoundParameters -Purpose 'Prepare privacy-allowlisted installed runtime overlay and current public preferences for GitHub release' -Expected 'Pinned public baseline; four-view TSF verification; no secrets/learning; hot settings seed preserves existing values; fresh clean deployment and candidates pass; no real-window input'
 return
}
$run=Get-ManagedTestRun
$install='H:/ZhongriInputMethod'
$repo=Join-Path $run.Project 'work/Rime_Config'
$delivery=Join-Path $repo "release/v$Version"
if(Test-Path $delivery){throw 'Existing release delivery protected; use another version or inspect before retry'}
Assert-TestCopySpace $run @((Join-Path $run.Project 'outputs/package-stage-20261002-145528'))
$fixture=New-TestRunDirectory $run 'release-stage'
$tools=Join-Path $run.Project 'outputs/release-tools'
$unpack=Get-Content (Join-Path $tools 'unpacker.json') -Raw|ConvertFrom-Json
if((Get-FileHash (Join-Path $tools 'innounp-2.zip')).Hash -ne $unpack.SHA256){throw 'Unpacker hash mismatch'}
Expand-Archive (Join-Path $tools 'innounp-2.zip') (Join-Path $fixture 'unpacker')
$unpacker=Get-ChildItem (Join-Path $fixture 'unpacker') -Recurse -File -Filter $unpack.Exe|Select-Object -First 1
$base=Join-Path $run.Project 'outputs/zhongri-release/ZhongriInputMethod-2.0.5-Setup.exe'
if((Get-FileHash $base).Hash -ne '48D13CE8DA7DCAA6677106992E12316AA51B4927A9F7BD94A07C2818D370BA81'){throw 'Public baseline mismatch'}
Add-TestRunInput $run $base 'verified public base installer'
Invoke-TestRunCommand $run $unpacker.FullName @('-x','-y',('-d'+(Join-Path $fixture 'base')),$base)
$stage=Join-Path $fixture ('base/'+$unpack.ExtractedRootRelative)
New-Item -ItemType Directory -Path $delivery,(Join-Path $delivery 'runtime-overlay')|Out-Null
$run.Data.Outputs.Add(@{Path=$delivery;Role='release delivery/source inputs; never disposable'})
$changes=[Collections.Generic.List[object]]::new()
function Add-Payload([string]$Source,[string]$Relative){
 $null=Assert-TestPlainPath $Source
 Add-TestRunInput $run $Source 'explicit public release input'
 $target=Join-Path $stage $Relative
 $hash=(Get-FileHash -LiteralPath $Source).Hash
 if((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $target).Hash -eq $hash){return}
 $old=if(Test-Path $target){(Get-FileHash $target).Hash}else{$null}
 $overlay=Join-Path $delivery ('runtime-overlay/'+$Relative)
 New-Item -ItemType Directory -Path (Split-Path $target),(Split-Path $overlay) -Force|Out-Null
 Copy-Item -LiteralPath $Source -Destination $target -Force
 Copy-Item -LiteralPath $Source -Destination $overlay -Force
 $changes.Add(@{Path=$Relative.Replace('\','/');SHA256=$hash;BaseSHA256=$old;Length=(Get-Item $Source).Length})
}
foreach($name in 'ZhongriServer.exe','ZhongriDeployer.exe','rime.dll','WinSparkle.dll','LICENSE.txt','第三方组件说明.txt','start_service.bat'){
 Add-Payload (Join-Path $install $name) $name
}
$registrations=@()
foreach($hive in 'CurrentUser','LocalMachine'){foreach($view in 'Registry32','Registry64'){
 $key=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::$hive,[Microsoft.Win32.RegistryView]::$view)
 $reg=$key.OpenSubKey('Software\Classes\CLSID\{760B5BB8-680D-4D4C-920A-D32940AD6144}\InprocServer32')
 if(!$reg){throw "Missing registration $hive/$view"}
 $dll=[string]$reg.GetValue('');$reg.Dispose();$key.Dispose()
 if(![IO.Path]::GetFullPath($dll).StartsWith('H:\ZhongriInputMethod\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected registered DLL root'}
 $registrations+=@{Hive=$hive;View=$view;Path=$dll;SHA256=(Get-FileHash $dll).Hash}
}}
$registrations|ConvertTo-Json|Set-Content (Join-Path $run.ReportDirectory 'registration-four-views.json')
foreach($view in 'Registry32','Registry64'){
 $matches=@($registrations|Where-Object View -eq $view)
 if($matches[0].SHA256 -ne $matches[1].SHA256){throw 'HKCU/HKLM DLL mismatch'}
 Add-Payload $matches[0].Path $(if($view -eq 'Registry64'){'zhongrix64.dll'}else{'zhongri.dll'})
}
foreach($part in 'config','mozc','tools/Settings-WinUI'){
 $root=Join-Path $install $part
 foreach($file in Get-ChildItem $root -File -Recurse){
  if($file.Name -match '\.(pdb|log|bak|backup|tmp|lock)$' -or $file.FullName -match '[\\/](userdb|mailbox|MozcProfile|backups|clipboard|sync)[\\/]' -or $file.Name -match 'learning|frequency|observations|^user\.yaml$|^installation\.yaml$|common_phrase_data'){continue}
  Add-Payload $file.FullName ($part+'/'+$file.FullName.Substring($root.Length+1))
 }
}
Add-Payload (Join-Path $install 'tools/中日输入法测试台.exe') 'tools/中日输入法测试台.exe'
$userDir=(Get-ItemProperty HKCU:/Software/ZhongriInputMethod).ZhongriUserDir
$preferences=@('default.custom.yaml','weasel.custom.yaml','chinese_correction_rules.tsv','custom_chinese_fuzzy.tsv','custom_japanese_fuzzy.tsv','japanese_input_table.tsv','japanese_input_table.json','japanese_input_table.state','global-settings-profiles.json','emoji_second_position.txt','japanese_fuzzy_multiple.flag','japanese_reserved_whole.flag','japanese_vowel_tail_overlap.flag','japanese_single_key_tail.flag')
foreach($name in $preferences){$file=Join-Path $userDir $name;if(Test-Path $file){Add-Payload $file ('config/'+$name)}}
foreach($file in Get-ChildItem (Join-Path $userDir 'japanese_input_tables') -File){
 if($file.Name -match '^[a-f0-9]{32}(\.reserved)?\.(json|tsv)$'){Add-Payload $file.FullName ('config/japanese_input_tables/'+$file.Name)}
}
$optionLines=@(Get-Content (Join-Path $userDir 'user.yaml')|Where-Object {$_ -match '^    [a-zA-Z_][a-zA-Z0-9_]*: (true|false)$'})
if(!$optionLines.Count){throw 'Current option snapshot not found'}
$snapshot=Join-Path $fixture 'public-user.yaml'
@('var:','  option:')+$optionLines+@('  previously_selected_schema: rime_ice_japanese_mozc')|Set-Content $snapshot -Encoding utf8
Add-Payload $snapshot 'config/user.yaml'
$hot=Get-Item HKCU:/Software/ZhongriInputMethod
$values=[ordered]@{}
foreach($name in $hot.GetValueNames()){
 if($name -match 'Generation|Trace|Debug|Process|Window|Pid|Api|Protected|Token|Password|Secret|AppId|Root|UserDir'){continue}
 if($hot.GetValueKind($name) -eq 'DWord'){$values[$name]=[int]$hot.GetValue($name)}
 elseif($name -match '^ShortcutText_(Learning|NextProfile|PreviousProfile)$'){$values[$name]=[string]$hot.GetValue($name)}
}
$seed=Join-Path $fixture 'default-hot-settings.json'
$values|ConvertTo-Json|Set-Content $seed -Encoding utf8
Add-Payload $seed 'default-hot-settings.json'
foreach($name in 'annotations-v4.tsv','index-v4.bin'){
 Add-Payload (Join-Path $env:LOCALAPPDATA ('ZhongriInputMethod/translation_annotations/'+$name)) ('data/translation_annotations/'+$name)
}
$csc=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$launcher=Join-Path $fixture 'ZhongriLauncher.exe'
Invoke-TestRunCommand $run $csc @('/nologo','/target:winexe','/reference:System.Windows.Forms.dll','/reference:System.Web.Extensions.dll',('/out:'+$launcher),(Join-Path $PSScriptRoot 'ZhongriLauncher.cs'))
Add-Payload $launcher 'ZhongriLauncher.exe'
$testLauncher=Join-Path $fixture 'launcher-seed-test'
New-Item -ItemType Directory $testLauncher|Out-Null
Copy-Item $launcher,$seed $testLauncher
$testKey='Software\ZhongriReleaseTests\'+$run.Id
try{
 $key=[Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($testKey);$key.SetValue('CandidateWidth',777,[Microsoft.Win32.RegistryValueKind]::DWord);$key.Dispose()
 Invoke-TestRunCommand $run (Join-Path $testLauncher 'ZhongriLauncher.exe') @('/seed-settings-test',$testKey)
 $key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($testKey)
 if($key.GetValue('CandidateWidth') -ne 777){throw 'Existing preference was overwritten'}
 foreach($name in $values.Keys){if($name -ne 'CandidateWidth' -and $key.GetValue($name) -ne $values[$name]){throw "Seed value mismatch: $name"}}
 $key.Dispose()
}finally{if(Test-Path ('HKCU:\'+$testKey)){Remove-Item -LiteralPath ('HKCU:\'+$testKey)}}
Invoke-TestRunCommand $run $env:ComSpec @('/d','/c',(Join-Path $PSScriptRoot 'build-release-selftest.cmd'))
Add-Payload (Join-Path $env:ZHONGRI_TEST_BUILD_DIR 'RimeCandidateSelfTest.exe') 'RimeCandidateSelfTest.exe'
$versionFile=Join-Path $fixture 'package-version.txt';"$Version-20261019"|Set-Content $versionFile -Encoding utf8
Add-Payload $versionFile 'package-version.txt'
# No provider credentials, identifiers, paths, history or learned stores are
# allowed in preference files. Source code may legitimately name API fields.
foreach($name in $preferences+@('user.yaml')){
 $file=Join-Path $stage ('config/'+$name)
 if(Test-Path $file){$text=[IO.File]::ReadAllText($file);if($text -match 'github_pat_|gh[pousr]_[A-Za-z0-9]{30,}|sk-[A-Za-z0-9]{25,}|"(?:NiuAppId|NiuApiKeyProtected|ApiKey|Token|Password)"\s*:|[A-Z]:\\Users\\'){throw "Privacy gate rejected preference $name"}}
}
$clean=New-TestRunDirectory $run 'clean-package-user'
Copy-Item (Join-Path $stage 'config/*') $clean -Recurse
Invoke-TestRunCommand $run (Join-Path $stage 'RimeCandidateSelfTest.exe') @($stage,$clean,'--deploy','--schema=rime_ice_japanese_mozc','nihao') 1200
$last=$run.Data.Commands[$run.Data.Commands.Count-1]
$deployLog=Join-Path $run.ReportDirectory ('command-'+$run.Data.Commands.Count+'-stdout.log')
if(!(Select-String $deployLog -Pattern '^CANDIDATE_1=你好$' -Quiet)){throw 'Clean package nihao candidate missing'}
$manifest=@{Version=$Version;BaseUrl='https://github.com/swiftol/Rime_Config/releases/download/v2.0.5/ZhongriInputMethod-2.0.5-Setup.exe';BaseSHA256='48D13CE8DA7DCAA6677106992E12316AA51B4927A9F7BD94A07C2818D370BA81';Unpacker=$unpack;Changes=@($changes.ToArray());ValidationRun=$run.Id;SettingCount=$values.Count}
$manifest|ConvertTo-Json -Depth 10|Set-Content (Join-Path $delivery 'manifest.json') -Encoding utf8
Copy-Item (Join-Path $tools 'innounp-2.zip') (Join-Path $delivery 'innounp-2.zip')
Save-TestRunSample $run (Join-Path $delivery 'manifest.json') 'release-manifest.json'
"PASS release overlay: $($changes.Count) changed public files, $($values.Count) hot preferences; clean deployment and non-overwriting seed verified; $delivery"
