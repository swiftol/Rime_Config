# Dot-source only. No automatic scan/delete of legacy directories.
Set-StrictMode -Version 3
if (-not (Get-Variable ZhongriManagedRuns -Scope Global -ErrorAction SilentlyContinue)) {
    $global:ZhongriManagedRuns=[Collections.Generic.List[object]]::new()
}

function Protect-TestEvidenceText([string]$Text) {
    $Text=[regex]::Replace($Text,'(?i)(authorization\s*[:=]\s*(?:bearer\s+)?)[^\s"'']+','$1[REDACTED]')
    $Text=[regex]::Replace($Text,'(?i)((?:api[_-]?key|access[_-]?token|refresh[_-]?token|password|secret)["'']?\s*[=:]\s*["'']?)[^\s,"'']+','$1[REDACTED]')
    $Text=[regex]::Replace($Text,'(?i)((?:--?|/)(?:api[_-]?key|access[_-]?token|password|secret)\s+["'']?)[^\s,"'']+','$1[REDACTED]')
    return [regex]::Replace($Text,'\bsk-[A-Za-z0-9_-]{12,}\b','[REDACTED]')
}
function Assert-TestPlainPath([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    $probe=$full
    while($probe) {
        if(Test-Path -LiteralPath $probe) {
            $item=Get-Item -LiteralPath $probe -Force
            if($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Directory link/reparse point excluded: $probe" }
        }
        $parent=Split-Path $probe -Parent
        if($parent -eq $probe) { break }; $probe=$parent
    }
    return $full
}
function ConvertTo-SafeTestEvidence($Value) {
    if($Value -is [Collections.IDictionary]) {
        $copy=[ordered]@{}
        foreach($key in $Value.Keys) {
            $copy[$key]=if($key -match '(?i)^(api[-_]?key|password|secret|access[-_]?token|refresh[-_]?token|authorization)$') { '[REDACTED]' } else { ConvertTo-SafeTestEvidence $Value[$key] }
        }
        return $copy
    }
    if($Value -is [string]) { return Protect-TestEvidenceText $Value }
    if($Value -is [Collections.IEnumerable]) {
        $copy=[Collections.Generic.List[object]]::new();$redactNext=$false
        foreach($item in $Value) {
            if($redactNext) { $copy.Add('[REDACTED]');$redactNext=$false;continue }
            $copy.Add((ConvertTo-SafeTestEvidence $item))
            if($item -is [string] -and $item -match '(?i)^[-/]+(api[-_]?key|password|secret|access[-_]?token|authorization)$') { $redactNext=$true }
        }
        return ,$copy.ToArray()
    }
    return $Value
}
function Write-TestAtomic([string]$Path,[string]$Text) {
    $null=Assert-TestPlainPath $Path
    $scratch=$Path+'.writing-'+[guid]::NewGuid().ToString('N')
    [IO.File]::WriteAllText($scratch,$Text,[Text.UTF8Encoding]::new($false))
    [IO.File]::Move($scratch,$Path,$true)
}
function Update-TestReportIndex([string]$Project) {
    $root=Assert-TestPlainPath (Join-Path $Project 'outputs/test-reports')
    $id=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($root))).Substring(0,20)
    $mutex=[Threading.Mutex]::new($false,'Local\ZhongriTestReports-'+$id)
    $held=$false
    try {
        try { $held=$mutex.WaitOne(60000) } catch [Threading.AbandonedMutexException] { $held=$true }
        if(!$held) { throw 'Report index locked by another writer' }
        $lines=[Collections.Generic.List[string]]::new()
        $lines.Add('# 测试报告索引');$lines.Add('')
        $lines.Add('按时间查询报告。已清理的环境不能视为仍在；重新运行须生成新的报告。旧 outputs 的历史报告不冒充本次运行。');$lines.Add('')
        $lines.Add('| 时间 | 测试 | 状态 | 报告 | 复现位置 | 临时环境清理 |')
        $lines.Add('| --- | --- | --- | --- | --- | --- |')
        # Reports live exactly one level below this root. Recursing through
        # every retained source/log snapshot makes concurrent writers time out.
        # Never follow a report-directory link to discover an index entry.
        $reportFiles=@(foreach($directory in Get-ChildItem -LiteralPath $root -Directory) {
            # The root and its ancestors were checked above. Each entry is
            # an immediate child, so validate its own attributes instead of
            # rewalking the same ancestor chain hundreds of times under lock.
            if($directory.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Report directory link refused'}
            $resultPath=Join-Path $directory.FullName 'result.json'
            if(Test-Path -LiteralPath $resultPath -PathType Leaf) { Get-Item -LiteralPath $resultPath }
        })
        foreach($file in @($reportFiles | Sort-Object FullName)) {
            if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Report file link refused'}
            # Indexing needs eight scalar fields, not every source/hash/log
            # object in every historical manifest. Avoid building huge PS
            # object graphs while excluding concurrent report writers.
            $doc=[System.Text.Json.JsonDocument]::Parse([IO.File]::ReadAllText($file.FullName))
            try {
            $json=$doc.RootElement;$cleanup=$json.GetProperty('Cleanup')
            $state=$cleanup.GetProperty('State').GetString();$reason=$cleanup.GetProperty('Reason').GetString()
            $started=$json.GetProperty('Started').GetString();$name=$json.GetProperty('Name').GetString()
            $status=$json.GetProperty('Status').GetString();$temp=$json.GetProperty('TempDirectory').GetString()
            $relative=[IO.Path]::GetRelativePath($root,$file.DirectoryName).Replace('\','/')
            $place=if($state -eq 'Deleted') { '命令、输入来源与哈希见报告（现场已清理）' } else { $temp.Replace('|','\|') }
            $time=([datetimeoffset]$started).ToString('o')
            $lines.Add("| $time | $name | $status | [README]($relative/README.md) | $place | ${state}: $($reason.Replace('|','/')) |")
            }finally{$doc.Dispose()}
        }
        Write-TestAtomic (Join-Path $root 'INDEX.md') ($lines -join "`n")
    } finally { if($held) { $mutex.ReleaseMutex() };$mutex.Dispose() }
}
function Save-TestRunReport($Run) {
    $null=Assert-TestPlainPath $Run.ReportDirectory
    $data=$Run.Data
    $safe=ConvertTo-SafeTestEvidence $data
    $json=Protect-TestEvidenceText ($safe | ConvertTo-Json -Depth 30)
    Write-TestAtomic (Join-Path $Run.ReportDirectory 'result.json') $json
    $readme=@"
# $($data.Name)

时间：$($data.Started) → $($data.Ended)；状态：$($data.Status)

目的：$($data.Purpose)

预期：$($data.Expected)

实际 / 失败详情：$($data.Actual)

## 复现

在项目根目录执行（参数已脱敏）：

    $($data.Reproduce)

依赖：$($data.Dependencies -join '; ')

输入来源、配置 / 模型 SHA256、代码提交与未提交文件哈希、命令与退出码见 [result.json](result.json)。使用原始来源重新取得模型或由保留源码生成，不能从报告推断已重新执行。stdout/stderr 和明确登记的小型样本保存在本报告目录。

## 资源

原临时位置：$($data.TempDirectory)

登记所有权：仅本次新建根目录；共享输入、用户数据、备份和目录链接不归本次清理。

清理：$($data.Cleanup.State)；原因：$($data.Cleanup.Reason)

删除字节数：$($data.Cleanup.DeletedBytes)；遗留：$($data.Cleanup.Remaining -join '; ')

报告保留，临时环境是否仍在以清理记录为准。失败 / 中断现场不自动删除。
"@
    if($data.Contains('Resolution')) {
        $readme+=(@('','## 修复与复测','',"修复：$($data.Resolution.Fix)",'',"复测报告：$($data.Resolution.Retest)") -join [Environment]::NewLine)
    }
    foreach($secret in @($data.Parameters.Keys | Where-Object { $_ -match '(?i)api[-_]?key|password|secret|token' })) {
        $literal=[string]$data.Parameters[$secret]
        if($literal) { $readme=$readme.Replace($literal,'[REDACTED]') }
    }
    Write-TestAtomic (Join-Path $Run.ReportDirectory 'README.md') (Protect-TestEvidenceText $readme)
    # Verify durable evidence before permitting removal.
    $verify=Get-Content -LiteralPath (Join-Path $Run.ReportDirectory 'result.json') -Raw | ConvertFrom-Json
    if($verify.Id -ne $Run.Id -or !(Test-Path -LiteralPath (Join-Path $Run.ReportDirectory 'stdout.log')) -or !(Test-Path -LiteralPath (Join-Path $Run.ReportDirectory 'stderr.log'))) { throw 'Incomplete report; cleanup forbidden' }
    Update-TestReportIndex $Run.Project
}
function Add-TestRunInput($Run,[string]$Path,[string]$Role='input',[string]$Recover='Use retained original at Source; never remove source') {
    $full=[IO.Path]::GetFullPath($Path)
    if(!(Test-Path -LiteralPath $full)) { throw "Required input missing: $full" }
    $null=Assert-TestPlainPath $full
    $files=if((Get-Item -LiteralPath $full).PSIsContainer) { @(Get-ChildItem -LiteralPath $full -File -Recurse -Force) } else { @(Get-Item -LiteralPath $full) }
    foreach($file in $files) {
        $null=Assert-TestPlainPath $file.FullName
        $Run.Data.Inputs.Add([ordered]@{Source=$file.FullName;Role=$Role;Bytes=$file.Length;LastWriteUtc=$file.LastWriteTimeUtc.ToString('o');SHA256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash;Recover=$Recover})
    }
}
function Assert-TestCopySpace($Run,[string[]]$Sources) {
    $bytes=0L
    foreach($source in $Sources) {
        $null=Assert-TestPlainPath $source
        foreach($file in @(Get-ChildItem -LiteralPath $source -File -Recurse -Force)) { $bytes+=$file.Length }
    }
    $drive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($Run.TempDirectory))
    $retainedBytes=0L
    foreach($resultFile in @(Get-ChildItem -LiteralPath (Join-Path $Run.Project 'outputs/test-reports') -File -Filter result.json -Recurse)) {
        $previous=Get-Content -LiteralPath $resultFile.FullName -Raw | ConvertFrom-Json
        if($previous.Id -eq $Run.Id -or $previous.Status -notin 'Failed','Interrupted' -or !(Test-Path -LiteralPath $previous.TempDirectory)) { continue }
        $null=Assert-TestPlainPath $previous.TempDirectory
        foreach($file in @(Get-ChildItem -LiteralPath $previous.TempDirectory -File -Recurse -Force)) { $retainedBytes+=$file.Length }
    }
    $Run.Data.SpaceChecks.Add(@{Time=(Get-Date).ToString('o');EstimatedCopyBytes=$bytes;FreeBytes=$drive.AvailableFreeSpace;ReserveBytes=1GB})
    if($retainedBytes -gt 4GB) { throw 'Registered unresolved failed/interrupted environments exceed 4 GiB. Inspect INDEX.md and resolve them before another large copy; none were deleted.' }
    if($drive.AvailableFreeSpace -lt $bytes+1GB) { throw "Insufficient disk space for test copy: need $bytes bytes plus 1 GiB reserve; no large copy started" }
}
function Save-TestRunSample($Run,[string]$Path,[string]$Name) {
    $null=Assert-TestPlainPath $Path
    $file=Get-Item -LiteralPath $Path
    if($file.PSIsContainer -or $file.Length -gt 1MB -or $Name -match '[/\\]' -or $Name -match '(?i)token|secret|credential|password') { throw 'Only explicitly selected small non-secret samples allowed' }
    Write-TestAtomic (Join-Path $Run.ReportDirectory $Name) (Protect-TestEvidenceText ([IO.File]::ReadAllText($file.FullName)))
    $Run.Data.Outputs.Add(@{Path=(Join-Path $Run.ReportDirectory $Name);Source=$file.FullName;Role='retained small evidence sample (secrets redacted)'})
}
function Get-TestGitExecutable {
    $command=Get-Command git -CommandType Application -ErrorAction SilentlyContinue
    if($command) { return $command.Source }
    # Desktop shortcuts do not inherit Codex's PATH. Use an existing bundled
    # sibling or standard installation explicitly; never alter system PATH.
    foreach($candidate in @((Join-Path (Split-Path $PSHOME -Parent) 'git/cmd/git.exe'),(Join-Path $env:ProgramFiles 'Git/cmd/git.exe'))) {
        if(Test-Path -LiteralPath $candidate -PathType Leaf) { return [IO.Path]::GetFullPath($candidate) }
    }
    throw 'Cannot locate Git for required source evidence; no test resources or registration writes started.'
}
function New-TestRun([string]$Script,[hashtable]$Parameters,[string]$Purpose,[string]$Expected) {
    $project=Split-Path $PSScriptRoot -Parent
    $gitExecutable=Get-TestGitExecutable
    $name=[IO.Path]::GetFileNameWithoutExtension($Script) -replace '[^a-zA-Z0-9-]','-'
    $id=(Get-Date -Format 'yyyyMMdd-HHmmssfff')+'-'+$name+'-'+[guid]::NewGuid().ToString('N').Substring(0,12)
    $temp=Assert-TestPlainPath (Join-Path $project "work/test-runs/$id")
    $report=Assert-TestPlainPath (Join-Path $project "outputs/test-reports/$id")
    if((Test-Path -LiteralPath $temp) -or (Test-Path -LiteralPath $report)) { throw 'Refuse pre-existing run directory' }
    $null=New-Item -ItemType Directory -Path $temp,$report
    $owner=[ordered]@{Id=$id;Project=$project;Path=$temp;PID=$PID;ProcessStart=(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')}
    Write-TestAtomic (Join-Path $temp '.test-run-owner.json') ($owner | ConvertTo-Json)
    $lease=[IO.File]::Open((Join-Path $temp '.test-run-lease'),[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    foreach($log in 'stdout.log','stderr.log') { Write-TestAtomic (Join-Path $report $log) '' }
    $version=[Collections.Generic.List[object]]::new()
    foreach($repo in @($project,(Join-Path $project 'work/Rime_Config'),(Join-Path $project 'work/zhongri-weasel'))) {
        if(Test-Path -LiteralPath (Join-Path $repo '.git')) {
            $commit=(& $gitExecutable -C $repo rev-parse HEAD 2>$null | Out-String).Trim()
            $status=(& $gitExecutable -C $repo status --porcelain --untracked-files=no 2>$null | Out-String).Trim()
            $changes=@()
            foreach($relative in @(& $gitExecutable -C $repo diff --name-only HEAD 2>$null)) {
                $changed=Join-Path $repo $relative
                if(Test-Path -LiteralPath $changed -PathType Leaf) { $changes+=@{Path=$relative;SHA256=(Get-FileHash -LiteralPath $changed).Hash} }
            }
            $codePaths=@($changes | Where-Object { $_.Path -match '\.(lua|cpp|hpp|h|cs|xaml|ps1|cmd|yaml|csproj|props)$' -and $_.Path -notmatch '(?i)credential|secret|common_phrase_data' } | ForEach-Object Path)
            if($codePaths.Count) {
                $diff=(& $gitExecutable -C $repo diff HEAD -- @codePaths 2>$null | Out-String)
                $patchName='code-changes-'+[IO.Path]::GetFileName($repo)+'.patch'
                Write-TestAtomic (Join-Path $report $patchName) (Protect-TestEvidenceText $diff)
            }
            foreach($relative in @(& $gitExecutable -C $repo ls-files --others --exclude-standard -- '*.lua' '*.cpp' '*.h' 2>$null)) {
                if($relative -match '(?i)credential|secret|common_phrase_data') { continue }
                $file=Get-Item -LiteralPath (Join-Path $repo $relative)
                if($file.Length -le 1MB) {
                    $snapshot='untracked-'+[IO.Path]::GetFileName($repo)+'-'+($relative -replace '[/\\]','_')
                    Write-TestAtomic (Join-Path $report $snapshot) (Protect-TestEvidenceText ([IO.File]::ReadAllText($file.FullName)))
                }
            }
            $version.Add(@{Repository=$repo;Commit=$commit;Uncommitted=$status;ChangedFileHashes=$changes})
        }
    }
    $normalized=@{}
    foreach($p in $Parameters.Keys) { $normalized[$p]=if($Parameters[$p] -is [Management.Automation.SwitchParameter]) { [bool]$Parameters[$p] } else { $Parameters[$p] } }
    $parameterJson=($normalized | ConvertTo-Json -Compress -Depth 8).Replace("'","''")
    $reproduce='pwsh -NoProfile -File "'+(Join-Path $PSScriptRoot 'invoke-project-test.ps1')+'" -Script "'+$Script+'" -ParametersJson '+"'"+$parameterJson+"'"
    $run=[pscustomobject]@{Id=$id;Project=$project;TempDirectory=$temp;ReportDirectory=$report;Lease=$lease;Script=$Script;ProcessHandles=[Collections.Generic.Dictionary[int,Diagnostics.Process]]::new();Data=[ordered]@{
        Id=$id;Name=$name;Started=(Get-Date).ToString('o');Ended='';Status='Running';Purpose=$Purpose;Expected=$Expected;Actual='Not completed';
        TempDirectory=$temp;RegisteredTemporaryRoots=@($temp);Owner=$owner;CodeVersion=$version;Inputs=[Collections.Generic.List[object]]::new();
        Commands=[Collections.Generic.List[object]]::new();Assertions=[Collections.Generic.List[string]]::new();Processes=[Collections.Generic.List[object]]::new();ExternalFiles=[Collections.Generic.List[object]]::new();SpaceChecks=[Collections.Generic.List[object]]::new();Outputs=[Collections.Generic.List[object]]::new();EvidenceComplete=$true;
        Dependencies=@("PowerShell $($PSVersionTable.PSVersion)","$([Environment]::OSVersion.VersionString)",[Runtime.InteropServices.RuntimeInformation]::FrameworkDescription,'Other exact executable/source/model paths and SHA256 are in Inputs/Commands and retained build script');Reproduce=$reproduce;Parameters=$normalized;
        OriginalTemp=$env:TEMP;Cleanup=[ordered]@{State='Pending';Reason='Test has not completed; never infer Passed';DeletedBytes=0;Remaining=@($temp)}
    }}
    Add-TestRunInput $run $Script 'test source' 'Retained project source; use recorded version/hash'
    Add-TestRunInput $run $gitExecutable 'explicit Git used for source evidence (desktop PATH independent)'
    Add-TestRunInput $run $PSCommandPath 'lifecycle source' 'Retained project source; use recorded version/hash'
    Save-TestRunSample $run $Script 'test-source.ps1'
    Save-TestRunSample $run (Join-Path $PSScriptRoot 'TestRunLifecycle.ps1') 'lifecycle-source.ps1'
    if($name -match 'japanese|msime|azik|learning' -and (Test-Path -LiteralPath (Join-Path $project 'tools/japanese-input-table-engine-test.cpp'))) {
        Add-TestRunInput $run (Join-Path $project 'tools/japanese-input-table-engine-test.cpp') 'shared test executable source' 'Source snapshot retained; original shared executable hash distinguishes it from a new build'
        Save-TestRunSample $run (Join-Path $project 'tools/japanese-input-table-engine-test.cpp') 'shared-native-test-source.cpp'
    }
    if($name -match 'japanese|installed|msime|candidate|annotation|translation|chinese|azik|learning|routing') {
        foreach($source in @('H:/ZhongriInputMethod/ZhongriServer.exe','H:/ZhongriInputMethod/mozc/dict_japanese.dat',(Join-Path $env:APPDATA 'ZhongriInputMethod/japanese_input_table.tsv'),(Join-Path $env:APPDATA 'ZhongriInputMethod/japanese_input_table.state'))) {
            if(Test-Path -LiteralPath $source -PathType Leaf) { Add-TestRunInput $run $source 'protected installed runtime / model / baseline key configuration' 'Retain installed source plus static model manifest/license; user input table remains in real profile, never removed' }
        }
        foreach($meta in @(Get-ChildItem -LiteralPath 'H:/ZhongriInputMethod/mozc' -File -ErrorAction SilentlyContinue | Where-Object Name -match '(?i)manifest|license|notice')) {
            Add-TestRunInput $run $meta.FullName 'model/runtime source, version and license metadata' 'Retain bundled upstream manifest/license alongside the original static model'
        }
    }
    Save-TestRunReport $run
    return $run
}
function Test-ManagedRunActive([string]$Script) {
    return $global:ZhongriManagedRuns.Count -gt 0 -and $global:ZhongriManagedRuns[$global:ZhongriManagedRuns.Count-1].Script -eq $Script
}
function Get-ManagedTestRun { return $global:ZhongriManagedRuns[$global:ZhongriManagedRuns.Count-1] }
function New-TestRunDirectory($Run,[string]$Name) {
    if($Name -notmatch '^[a-zA-Z0-9_.-]+$' -or $Name -in '.','..') { throw 'Unsafe temporary child name' }
    $path=Join-Path $Run.TempDirectory $Name
    if(Test-Path -LiteralPath $path) { throw 'Temporary directory already exists' }
    $null=New-Item -ItemType Directory -Path $path
    return $path
}
function Start-TestRunProcess($Run,[string]$FilePath,[string[]]$ArgumentList=@(),[string]$WorkingDirectory='',
    [ValidateSet('Hidden','Normal','Minimized','Maximized')][string]$WindowStyle='Hidden',[switch]$PassThru,[switch]$Wait) {
    if(!(Test-Path -LiteralPath $FilePath)) { $FilePath=(Get-Command $FilePath -ErrorAction Stop).Source }
    if([IO.Path]::GetFileName($FilePath) -ieq 'runas.exe') { throw 'Detached launcher ownership cannot be proven. Use a directly tracked test executable; existing user processes were not touched.' }
    Add-TestRunInput $Run $FilePath 'background executable' 'Retained runtime or reproducible build from project source'
    $start=@{FilePath=$FilePath;WindowStyle=$WindowStyle;PassThru=$true}
    if($ArgumentList.Count) { $start.ArgumentList=$ArgumentList }
    if($WorkingDirectory) { $start.WorkingDirectory=$WorkingDirectory }
    $process=Start-Process @start
    $Run.ProcessHandles[$process.Id]=$process
    $Run.Data.Processes.Add(@{PID=$process.Id;Start=$process.StartTime.ToUniversalTime().ToString('o');File=[IO.Path]::GetFullPath($FilePath)})
    $Run.Data.Commands.Add(@{File=$FilePath;Arguments=$ArgumentList;Started=(Get-Date).ToString('o');ExitCode=$null;PID=$process.Id;Kind='owned background process'})
    Save-TestRunReport $Run
    if($Wait) {
        if(!$process.WaitForExit(600000)) { throw 'Owned background test exceeded 600 seconds; reproduction retained' }
        $Run.Data.Processes[$Run.Data.Processes.Count-1].Ended=$process.ExitTime.ToUniversalTime().ToString('o')
    }
    return $process
}
function Stop-TestOwnedProcess {
    [CmdletBinding()]
    param([Parameter(ValueFromPipeline)]$InputObject,[Alias('Id')][int[]]$ProcessId=@(),[switch]$Force)
    process {
        $run=Get-ManagedTestRun
        $ids=@($ProcessId)
        if($null -ne $InputObject) { $ids+= $InputObject.Id }
        foreach($targetId in $ids) {
            $registered=@($run.Data.Processes | Where-Object PID -eq $targetId)
            if(!$registered.Count) { Write-Verbose "Unowned PID $targetId was not stopped";continue }
            $live=Get-Process -Id $targetId -ErrorAction SilentlyContinue
            if($live -and $live.StartTime.ToUniversalTime().ToString('o') -eq $registered[0].Start) {
                $null=$live.Handle # Retain an OS handle before stopping, so exit metadata remains available.
                Stop-Process -Id $targetId -ErrorAction Stop
                if(!$live.WaitForExit(10000)) { throw "Owned process $targetId did not exit" }
                $exitTime=$live.ExitTime
                $registered[0].Ended=if($null -ne $exitTime) { $exitTime.ToUniversalTime().ToString('o') } else { (Get-Date).ToUniversalTime().ToString('o') }
                $registered[0].EndedSource=if($null -ne $exitTime) {'OS exit time'} else {'Observed exited; exact OS time unavailable'}
            }
        }
    }
}
function Stop-TestRunProcesses($Run) {
    if(!$Run.Data.Processes.Count) { return }
    # Discover descendants before ending parents, never stop by name/path alone.
    $all=@(Get-CimInstance Win32_Process)
    $owned=@($Run.Data.Processes)
    foreach($root in $owned) {
        if($Run.ProcessHandles.ContainsKey([int]$root.PID)) {
            $handle=$Run.ProcessHandles[[int]$root.PID]
            if($handle.HasExited) { $root.Ended=$handle.ExitTime.ToUniversalTime().ToString('o');$root.ExitCode=$handle.ExitCode }
        }
        $live=Get-Process -Id $root.PID -ErrorAction SilentlyContinue
        if((!$live -or $live.StartTime.ToUniversalTime().ToString('o') -ne $root.Start) -and !$root.Contains('Ended')) {
            throw "Owned process exited before descendant ownership could be confirmed (PID $($root.PID)); keep environment"
        }
    }
    for($round=0;$round -lt 8;$round++) {
        $added=$false
        foreach($p in $all) {
            if($owned.PID -contains [int]$p.ProcessId) { continue }
            $parent=@($owned | Where-Object { $_.PID -eq [int]$p.ParentProcessId })
            if($parent.Count -and $null -eq $p.CreationDate) { throw "Child creation time unavailable for PID $($p.ProcessId); ownership cannot be confirmed" }
            if($parent.Count -and $p.CreationDate.ToUniversalTime() -ge [datetime]$parent[0].Start) {
                $liveParent=Get-Process -Id $parent[0].PID -ErrorAction SilentlyContinue
                $sameParent=$liveParent -and $liveParent.StartTime.ToUniversalTime().ToString('o') -eq $parent[0].Start
                $exactEnd=!$parent[0].Contains('EndedSource') -or $parent[0].EndedSource -eq 'OS exit time'
                $knownLifetime=$exactEnd -and $parent[0].Contains('Ended') -and $p.CreationDate.ToUniversalTime() -le [datetime]$parent[0].Ended
                if(!$sameParent -and !$knownLifetime) { continue }
                $child=Get-Process -Id $p.ProcessId -ErrorAction SilentlyContinue
                if($child) { $owned+=@{PID=[int]$p.ProcessId;Start=$child.StartTime.ToUniversalTime().ToString('o');File=$p.ExecutablePath};$added=$true }
            }
        }
        if(!$added) { break }
    }
    foreach($p in @($owned | Sort-Object Start)) {
        $live=Get-Process -Id $p.PID -ErrorAction SilentlyContinue
        if($live -and $live.StartTime.ToUniversalTime().ToString('o') -eq $p.Start) {
            $null=$live.Handle
            Stop-Process -Id $live.Id -ErrorAction Stop
            if(!$live.WaitForExit(10000)) { throw "Own process still running: $($p.PID)" }
            $exitTime=$live.ExitTime
            $p.Ended=if($null -ne $exitTime) { $exitTime.ToUniversalTime().ToString('o') } else { (Get-Date).ToUniversalTime().ToString('o') }
            $p.EndedSource=if($null -ne $exitTime) {'OS exit time'} else {'Observed exited; exact OS time unavailable'}
            if($null -ne $live.ExitCode) { $p.ExitCode=$live.ExitCode }
        }
    }
    foreach($command in $Run.Data.Commands) {
        if($command.Contains('Kind') -and $command.Kind -eq 'owned background process') {
            $processEntry=@($owned | Where-Object PID -eq $command.PID)
            $command.ExitCode=if($processEntry.Count -and $processEntry[0].Contains('ExitCode')) { $processEntry[0].ExitCode } else { 'Exited/stopped after assertions; code unavailable, not a pass assertion' }
        }
    }
}
function Initialize-TestRimeFixture($Run,[string]$Destination,[string]$Seed) {
    if(!$Destination.StartsWith($Run.TempDirectory+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -and $Destination -ne $Run.TempDirectory) { throw 'Fixture is not task-owned' }
    Assert-TestCopySpace $Run @((Join-Path $Seed 'build'),(Join-Path $Seed 'lua'),(Join-Path $Seed 'opencc'))
    foreach($folder in 'build','lua','opencc') {
        $source=Join-Path $Seed $folder
        Add-TestRunInput $Run $source 'necessary shared test seed' 'Retain this seed; rebuild compiled dictionaries from recorded Rime sources/runtime, not from personal user databases'
        Copy-Item -LiteralPath $source -Destination $Destination -Recurse
    }
    Add-TestRunInput $Run (Join-Path $Seed 'rime.lua') 'seed config'
    Copy-Item -LiteralPath (Join-Path $Seed 'rime.lua') -Destination $Destination
    $lua=Join-Path $Run.Project 'work/Rime_Config/lua'
    Add-TestRunInput $Run $lua 'source Lua' 'Retained Rime_Config source, commit/modified file hashes recorded'
    Get-ChildItem -LiteralPath $lua -File | Copy-Item -Destination (Join-Path $Destination 'lua') -Force
}
function Remove-OwnedTestRunDirectory($Run) {
    $result=[ordered]@{State='Skipped';Reason='';DeletedBytes=0;Remaining=@($Run.TempDirectory)}
    $beforeRootBytes=0L;$externalRemovedBytes=0L;$attempted=$false
    try {
        $root=Assert-TestPlainPath $Run.TempDirectory
        $boundary=Assert-TestPlainPath (Join-Path $Run.Project 'work/test-runs')
        if([IO.Path]::GetDirectoryName($root) -ne $boundary -or $root -ne $Run.Data.RegisteredTemporaryRoots[0]) { throw 'Root outside exact registered temporary scope' }
        $owner=Get-Content -LiteralPath (Join-Path $root '.test-run-owner.json') -Raw | ConvertFrom-Json
        if($owner.Id -ne $Run.Id -or $owner.Path -ne $root -or $owner.Project -ne $Run.Project) { throw 'Owner manifest mismatch' }
        $original=Get-Process -Id $owner.PID -ErrorAction SilentlyContinue
        if($original -and $original.Id -ne $PID -and $original.StartTime.ToUniversalTime() -eq ([datetime]$owner.ProcessStart).ToUniversalTime()) { throw 'Original test owner still running; retained' }
        $items=@(Get-ChildItem -LiteralPath $root -Force -Recurse)
        if(@($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Directory links/reparse points retained; no recursive removal' }
        $busy=@(Get-CimInstance Win32_Process | Where-Object { $_.ProcessId -ne $PID -and $_.CommandLine -and $_.CommandLine.IndexOf($root,[StringComparison]::OrdinalIgnoreCase) -ge 0 })
        if($busy.Count) { throw "A process references this environment; PIDs: $($busy.ProcessId -join ',')" }
        $bytes=0L
        foreach($file in @($items | Where-Object { !$_.PSIsContainer })) {
            $null=Assert-TestPlainPath $file.FullName
            $lock=[IO.File]::Open($file.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
            $lock.Dispose();$bytes+=$file.Length
        }
        $beforeRootBytes=$bytes
        foreach($external in $Run.Data.ExternalFiles) {
            $path=Assert-TestPlainPath $external.Path
            if([IO.Path]::GetDirectoryName($path) -ne $external.Parent -or [IO.Path]::GetFileName($path).IndexOf($Run.Id,[StringComparison]::Ordinal) -lt 0) { throw 'External IPC file does not match exact declared task ownership' }
            if(Test-Path -LiteralPath $path) {
                $file=Get-Item -LiteralPath $path
                if($file.PSIsContainer -or $external.SHA256 -ne (Get-FileHash -LiteralPath $path).Hash) { throw 'External IPC file changed or ownership unclear' }
                $lock=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None);$lock.Dispose()
            }
        }
        Write-TestAtomic (Join-Path $Run.ReportDirectory 'cleanup.json') (@{State='Removing';Path=$root;ExternalFiles=$Run.Data.ExternalFiles;PlannedBytes=$bytes;Time=(Get-Date).ToString('o')} | ConvertTo-Json -Depth 8)
        $attempted=$true
        foreach($external in $Run.Data.ExternalFiles) {
            if(Test-Path -LiteralPath $external.Path) {
                $amount=(Get-Item -LiteralPath $external.Path).Length
                Remove-Item -LiteralPath $external.Path -Force -ErrorAction Stop
                if(Test-Path -LiteralPath $external.Path) { throw 'External file not deleted' }
                $bytes+=$amount
                $externalRemovedBytes+=$amount
            }
        }
        # PowerShell only; exact validated task-owned root, no shell expansion.
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction Stop
        if(Test-Path -LiteralPath $root) { throw 'Removal left a directory behind' }
        $result.State='Deleted';$result.Reason='Passed, evidence saved, processes exited, ownership/path/link/lock checks passed';$result.DeletedBytes=$bytes;$result.Remaining=@()
    } catch {
        $result.Reason=$_.Exception.Message
        if(!(Test-Path -LiteralPath $Run.TempDirectory)) { $result.Remaining=@() }
        elseif($attempted -and !((Get-Item -LiteralPath $Run.TempDirectory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            $left=@(Get-ChildItem -LiteralPath $Run.TempDirectory -Force -File -Recurse)
            $leftBytes=(@($left) | Measure-Object Length -Sum).Sum
            $result.DeletedBytes=[math]::Max(0,$beforeRootBytes-$leftBytes)+$externalRemovedBytes
            $result.Remaining=@($Run.TempDirectory)+@($left | Select-Object -First 100 -ExpandProperty FullName)
            $result.RemainingFileCount=$left.Count
        }
        $result.Remaining+=@($Run.Data.ExternalFiles | Where-Object { Test-Path -LiteralPath $_.Path } | ForEach-Object Path)
    }
    Write-TestAtomic (Join-Path $Run.ReportDirectory 'cleanup.json') ($result | ConvertTo-Json -Depth 8)
    return $result
}
function Complete-TestRun($Run,[string]$Status,[string]$Actual) {
    $Run.Data.Status=$Status;$Run.Data.Actual=$Actual;$Run.Data.Ended=(Get-Date).ToString('o')
    try { Stop-TestRunProcesses $Run } catch { $Run.Data.Cleanup.Reason=$_.Exception.Message;$Run.Data.Status='Failed';$Run.Data.Actual+='; '+$_.Exception.Message }
    foreach($handle in $Run.ProcessHandles.Values) { $handle.Dispose() }
    if($Run.Lease) { $Run.Lease.Dispose();$Run.Lease=$null }
    $Run.Data.Cleanup.State='Retained';$Run.Data.Cleanup.Reason='Failed/interrupted reproduction retained'
    if($Run.Data.Status -eq 'Passed') { $Run.Data.Cleanup.Reason='Evidence saved before cleanup; validation pending' }
    try {
        foreach($external in $Run.Data.ExternalFiles) {
            if(Test-Path -LiteralPath $external.Path -PathType Leaf) { $external.SHA256=(Get-FileHash -LiteralPath $external.Path).Hash }
        }
        foreach($log in @(Get-ChildItem -LiteralPath $Run.TempDirectory -File -Filter '*.log' -Recurse -ErrorAction Stop)) {
            $null=Assert-TestPlainPath $log.FullName
            if($log.Length -gt 1MB) { $Run.Data.EvidenceComplete=$false;$Run.Data.Actual+='; oversized log retained in environment: '+$log.FullName;continue }
            $name='log-'+([IO.Path]::GetRelativePath($Run.TempDirectory,$log.FullName) -replace '[/\\]','_')
            Save-TestRunSample $Run $log.FullName $name
        }
        Save-TestRunReport $Run
        $reportSize=(@(Get-ChildItem -LiteralPath $Run.ReportDirectory -File -Recurse) | Measure-Object Length -Sum).Sum
        if($reportSize -gt 20MB) { $Run.Data.EvidenceComplete=$false;$Run.Data.Actual+='; report over 20 MiB: review evidence/retention before cleanup' }
        if($Run.Data.Status -eq 'Passed' -and $Run.Data.EvidenceComplete) { $Run.Data.Cleanup=Remove-OwnedTestRunDirectory $Run;Save-TestRunReport $Run }
        elseif(!$Run.Data.EvidenceComplete) { $Run.Data.Cleanup.Reason='Evidence needs review; archive limits exceeded; retain environment';Save-TestRunReport $Run }
    } catch {
        Write-Warning "Report/cleanup failed; no further deletion attempted: $($_.Exception.Message); environment=$($Run.TempDirectory)"
        $reason='Report/cleanup failed; no further deletion attempted: '+$_.Exception.Message
        $Run.Data.Status='Failed';$Run.Data.EvidenceComplete=$false;$Run.Data.Cleanup.State='Skipped';$Run.Data.Cleanup.Reason=$reason;$Run.Data.Actual+='; '+$reason
        try {
            if(Test-Path -LiteralPath $Run.TempDirectory) { Write-TestAtomic (Join-Path $Run.TempDirectory 'cleanup-skipped.json') ($Run.Data.Cleanup | ConvertTo-Json -Depth 8) }
            Write-TestAtomic (Join-Path $Run.ReportDirectory 'result.json') (Protect-TestEvidenceText ((ConvertTo-SafeTestEvidence $Run.Data) | ConvertTo-Json -Depth 30))
            Update-TestReportIndex $Run.Project
        } catch { Write-Warning 'Could not persist complete evidence; registered environment remains for inspection' }
        throw
    }
    if($Status -eq 'Passed' -and $Run.Data.Status -eq 'Failed') { throw 'Test finalization failed; report and reproduction retained' }
}
function Register-TestRunMailboxFile($Run,[string]$Path) {
    $full=Assert-TestPlainPath ([IO.Path]::GetFullPath($Path))
    if(Test-Path -LiteralPath $full) { throw 'Never register an existing IPC file as temporary' }
    if([IO.Path]::GetFileName($full).IndexOf($Run.Id,[StringComparison]::Ordinal) -lt 0) { throw 'Mailbox file must include this run ID' }
    $Run.Data.ExternalFiles.Add(@{Path=$full;Parent=[IO.Path]::GetDirectoryName($full);SHA256='';Reason='Installed bridge IPC requires exact live mailbox location; only this new run-ID file, never mailbox directory'})
    Save-TestRunReport $Run
}
function Invoke-TestRunCommand($Run,[string]$FilePath,[string[]]$Arguments=@(),[int]$TimeoutSeconds=600) {
    if(!(Test-Path -LiteralPath $FilePath)) { $FilePath=(Get-Command $FilePath -ErrorAction Stop).Source }
    $command=[ordered]@{File=$FilePath;Arguments=$Arguments;Started=(Get-Date).ToString('o');ExitCode=$null}
    $Run.Data.Commands.Add($command)
    Add-TestRunInput $Run $FilePath 'command executable' 'Retain original or rebuild with recorded project sources'
    Save-TestRunReport $Run
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=[IO.Path]::GetFullPath($FilePath);$info.WorkingDirectory=(Get-Location).ProviderPath
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    foreach($arg in $Arguments) { $info.ArgumentList.Add($arg) }
    $process=[Diagnostics.Process]::new();$process.StartInfo=$info
    try {
        $null=$process.Start()
        $nativeOwner=@{PID=$process.Id;Start=$process.StartTime.ToUniversalTime().ToString('o');File=$info.FileName}
        $Run.Data.Processes.Add($nativeOwner)
        $command.PID=$process.Id
        $outTask=$process.StandardOutput.ReadToEndAsync();$errTask=$process.StandardError.ReadToEndAsync()
        Save-TestRunReport $Run
        if(!$process.WaitForExit($TimeoutSeconds*1000)) {
            $command.ExitCode=124
            Stop-TestRunProcesses $Run
            throw "Owned test command timed out after $TimeoutSeconds seconds; reproduction retained"
        }
        $nativeOwner.Ended=$process.ExitTime.ToUniversalTime().ToString('o')
        if(!$outTask.Wait(5000) -or !$errTask.Wait(5000)) {
            $command.ExitCode=124
            Stop-TestRunProcesses $Run
            throw 'A test descendant kept stdout/stderr open; own processes stopped, reproduction retained'
        }
        $out=$outTask.GetAwaiter().GetResult();$err=$errTask.GetAwaiter().GetResult()
        $command.ExitCode=$process.ExitCode;$global:LASTEXITCODE=$process.ExitCode
        $stem='command-'+$Run.Data.Commands.Count
        if($Run.Data.Name -match '(?i)credential') { $out='Sensitive output withheld';$err='Sensitive output withheld' }
        Write-TestAtomic (Join-Path $Run.ReportDirectory ($stem+'-stdout.log')) (Protect-TestEvidenceText $out)
        Write-TestAtomic (Join-Path $Run.ReportDirectory ($stem+'-stderr.log')) (Protect-TestEvidenceText $err)
        Save-TestRunReport $Run
        foreach($line in @($out -split '\r?\n')) { if($line -ne '') { Write-Output $line } }
        if($command.ExitCode -ne 0) { throw "Test command failed ($($command.ExitCode)): $FilePath; see $stem-stderr.log" }
    } finally { $process.Dispose() }
}
function Invoke-ManagedTest([string]$Script,[hashtable]$Parameters=@{},[string]$Purpose='Regression test',[string]$Expected='All assertions pass; no unowned resources changed') {
    $run=New-TestRun $Script $Parameters $Purpose $Expected
    $global:ZhongriManagedRuns.Add($run)
    $savedEnvironment=@{}
    foreach($name in 'TEMP','TMP','TMPDIR','ZHONGRI_TEST_RUN_ROOT','ZHONGRI_TEST_REPORT_ROOT','ZHONGRI_TEST_BUILD_DIR') { $savedEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process') }
    $env:TEMP=New-TestRunDirectory $run 'os-temp';$env:TMP=$env:TEMP
    $env:TMPDIR=$env:TEMP
    $env:ZHONGRI_TEST_RUN_ROOT=$run.TempDirectory;$env:ZHONGRI_TEST_REPORT_ROOT=$run.ReportDirectory
    $env:ZHONGRI_TEST_BUILD_DIR=New-TestRunDirectory $run 'native-build'
    $status='Interrupted';$actual='Execution did not complete'
    try {
        & $Script @Parameters *>&1 | ForEach-Object {
            $message=if($run.Data.Name -match '(?i)credential') { 'Sensitive credential-test output withheld; status/exit code retained'+"`n" } else { Protect-TestEvidenceText ($_ | Out-String) }
            $log=if($_ -is [Management.Automation.ErrorRecord]) { 'stderr.log' } else { 'stdout.log' }
            [IO.File]::AppendAllText((Join-Path $run.ReportDirectory $log),$message,[Text.UTF8Encoding]::new($false))
            if($message -match '(?i)\b(PASS|FAIL|ASSERT)\b') { $run.Data.Assertions.Add($message.Trim()) }
            Write-Output $_
        }
        $status='Passed';$actual='Test returned normally; assertions and stdout/stderr retained'
    } catch {
        $status=if($_.Exception -is [Management.Automation.PipelineStoppedException] -or $_.Exception -is [OperationCanceledException]) { 'Interrupted' } else { 'Failed' }
        $actual=if($run.Data.Name -match '(?i)credential') { 'Sensitive credential-test failure details withheld; exit/status retained' } else { Protect-TestEvidenceText ($_ | Out-String) }
        [IO.File]::AppendAllText((Join-Path $run.ReportDirectory 'stderr.log'),$actual)
        throw
    } finally {
        foreach($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process') }
        $run.Data.Commands.Insert(0,@{File=$Script;Arguments=$Parameters;ExitCode=if($status -eq 'Passed'){0}else{1};Started=$run.Data.Started})
        $global:ZhongriManagedRuns.RemoveAt($global:ZhongriManagedRuns.Count-1)
        Complete-TestRun $run $status $actual
        Write-Host "TEST_REPORT=$($run.ReportDirectory) CLEANUP=$($run.Data.Cleanup.State)"
    }
}
