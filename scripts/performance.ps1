param([ValidateRange(0,86400)][int]$WarmupSeconds=300,[ValidateRange(5,172800)][int]$SampleSeconds=1800,[ValidateRange(1,60)][int]$IntervalSeconds=1,[string]$Name='baseline',[string]$SettingsFile='',[string]$Profile='Five M widgets, default intervals, real system providers, weather key absent',[ValidateSet('Five','Nine','Clock','None')][string]$Budget='Five')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(Get-Process -Name Tempos -ErrorAction SilentlyContinue){throw 'Close the existing Tempos instance before measuring an isolated performance process.'}
if($SampleSeconds -lt 2*$IntervalSeconds){throw 'SampleSeconds must include at least two complete sample intervals.'}
$result=Join-Path $root ('test-results/performance-'+$Name+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $result | Out-Null
if($SettingsFile){Copy-Item -LiteralPath $SettingsFile -Destination (Join-Path $result 'settings.json')}
$exe=Join-Path $root 'out/release/Tempos.exe'
$binaryHash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$start=[System.Diagnostics.ProcessStartInfo]::new($exe)
$start.UseShellExecute=$false
$start.WorkingDirectory=$root
$start.ArgumentList.Add('--data-dir')
$start.ArgumentList.Add($result)
$process=[System.Diagnostics.Process]::Start($start)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PerfNative {
[DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process,uint flag);
[DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string name);
[DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,IntPtr name);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr w,IntPtr l);
[DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd,uint msg,IntPtr w,IntPtr l);
[DllImport("user32.dll")] public static extern bool PostThreadMessage(uint thread,uint msg,IntPtr w,IntPtr l);
}
'@
$rows=[System.Collections.Generic.List[object]]::new()
$startTime=Get-Date
try {
  $process.WaitForInputIdle(15000) | Out-Null
  $management=[IntPtr]::Zero
  for($attempt=0;$attempt -lt 100 -and $management -eq [IntPtr]::Zero;$attempt++){
    $candidate=[IntPtr]::Zero
    do {
      $candidate=[PerfNative]::FindWindowEx([IntPtr]::Zero,$candidate,'Tempos.Management',[IntPtr]::Zero)
      [uint32]$ownerId=0
      if($candidate -ne [IntPtr]::Zero){[PerfNative]::GetWindowThreadProcessId($candidate,[ref]$ownerId)|Out-Null}
      if($ownerId -eq $process.Id){$management=$candidate;break}
    } while($candidate -ne [IntPtr]::Zero)
    if($management -eq [IntPtr]::Zero){Start-Sleep -Milliseconds 100}
  }
  if($management -eq [IntPtr]::Zero){throw 'Owned management window unavailable.'}
  [PerfNative]::SendMessage($management,0x111,[IntPtr]1505,[IntPtr]::Zero)|Out-Null
  $initialState=Get-Content -LiteralPath (Join-Path $result 'diagnostics.json') -Raw|ConvertFrom-Json
  $expectedWidgets=switch($Budget){'Five'{5};'Nine'{9};'Clock'{1};'None'{$initialState.widgets}}
  if($initialState.widgets -ne $expectedWidgets -or $initialState.hidden -or @($initialState.windows|Where-Object {-not $_.placed -or -not $_.visible}).Count){throw 'Performance profile must have the expected widgets placed and visible.'}
  if($Budget -ne 'None'){
    $expectedKinds=0..($expectedWidgets-1)
    if((@($initialState.windows.kind|Sort-Object) -join ',') -ne ($expectedKinds -join ',')){throw 'Performance budget requires one widget of each expected kind.'}
  }
  $initialState|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $result 'initial-state.json') -Encoding utf8
  Write-Output ('Process '+$process.Id+' warmup '+$WarmupSeconds+' seconds; output '+$result)
  $stopwatch=[System.Diagnostics.Stopwatch]::StartNew()
  $process.Refresh()
  $previousCpu=$process.TotalProcessorTime.TotalMilliseconds
  $previousTime=0.0
  $nextProgress=30
  while($stopwatch.Elapsed.TotalSeconds -lt ($WarmupSeconds+$SampleSeconds)) {
    Start-Sleep -Seconds $IntervalSeconds
    $process.Refresh()
    if($process.HasExited){throw 'Tempos exited during performance test.'}
    $time=$stopwatch.Elapsed.TotalSeconds
    if($time -ge $nextProgress){Write-Output ('Progress '+[Math]::Floor($time)+'/'+($WarmupSeconds+$SampleSeconds)+' seconds');$nextProgress+=30}
    $cpu=$process.TotalProcessorTime.TotalMilliseconds
    if($previousTime -ge $WarmupSeconds) {
      $raw=($cpu-$previousCpu)/($time-$previousTime)
      $row=[pscustomobject]@{seconds=[Math]::Round($time,2);elapsedSeconds=$time-$previousTime;cpuMilliseconds=$cpu-$previousCpu;rawCpuMsPerSecond=$raw;normalizedCpuPercent=$raw/(10*[Environment]::ProcessorCount);privateBytes=$process.PrivateMemorySize64;workingSet=$process.WorkingSet64;handles=$process.HandleCount;threads=$process.Threads.Count;gdi=[PerfNative]::GetGuiResources($process.Handle,0);user=[PerfNative]::GetGuiResources($process.Handle,1)}
      $rows.Add($row)
      $row | ConvertTo-Json -Compress | Add-Content (Join-Path $result 'samples.jsonl') -Encoding utf8
    }
    $previousCpu=$cpu
    $previousTime=$time
  }
  if($rows.Count -lt 2){throw 'Insufficient performance samples.'}
  $cpuValues=@($rows | ForEach-Object normalizedCpuPercent | Sort-Object)
  $report=[ordered]@{started=$startTime.ToString('o');completed=(Get-Date).ToString('o');executableSha256=$binaryHash;windows=[Environment]::OSVersion.Version.ToString();profile=$Profile;warmupSeconds=$WarmupSeconds;sampleSeconds=$SampleSeconds;intervalSeconds=$IntervalSeconds;logicalProcessors=[Environment]::ProcessorCount;count=$rows.Count;averageCpuPercent=($rows | Measure-Object normalizedCpuPercent -Average).Average;p95CpuPercent=$cpuValues[[Math]::Floor(($cpuValues.Count-1)*0.95)];averageRawCpuMsPerSecond=($rows | Measure-Object rawCpuMsPerSecond -Average).Average;maximumPrivateBytes=($rows|Measure-Object privateBytes -Maximum).Maximum;maximumWorkingSet=($rows|Measure-Object workingSet -Maximum).Maximum;privateGrowth=$rows[-1].privateBytes-$rows[0].privateBytes;handleGrowth=$rows[-1].handles-$rows[0].handles;gdiGrowth=$rows[-1].gdi-$rows[0].gdi;userGrowth=$rows[-1].user-$rows[0].user}
  $duration=($rows|Measure-Object elapsedSeconds -Sum).Sum
  $report.averageRawCpuMsPerSecond=($rows|Measure-Object cpuMilliseconds -Sum).Sum/$duration
  $report.averageCpuPercent=$report.averageRawCpuMsPerSecond/(10*[Environment]::ProcessorCount)
  $report.p95CpuPercent=$cpuValues[[Math]::Ceiling($cpuValues.Count*0.95)-1]
  $report.actualSampleSeconds=$duration
  $report.budget=$Budget
  $report.standardDuration=($WarmupSeconds -ge 300 -and $SampleSeconds -ge 1800 -and $duration -ge 1799 -and $IntervalSeconds -eq 1)
  $report.maximumThreads=($rows|Measure-Object threads -Maximum).Maximum
  $checks=[System.Collections.Generic.List[object]]::new()
  function Add-BudgetCheck([string]$Label,[double]$Value,[double]$Limit){$checks.Add([pscustomobject]@{name=$Label;actual=$Value;limit=$Limit;passed=($Value -le $Limit)})}
  switch($Budget){
    'Five'{
      Add-BudgetCheck 'Average CPU percent' $report.averageCpuPercent 0.1
      Add-BudgetCheck 'P95 CPU percent' $report.p95CpuPercent 0.3
      Add-BudgetCheck 'Raw CPU ms/second' $report.averageRawCpuMsPerSecond 4
      Add-BudgetCheck 'Private bytes' $report.maximumPrivateBytes (32MB)
      Add-BudgetCheck 'Working set' $report.maximumWorkingSet (48MB)
    }
    'Nine'{
      Add-BudgetCheck 'Average CPU percent' $report.averageCpuPercent 0.2
      Add-BudgetCheck 'Private bytes' $report.maximumPrivateBytes (48MB)
      Add-BudgetCheck 'Working set' $report.maximumWorkingSet (72MB)
    }
    'Clock'{Add-BudgetCheck 'Average CPU percent' $report.averageCpuPercent 0.02}
  }
  [PerfNative]::SendMessage($management,0x111,[IntPtr]1505,[IntPtr]::Zero)|Out-Null
  $finalState=Get-Content -LiteralPath (Join-Path $result 'diagnostics.json') -Raw|ConvertFrom-Json
  $checks.Add([pscustomobject]@{name='Widgets remain visible';passed=($finalState.widgets -eq $expectedWidgets -and -not $finalState.hidden -and @($finalState.windows|Where-Object {-not $_.placed -or -not $_.visible}).Count -eq 0)})
  $report.checks=@($checks.ToArray())
  $report.passed=@($checks|Where-Object {-not $_.passed}).Count -eq 0
  $report|ConvertTo-Json -Depth 6|Set-Content (Join-Path $result 'report.json') -Encoding utf8
  $report|ConvertTo-Json -Depth 6
  if(-not $report.passed){throw 'Performance budget exceeded. See report.json and samples.jsonl.'}
} finally {
  # The app only consumes this normal menu command in its own window.
  $hwnd=[IntPtr]::Zero
  do {
    $hwnd=[PerfNative]::FindWindowEx([IntPtr]::Zero,$hwnd,'Tempos.Management',[IntPtr]::Zero)
    [uint32]$ownerId=0
    if($hwnd -ne [IntPtr]::Zero){[PerfNative]::GetWindowThreadProcessId($hwnd,[ref]$ownerId)|Out-Null}
  } while($hwnd -ne [IntPtr]::Zero -and $ownerId -ne $process.Id)
  if($hwnd -ne [IntPtr]::Zero){[PerfNative]::PostMessage($hwnd,0x111,[IntPtr]1507,[IntPtr]::Zero)|Out-Null}
  if(-not $process.HasExited -and -not $process.WaitForExit(1500)){
    $uiThread=$process.Threads|Sort-Object StartTime|Select-Object -First 1
    [PerfNative]::PostThreadMessage($uiThread.Id,0x12,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
    if(-not $process.WaitForExit(15000)){throw 'The owned performance process did not exit normally.'}
  }
  $process.Dispose()
}
