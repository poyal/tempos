$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/performance-support.ps1')
$script:checks=0
function Require([bool]$Condition,[string]$Message) {
  if(-not $Condition){throw $Message}
  $script:checks++
}
function Sample {
  [pscustomobject]@{averageCpuPercent=0.043;p95CpuPercent=0.13;averageRawCpuMsPerSecond=5.135;maximumPrivateBytes=19MB;maximumWorkingSet=44MB}
}
function Required-Passed($Policy) { @($Policy.required|Where-Object {-not $_.passed}).Count -eq 0 }
$r=Sample
$p=Get-TemposPerformancePolicy $r Five
Require (Required-Passed $p) '5.135 ms/s is within the operational budget'
Require ($p.targets.Count -eq 1 -and -not $p.targets[0].passed) '4 ms/s remains a separate unmet target'
Require ($p.version -eq 'personal-two-windows-v1') 'Reports identify the policy'
foreach($case in @(
  @('averageRawCpuMsPerSecond',10.0,$true),@('averageRawCpuMsPerSecond',10.001,$false),
  @('averageCpuPercent',0.1,$true),@('averageCpuPercent',0.1001,$false),
  @('p95CpuPercent',0.3,$true),@('p95CpuPercent',0.3001,$false),
  @('maximumPrivateBytes',32MB,$true),@('maximumPrivateBytes',(32MB+1),$false),
  @('maximumWorkingSet',48MB,$true),@('maximumWorkingSet',(48MB+1),$false))) {
  $r=Sample; $r.($case[0])=$case[1]
  Require ((Required-Passed (Get-TemposPerformancePolicy $r Five)) -eq $case[2]) ('Boundary '+$case[0]+' '+$case[1])
}
foreach($bad in @($null,[double]::NaN,[double]::PositiveInfinity,-1,'invalid')) {
  $r=Sample;$r.averageRawCpuMsPerSecond=$bad
  Require (-not (Required-Passed (Get-TemposPerformancePolicy $r Five))) 'Invalid or missing CPU data cannot pass'
}
$r=Sample; $r.averageRawCpuMsPerSecond=4
Require ((Get-TemposPerformancePolicy $r Five).targets[0].passed) 'Optimization target boundary passes'
$r=Sample; $r.averageRawCpuMsPerSecond=20; $r.averageCpuPercent=0.2; $r.p95CpuPercent=0.6; $r.maximumPrivateBytes=48MB; $r.maximumWorkingSet=72MB
Require (Required-Passed (Get-TemposPerformancePolicy $r Nine)) 'Nine-widget operational boundaries'
$r.averageRawCpuMsPerSecond=20.001
Require (-not (Required-Passed (Get-TemposPerformancePolicy $r Nine))) 'Nine widgets cannot exceed 2 percent of one core time'
$r=Sample;$r.averageCpuPercent=0.0201
Require (-not (Required-Passed (Get-TemposPerformancePolicy $r Clock))) 'Clock budget remains enforced'
$p=Get-TemposPerformancePolicy (Sample) None
Require ($p.required.Count -eq 0 -and $p.targets.Count -eq 0) 'None is a profile without resource qualification'

# Hold a real monitoring reader open for the whole write sequence. The old
# per-sample Add-Content open could fail under exactly this sharing condition.
$path=Join-Path ([IO.Path]::GetTempPath()) ('tempos-log-sharing-'+[guid]::NewGuid().ToString('N')+'.jsonl')
$writer=$null;$stream=$null;$reader=$null
try {
  $writer=New-TemposSampleWriter $path
  $stream=[IO.FileStream]::new($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
  $reader=[IO.StreamReader]::new($stream)
  for($i=0;$i -lt 1000;$i++) {
    $writer.WriteLine(('{"sample":'+$i+'}'))
    $line=$reader.ReadLine()
    if($null -eq $line -or ($line|ConvertFrom-Json).sample -ne $i){throw 'Concurrent reader missed or corrupted a flushed sample'}
  }
  Require ($reader.ReadLine() -eq $null) '1000 concurrent writes/reads preserve every JSON sample exactly once'
  $writer.Dispose();$writer=$null
  $reader.Dispose();$reader=$null;$stream=$null
  $exclusive=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
  $exclusive.Dispose()
  Require $true 'Writer and monitoring reader release their file handles'
} finally {
  if($writer){$writer.Dispose()}
  if($reader){$reader.Dispose()}elseif($stream){$stream.Dispose()}
  if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}
}
Write-Output ('PASS '+$script:checks+' policy/sharing checks; 1000 concurrently read samples')
