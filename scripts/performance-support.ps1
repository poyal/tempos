# Policy limits are product budgets, not universal Windows responsiveness limits.
function Get-TemposPerformancePolicy([object]$Report, [ValidateSet('Five','Nine','Clock','None')][string]$Budget) {
  $required=[Collections.Generic.List[object]]::new()
  $targets=[Collections.Generic.List[object]]::new()
  function Check([string]$Name, $Value, [double]$Limit) {
    $valid=$null -ne $Value
    $number=0.0
    try { $number=[double]$Value } catch { $valid=$false }
    $valid=$valid -and -not [double]::IsNaN($number) -and -not [double]::IsInfinity($number) -and $number -ge 0
    [pscustomobject]@{name=$Name;actual=$Value;limit=$Limit;passed=($valid -and $number -le $Limit);valid=$valid}
  }
  switch($Budget) {
    'Five' {
      $required.Add((Check 'Average CPU percent' $Report.averageCpuPercent 0.1))
      $required.Add((Check 'P95 CPU percent' $Report.p95CpuPercent 0.3))
      $required.Add((Check 'Raw CPU ms/second' $Report.averageRawCpuMsPerSecond 10))
      $required.Add((Check 'Private bytes' $Report.maximumPrivateBytes (32MB)))
      $required.Add((Check 'Working set' $Report.maximumWorkingSet (48MB)))
      $targets.Add((Check 'Raw CPU optimization target ms/second' $Report.averageRawCpuMsPerSecond 4))
    }
    'Nine' {
      $required.Add((Check 'Average CPU percent' $Report.averageCpuPercent 0.2))
      $required.Add((Check 'P95 CPU percent' $Report.p95CpuPercent 0.6))
      $required.Add((Check 'Raw CPU ms/second' $Report.averageRawCpuMsPerSecond 20))
      $required.Add((Check 'Private bytes' $Report.maximumPrivateBytes (48MB)))
      $required.Add((Check 'Working set' $Report.maximumWorkingSet (72MB)))
    }
    'Clock' { $required.Add((Check 'Average CPU percent' $Report.averageCpuPercent 0.02)) }
  }
  [pscustomobject]@{version='personal-two-windows-v1';required=@($required.ToArray());targets=@($targets.ToArray())}
}

function New-TemposSampleWriter([string]$Path) {
  # Keep one writer that permits monitoring readers. Add-Content repeatedly opens
  # with sharing restrictions and can fail while another process reads the log.
  $stream=[IO.FileStream]::new($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite)
  try {
    $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
    $writer.AutoFlush=$true
    return $writer
  } catch { $stream.Dispose(); throw }
}
