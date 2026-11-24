param([string]$Dotnet='', [string]$Exe='', [switch]$LongRunning, [string[]]$Modes=@('workspace-editor','edit-mode','interaction','drag-performance','geometry-matrix','render-cache','legacy-grid','shell-actions','updates','persistence','options','failures','capacity','placement','api-mock','weather','startup','isolation','clock-layout','matrix','behavior','temperature','ui-review','design','visual','windows-real'))
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $Dotnet){$Dotnet=Join-Path $root '.tools/dotnet/dotnet.exe'}
$env:DOTNET_ROOT=Split-Path $Dotnet -Parent
$dll=Join-Path $root 'tests/e2e/bin/Release/net10.0-windows/Tempos.E2E.dll'
if(-not $Exe){$Exe=Join-Path $root 'out/release/Tempos.TestHost.exe'}
$exe=[IO.Path]::GetFullPath($Exe)
if([IO.Path]::GetFileName($exe) -ne 'Tempos.TestHost.exe'){throw 'Only isolated TestHost is supported.'}
$hash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$folder=Join-Path $root ('test-results/full-validation-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $folder|Out-Null
$runs=[Collections.Generic.List[object]]::new()
if($LongRunning){$Modes+=@('stress','soak')}
foreach($mode in $Modes){
  if($mode -notin @('workspace-editor','edit-mode','interaction','drag-performance','geometry-matrix','render-cache','legacy-grid','shell-actions','updates','persistence','options','failures','capacity','placement','api-mock','weather','startup','isolation','clock-layout','matrix','behavior','temperature','ui-review','design','visual','windows-real','stress','soak','layout','readme','smoke')){throw 'Unsupported/unsafe suite mode.'}
  if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $hash){throw 'Binary changed during validation.'}
  $before=@(Get-ChildItem (Join-Path $root 'test-results') -Directory -Filter 'e2e-*' | ForEach-Object FullName)
  $log=Join-Path $folder ($mode+'.log')
  Write-Output ('RUN '+$mode+' '+(Get-Date -Format o))
  try {
    $ErrorActionPreference='Continue'
    & $Dotnet $dll $root ('--'+$mode) --exe $exe *> $log
    $exit=$LASTEXITCODE
  } finally { $ErrorActionPreference='Stop' }
  $new=@(Get-ChildItem (Join-Path $root 'test-results') -Directory -Filter 'e2e-*' | Where-Object {$_.FullName -notin $before})
  if($new.Count -ne 1){throw 'Expected exactly one isolated E2E result directory.'}
  $resultFile=Join-Path $new[0].FullName 'results.json'
  if(-not (Test-Path -LiteralPath $resultFile)){throw 'E2E did not write its result.'}
  $result=Get-Content -LiteralPath $resultFile -Raw -Encoding UTF8|ConvertFrom-Json
  $failed=@($result.results|Where-Object {$_.passed -eq $false}).Count
  $runs.Add([pscustomobject]@{mode=$mode;folder=$new[0].FullName;exit=$exit;pass=@($result.results|Where-Object {$_.passed -eq $true}).Count;fail=$failed;skip=@($result.results|Where-Object {$_.skipped -eq $true}).Count;sha256=$result.executableSha256})
  $runs|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $folder 'manifest.json') -Encoding UTF8
  Write-Output ('DONE '+$mode+' exit='+$exit+' failures='+$failed)
  if($exit -ne 0 -or $failed -ne 0 -or $result.apiMode -ne 'mock' -or $result.executableSha256 -ne $hash){Get-Content $log -Tail 15;throw 'Validation stopped on failure; original results preserved.'}
}
Write-Output ('VALIDATED '+$folder)

