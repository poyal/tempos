param([ValidateRange(0,86400)][int]$WarmupSeconds=300,[ValidateRange(5,172800)][int]$SampleSeconds=1800,[ValidateRange(1,60)][int]$IntervalSeconds=1,[string]$Name='baseline',[string]$Exe='',[string]$SettingsFile='',[string]$Profile='Five M widgets, default intervals, real system providers, weather key absent',[ValidateSet('Five','Nine','Clock','None')][string]$Budget='Five',[switch]$AllowParked,[switch]$SettingsClosed)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'performance-support.ps1')
# Installed Tempos processes remain running; only our TestHost PID is controlled.
$installedIds=@(Get-Process -Name Tempos -ErrorAction SilentlyContinue | ForEach-Object Id)
function Protected-State {
  $profilePath=Join-Path $env:LOCALAPPDATA 'Tempos'
  $values=@('settings.json','settings.backup.json') | ForEach-Object {
    $file=Join-Path $profilePath $_
    if(Test-Path -LiteralPath $file){(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash}else{'absent'}
  }
  $runValue=Get-ItemPropertyValue -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name Tempos -ErrorAction SilentlyContinue
  return (($values -join '|')+'|'+$runValue)
}
$protectedBefore=Protected-State
if($SampleSeconds -lt 2*$IntervalSeconds){throw 'SampleSeconds must include at least two complete sample intervals.'}
$result=Join-Path $root ('test-results/performance-'+$Name+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $result | Out-Null
if($SettingsFile){Copy-Item -LiteralPath $SettingsFile -Destination (Join-Path $result 'settings.json')}
if(-not $Exe){$Exe=Join-Path $root 'out/release/Tempos.TestHost.exe'}
$exe=[IO.Path]::GetFullPath($Exe)
if([IO.Path]::GetFileName($exe) -ne 'Tempos.TestHost.exe'){throw 'Only isolated TestHost performance runs are supported.'}
$binaryHash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$start=[System.Diagnostics.ProcessStartInfo]::new($exe)
$start.UseShellExecute=$false
$start.WorkingDirectory=$root
$start.Arguments='--live-system --data-dir "'+$result+'"'
$process=$null
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PerfNative {
public static bool InputDesktopActive() {
  var input=OpenInputDesktop(0,false,1);
  if(input==IntPtr.Zero) return false;
  try {
    var a=new System.Text.StringBuilder(256); var b=new System.Text.StringBuilder(256); uint needed;
    return GetUserObjectInformation(input,2,a,512,out needed) &&
      GetUserObjectInformation(GetThreadDesktop(GetCurrentThreadId()),2,b,512,out needed) &&
      a.ToString()==b.ToString();
  } finally { CloseDesktop(input); }
}
[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint flags);
[DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
[DllImport("user32.dll")] static extern IntPtr GetThreadDesktop(uint thread);
[DllImport("user32.dll")] static extern IntPtr OpenInputDesktop(uint flags,bool inherit,uint access);
[DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
[DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool GetUserObjectInformation(IntPtr handle,int index,System.Text.StringBuilder value,uint length,out uint needed);
public static IntPtr OwnedManagement(uint pid) {
  IntPtr h=IntPtr.Zero;
  while((h=FindWindowEx(IntPtr.Zero,h,null,IntPtr.Zero))!=IntPtr.Zero) {
    uint owner; GetWindowThreadProcessId(h,out owner);
    if(owner!=pid) continue;
    var name=new System.Text.StringBuilder(256); GetClassName(h,name,256);
    if(name.ToString().StartsWith("Tempos.TestHost.Management.")) return h;
  }
  return IntPtr.Zero;
}
[DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process,uint flag);
[DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string name);
[DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,IntPtr name);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
[DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd,System.Text.StringBuilder name,int count);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr w,IntPtr l);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
[DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd,uint msg,IntPtr w,IntPtr l);
[DllImport("user32.dll")] public static extern bool PostThreadMessage(uint thread,uint msg,IntPtr w,IntPtr l);
}
'@
$rows=[System.Collections.Generic.List[object]]::new()
$startTime=Get-Date
$executionState=0
$sampleWriter=$null
try {
  # Scoped request only; no power-plan, screen-lock or registry settings are changed.
  $executionState=[PerfNative]::SetThreadExecutionState([uint32]2147483651)
  if(-not $executionState -or -not [PerfNative]::InputDesktopActive()){throw 'Performance measurement requires the active input desktop.'}
  $sampleWriter=New-TemposSampleWriter (Join-Path $result 'samples.jsonl')
  $process=[System.Diagnostics.Process]::Start($start)
  $process.WaitForInputIdle(15000) | Out-Null
  $management=[IntPtr]::Zero
  for($attempt=0;$attempt -lt 100 -and $management -eq [IntPtr]::Zero;$attempt++){
    $management=[PerfNative]::OwnedManagement($process.Id)
    if($management -eq [IntPtr]::Zero){Start-Sleep -Milliseconds 100}
  }
  if($management -eq [IntPtr]::Zero){throw 'Owned management window unavailable.'}
  if($SettingsClosed){
    [PerfNative]::SendMessage($management,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
    if([PerfNative]::IsWindowVisible($management)){throw 'Owned settings window did not close.'}
  }
  [PerfNative]::SendMessage($management,0x111,[IntPtr]1505,[IntPtr]::Zero)|Out-Null
  $initialState=Get-Content -LiteralPath (Join-Path $result 'diagnostics.json') -Raw|ConvertFrom-Json
  if($initialState.apiMode -ne 'mock' -or [IO.Path]::GetFullPath($initialState.testDataDirectory) -ne [IO.Path]::GetFullPath($result)){throw 'Performance TestHost isolation/API boundary mismatch.'}
  $expectedWidgets=switch($Budget){'Five'{5};'Nine'{9};'Clock'{1};'None'{$initialState.widgets}}
  if($initialState.widgets -ne $expectedWidgets -or $initialState.hidden -or
     (-not $AllowParked -and @($initialState.windows|Where-Object {-not $_.placed -or -not $_.visible}).Count) -or
     @($initialState.windows|Where-Object {$_.placed -and $_.visible}).Count -eq 0){throw 'Performance profile must have the expected widgets placed and visible.'}
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
  $measuredSeconds=0.0
  $nextProgress=30
  while($measuredSeconds -lt $SampleSeconds) {
    Start-Sleep -Seconds $IntervalSeconds
    $process.Refresh()
    if($process.HasExited){throw 'Tempos exited during performance test.'}
    $time=$stopwatch.Elapsed.TotalSeconds
    if($time-$previousTime -gt [Math]::Max(10,5*$IntervalSeconds)){throw 'Sampling interrupted by suspension or an excessive scheduling gap.'}
    if($time -ge $nextProgress){
      if(-not [PerfNative]::InputDesktopActive()){throw 'Input desktop changed or locked during measurement.'}
      Write-Output ('Progress '+[Math]::Floor($time)+'/'+($WarmupSeconds+$SampleSeconds)+' seconds');$nextProgress+=30
    }
    $cpu=$process.TotalProcessorTime.TotalMilliseconds
    if($previousTime -ge $WarmupSeconds) {
      $raw=($cpu-$previousCpu)/($time-$previousTime)
      $row=[pscustomobject]@{seconds=[Math]::Round($time,2);elapsedSeconds=$time-$previousTime;cpuMilliseconds=$cpu-$previousCpu;rawCpuMsPerSecond=$raw;normalizedCpuPercent=$raw/(10*[Environment]::ProcessorCount);privateBytes=$process.PrivateMemorySize64;workingSet=$process.WorkingSet64;handles=$process.HandleCount;threads=$process.Threads.Count;gdi=[PerfNative]::GetGuiResources($process.Handle,0);user=[PerfNative]::GetGuiResources($process.Handle,1)}
      $rows.Add($row)
      $measuredSeconds+=$row.elapsedSeconds
      $sampleWriter.WriteLine(($row | ConvertTo-Json -Compress))
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
  $report.maximumSampleIntervalSeconds=($rows|Measure-Object elapsedSeconds -Maximum).Maximum
  $report.inputDesktopVerified=$true
  $report.sleepPrevention='scoped-display-and-system-request'
  $report.budget=$Budget
  $report.apiMode='mock'
  $report.systemMode='windows-real'
  $report.testClass='isolated-performance'
  $report.allowParked=[bool]$AllowParked
  $report.initialVisibleCount=@($initialState.windows|Where-Object {$_.placed -and $_.visible}).Count
  $report.standardDuration=($WarmupSeconds -ge 300 -and $SampleSeconds -ge 1800 -and $duration -ge 1799 -and $IntervalSeconds -eq 1)
  $report.maximumThreads=($rows|Measure-Object threads -Maximum).Maximum
  $report.device=[ordered]@{name=[Environment]::MachineName;cpu=(Get-ItemProperty -LiteralPath 'HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0' -ErrorAction SilentlyContinue).ProcessorNameString;logicalProcessors=[Environment]::ProcessorCount;monitors=$initialState.monitors}
  $checks=[System.Collections.Generic.List[object]]::new()
  $policy=Get-TemposPerformancePolicy $report $Budget
  $report.policyVersion=$policy.version
  foreach($check in $policy.required){$checks.Add($check)}
  $report.optimizationTargets=$policy.targets
  $report.optimizationTargetsMet=@($policy.targets|Where-Object {-not $_.passed}).Count -eq 0
  [PerfNative]::SendMessage($management,0x111,[IntPtr]1505,[IntPtr]::Zero)|Out-Null
  $finalState=Get-Content -LiteralPath (Join-Path $result 'diagnostics.json') -Raw|ConvertFrom-Json
  $report.settingsClosed=[bool]$SettingsClosed
  $report.previewDrawsDuringRun=[long]$finalState.previewDraws-[long]$initialState.previewDraws
  if($SettingsClosed){
    $checks.Add([pscustomobject]@{name='Closed settings stop preview rendering';passed=($report.previewDrawsDuringRun -eq 0 -and -not [PerfNative]::IsWindowVisible($management))})
  }
  $requests=Join-Path $result 'api-mock-requests.jsonl'
  if(Test-Path $requests){
    foreach($line in Get-Content $requests){
      $request=$line|ConvertFrom-Json
      if($request.transport -ne 'mock' -or -not $request.matched){throw 'Unregistered or live API request during performance test.'}
    }
  }
  $report.finalVisibleCount=@($finalState.windows|Where-Object {$_.placed -and $_.visible}).Count
  $checks.Add([pscustomobject]@{name='Widgets remain visible';passed=($finalState.widgets -eq $expectedWidgets -and -not $finalState.hidden -and $report.finalVisibleCount -eq $report.initialVisibleCount -and ($AllowParked -or $report.finalVisibleCount -eq $expectedWidgets))})
  $report.checks=@($checks.ToArray())
  $report.passed=@($checks|Where-Object {-not $_.passed}).Count -eq 0
  # A short smoke/profile run can satisfy numerical budgets without qualifying
  # this device, and a single device never qualifies both supported PCs.
  $report.devicePerformanceQualified=$report.passed -and $report.standardDuration -and $Budget -ne 'None'
  $report.qualificationScope='This device and profile only; functional and second-PC checks are separate'
  $report|ConvertTo-Json -Depth 6|Set-Content (Join-Path $result 'report.json') -Encoding utf8
  $report|ConvertTo-Json -Depth 6
  if(-not $report.passed){throw 'Performance budget exceeded. See report.json and samples.jsonl.'}
} finally {
  if($sampleWriter){$sampleWriter.Dispose()}
  if($executionState){[PerfNative]::SetThreadExecutionState($executionState)|Out-Null}
  # The app only consumes this normal menu command in its own window.
  if($process){
    $hwnd=[PerfNative]::OwnedManagement($process.Id)
    if($hwnd -ne [IntPtr]::Zero){[PerfNative]::PostMessage($hwnd,0x111,[IntPtr]1507,[IntPtr]::Zero)|Out-Null}
    if(-not $process.HasExited -and -not $process.WaitForExit(1500)){
      $uiThread=$process.Threads|Sort-Object StartTime|Select-Object -First 1
      [PerfNative]::PostThreadMessage($uiThread.Id,0x12,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
      if(-not $process.WaitForExit(15000)){throw 'The owned performance process did not exit normally.'}
    }
    $process.Dispose()
  }
  if((Protected-State) -ne $protectedBefore){throw 'Installed profile changed; user data has not been overwritten or restored.'}
  foreach($installedId in $installedIds){
    if(-not (Get-Process -Id $installedId -ErrorAction SilentlyContinue)){throw 'An installed process exited during measurement.'}
  }
}
