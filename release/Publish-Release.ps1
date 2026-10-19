param([Parameter(Mandatory)][string]$Manifest,[switch]$NoPublish,[string]$WorkDirectory=$env:RUNNER_TEMP,[string]$OutputDirectory='')
$ErrorActionPreference='Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
$repo=Split-Path $PSScriptRoot -Parent
$ready=Get-Content -LiteralPath $Manifest -Raw|ConvertFrom-Json
if($ready.Version -notmatch '^\d+\.\d+\.\d+$'){throw 'Invalid release version'}
$inputRoot=Join-Path $PSScriptRoot ('v'+$ready.Version)
$manifestFile=Join-Path $inputRoot 'manifest.json'
if((Get-FileHash $manifestFile).Hash -ne $ready.ManifestSHA256){throw 'Release manifest changed after approval'}
$m=Get-Content $manifestFile -Raw|ConvertFrom-Json
if(!$WorkDirectory){throw 'Explicit isolated working directory required'}
$work=Join-Path $WorkDirectory ('release-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work|Out-Null
if(!$OutputDirectory){$OutputDirectory=Join-Path $work 'out'}
New-Item -ItemType Directory $OutputDirectory -Force|Out-Null
$setup=Join-Path $OutputDirectory ("ZhongriInputMethod-"+$m.Version+'-Setup.exe')
if(Test-Path $setup){throw 'Existing final installer protected'}
$base=Join-Path $work 'base.exe'
Invoke-WebRequest $m.BaseUrl -OutFile $base
if((Get-FileHash $base).Hash -ne $m.BaseSHA256){throw 'Public baseline installer hash mismatch'}
$unpackZip=Join-Path $inputRoot 'innounp-2.zip'
if((Get-FileHash $unpackZip).Hash -ne $m.Unpacker.SHA256){throw 'Unpacker hash mismatch'}
Expand-Archive $unpackZip (Join-Path $work 'unpacker')
$unpacker=Get-ChildItem (Join-Path $work 'unpacker') -Recurse -File -Filter $m.Unpacker.Exe|Select-Object -First 1
& $unpacker.FullName -x -y ('-d'+(Join-Path $work 'base')) $base *> (Join-Path $work 'unpack.log')
if($LASTEXITCODE){throw 'Baseline extraction failed'}
$stage=[IO.Path]::GetFullPath((Join-Path $work ('base/'+$m.Unpacker.ExtractedRootRelative)))
$paths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($change in $m.Changes){
 $relative=[string]$change.Path
 if(!$paths.Add($relative) -or $relative -match '(^|[\\/])\.\.([\\/]|$)|^[\\/]|:'){throw 'Invalid or duplicate payload path'}
 $source=Join-Path $inputRoot ('runtime-overlay/'+$relative)
 $target=[IO.Path]::GetFullPath((Join-Path $stage $relative))
 if(!$target.StartsWith($stage+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Payload escaped stage'}
 if((Get-FileHash $source).Hash -ne $change.SHA256){throw "Overlay hash mismatch: $relative"}
 if($change.BaseSHA256 -and (!(Test-Path $target) -or (Get-FileHash $target).Hash -ne $change.BaseSHA256)){throw "Baseline payload mismatch: $relative"}
 New-Item -ItemType Directory (Split-Path $target) -Force|Out-Null
 Copy-Item -LiteralPath $source -Destination $target -Force
}
foreach($change in $m.Changes){if((Get-FileHash (Join-Path $stage $change.Path)).Hash -ne $change.SHA256){throw 'Final stage mismatch'}}
$private=Get-ChildItem $stage -Recurse -File|Where-Object {$_.Name -match '(?i)(learning|frequency|observations)\.tsv$|\.userdb|\.bak$|\.log$|\.lock$|^installation\.yaml$|^common_phrase_data\.lua$'}
if($private){throw 'Private or diagnostic files in release stage'}
$clean=Join-Path $work 'clean-user';New-Item -ItemType Directory $clean|Out-Null
Copy-Item (Join-Path $stage 'config/*') $clean -Recurse
& (Join-Path $stage 'RimeCandidateSelfTest.exe') $stage $clean --deploy --schema=rime_ice_japanese_mozc nihao *> (Join-Path $work 'clean-deploy.log')
if($LASTEXITCODE -or !(Select-String (Join-Path $work 'clean-deploy.log') -Pattern '^CANDIDATE_1=你好$' -Quiet)){throw 'Clean full package deployment/candidate check failed'}
$iscc=Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'
if(!(Test-Path $iscc)){
 & choco install innosetup --version=6.7.0 --yes --no-progress
 if($LASTEXITCODE){throw 'Inno Setup installation failed'}
}
& $iscc /Q ("/DProductVersion="+$m.Version) ("/DPackageRoot="+$stage) ("/O"+$OutputDirectory) (Join-Path $PSScriptRoot 'Installer.iss')
if($LASTEXITCODE -or !(Test-Path $setup)){throw 'Installer compilation failed'}
$sha=(Get-FileHash $setup).Hash
"$sha  $([IO.Path]::GetFileName($setup))"|Set-Content (Join-Path $OutputDirectory 'SHA256SUMS.txt') -Encoding utf8
$sourceZip=Join-Path $OutputDirectory ('ZhongriInputMethod-'+$m.Version+'-Source.zip')
& git -C $repo archive --format=zip ('--output='+$sourceZip) HEAD
if($LASTEXITCODE){throw 'Corresponding source archive failed'}
$notes=Join-Path $repo ('docs/releases/v'+$m.Version+'.md')
if(!(Test-Path $notes)){throw 'Release announcement missing'}
if($NoPublish){"PASS built verified installer without publishing: $setup SHA256=$sha";return}
if($env:GITHUB_REPOSITORY -ne 'swiftol/Rime_Config' -or !$env:GH_TOKEN){throw 'Publishing only allowed in the authorized repository workflow'}
$tag='v'+$m.Version
& gh release view $tag --repo $env:GITHUB_REPOSITORY *> (Join-Path $work 'existing-release.log')
if($LASTEXITCODE -eq 0){throw 'Existing release protected; never replace its assets automatically'}
$notesFile=Join-Path $work 'release-notes.md'
([IO.File]::ReadAllText($notes)+"`n`n安装包 SHA-256：``$sha`` `n")|Set-Content $notesFile -Encoding utf8
& gh release create $tag $setup (Join-Path $OutputDirectory 'SHA256SUMS.txt') $sourceZip --repo $env:GITHUB_REPOSITORY --target $env:GITHUB_SHA --title ('中日输入法 '+$m.Version) --notes-file $notesFile --draft
if($LASTEXITCODE){throw 'Draft release creation/upload failed'}
$release=(& gh release view $tag --repo $env:GITHUB_REPOSITORY --json assets |ConvertFrom-Json)
if($LASTEXITCODE -or @($release.assets).Count -ne 3){throw 'Uploaded asset verification failed; release remains draft'}
& gh release edit $tag --repo $env:GITHUB_REPOSITORY --draft=false --latest
if($LASTEXITCODE){throw 'Release remains draft; publication failed'}
"PUBLISHED=https://github.com/$env:GITHUB_REPOSITORY/releases/tag/$tag"
