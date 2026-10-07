param([ValidateRange(0,86400)][int]$WarmupSeconds=300,[ValidateRange(5,172800)][int]$SampleSeconds=1800,[ValidateRange(1,60)][int]$IntervalSeconds=1,[string]$Name='baseline',[string]$SettingsFile='',[string]$Profile='Five M widgets, default intervals, real system providers, weather key absent')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
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
[DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string name);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr w,IntPtr l);
[DllImport("user32.dll")] public static extern bool PostThreadMessage(uint thread,uint msg,IntPtr w,IntPtr l);
}
'@
$rows=[System.Collections.Generic.List[object]]::new()
$startTime=Get-Date
try {
  $process.WaitForInputIdle(15000) | Out-Null
  Write-Output ('Process '+$process.Id+' warmup '+$WarmupSeconds+' seconds; output '+$result)
  $stopwatch=[System.Diagnostics.Stopwatch]::StartNew()
  $process.Refresh()
  $previousCpu=$process.TotalProcessorTime.TotalMilliseconds
  $previousTime=0.0
  while($stopwatch.Elapsed.TotalSeconds -lt ($WarmupSeconds+$SampleSeconds)) {
    Start-Sleep -Seconds $IntervalSeconds
    $process.Refresh()
    if($process.HasExited){throw 'Tempos exited during performance test.'}
    $time=$stopwatch.Elapsed.TotalSeconds
    $cpu=$process.TotalProcessorTime.TotalMilliseconds
    if($previousTime -ge $WarmupSeconds) {
      $raw=($cpu-$previousCpu)/($time-$previousTime)
      $row=[pscustomobject]@{seconds=[Math]::Round($time,2);rawCpuMsPerSecond=$raw;normalizedCpuPercent=$raw/(10*[Environment]::ProcessorCount);privateBytes=$process.PrivateMemorySize64;workingSet=$process.WorkingSet64;handles=$process.HandleCount;gdi=[PerfNative]::GetGuiResources($process.Handle,0);user=[PerfNative]::GetGuiResources($process.Handle,1)}
      $rows.Add($row)
      $row | ConvertTo-Json -Compress | Add-Content (Join-Path $result 'samples.jsonl') -Encoding utf8
    }
    $previousCpu=$cpu
    $previousTime=$time
  }
  $cpuValues=@($rows | ForEach-Object normalizedCpuPercent | Sort-Object)
  $report=[ordered]@{started=$startTime.ToString('o');completed=(Get-Date).ToString('o');executableSha256=$binaryHash;windows=[Environment]::OSVersion.Version.ToString();profile=$Profile;warmupSeconds=$WarmupSeconds;sampleSeconds=$SampleSeconds;intervalSeconds=$IntervalSeconds;logicalProcessors=[Environment]::ProcessorCount;count=$rows.Count;averageCpuPercent=($rows | Measure-Object normalizedCpuPercent -Average).Average;p95CpuPercent=$cpuValues[[Math]::Floor(($cpuValues.Count-1)*0.95)];averageRawCpuMsPerSecond=($rows | Measure-Object rawCpuMsPerSecond -Average).Average;maximumPrivateBytes=($rows|Measure-Object privateBytes -Maximum).Maximum;maximumWorkingSet=($rows|Measure-Object workingSet -Maximum).Maximum;privateGrowth=$rows[-1].privateBytes-$rows[0].privateBytes;handleGrowth=$rows[-1].handles-$rows[0].handles;gdiGrowth=$rows[-1].gdi-$rows[0].gdi;userGrowth=$rows[-1].user-$rows[0].user}
  $report|ConvertTo-Json|Set-Content (Join-Path $result 'report.json') -Encoding utf8
  $report|ConvertTo-Json
} finally {
  # The app only consumes this normal menu command in its own window.
  $hwnd=[IntPtr]::Zero
  do {
    $hwnd=[PerfNative]::FindWindowEx([IntPtr]::Zero,$hwnd,'Tempos.Management',$null)
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
