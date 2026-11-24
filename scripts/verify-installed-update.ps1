param([Parameter(Mandatory)][string]$Assets,[Parameter(Mandatory)][version]$Version,[Parameter(Mandatory)][version]$Previous,[string]$BootstrapExe)
$ErrorActionPreference='Stop'
$targetVersion=$Version.ToString(); $from=$Previous.ToString()
$Assets=(Resolve-Path $Assets).Path
$appKey='HKCU:\Software\Tempos\Installer'
$registration='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Tempos'
$desktop=Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Tempos.lnk'
$menu=Join-Path ([Environment]::GetFolderPath('Programs')) 'Tempos'
if((Test-Path $appKey) -or (Test-Path $registration) -or (Test-Path $desktop) -or (Test-Path $menu) -or (Get-Process Tempos -ErrorAction SilentlyContinue)){throw 'SKIP: existing current-user installation, process or shortcut.'}
$work=Join-Path ([IO.Path]::GetTempPath()) ('tempos-release-'+[guid]::NewGuid().ToString('N'))
$install=Join-Path $work '한글 설치'
$data=Join-Path $work '시험 설정'
$exe=Join-Path $install 'Tempos.exe'
[IO.Directory]::CreateDirectory($data)|Out-Null
function Install([version]$v){
  $installer=Join-Path $Assets "Tempos-$v-win-x64.exe"
  $p=Start-Process -FilePath $installer -ArgumentList "/S /D=$install" -PassThru -Wait -WindowStyle Hidden
  if($p.ExitCode -or (Get-Item $exe).VersionInfo.ProductVersion -ne $v.ToString()){throw "Installation failed: $v"}
}
function CheckInventory {
  foreach($item in (Get-Content -LiteralPath (Join-Path $install 'tempos-files.json') -Raw | ConvertFrom-Json)){
    if((Get-FileHash -LiteralPath (Join-Path $install $item.path)).Hash -ne $item.sha256){throw "Installed file mismatch: $($item.path)"}
  }
}
function WindowOf([Diagnostics.Process]$p){
  for($i=0;$i -lt 300;$i++){
    if($p.HasExited){throw "App exited before initialization: $($p.ExitCode)"}
    $p.Refresh()
    if($p.MainWindowHandle -ne 0 -and [NativeUpdateTest]::GetDlgItem($p.MainWindowHandle,1703) -ne 0){return $p.MainWindowHandle}
    Start-Sleep -Milliseconds 100
  }
  throw 'No initialized settings window.'
}
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class NativeUpdateTest {
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, int msg, IntPtr w, IntPtr l);
}
"@
Install $Previous
if($BootstrapExe){
  # The published 0.1.5 only understands the installer. Verify its EXE bridge,
  # then install a prior-version build of the new protocol for the first ZIP test.
  Install $Version
  CheckInventory
  if((Get-Item $BootstrapExe).VersionInfo.ProductVersion -ne $from){throw 'Invalid protocol baseline'}
  Copy-Item -LiteralPath $BootstrapExe -Destination $exe -Force
  $inventoryPath=Join-Path $install 'tempos-files.json'
  $inventory=Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
  ($inventory | Where-Object path -eq 'Tempos.exe').sha256=(Get-FileHash $exe).Hash.ToLowerInvariant()
  [IO.File]::WriteAllText($inventoryPath,(ConvertTo-Json -InputObject $inventory -Compress))
  Set-ItemProperty -LiteralPath $registration -Name DisplayVersion -Value $from
}
CheckInventory
$note=Join-Path $install 'user-note.txt'
[IO.File]::WriteAllText($note,'preserve')
$settings=Join-Path $data 'settings.json'
[IO.File]::WriteAllText($settings,'{"version":1,"checkUpdatesOnStart":false,"widgets":[{"id":"verified-clock","kind":0,"size":2}]}')
$id=[guid]::NewGuid().ToString('B')
$job=Join-Path $data "updates/$id"
[IO.Directory]::CreateDirectory($job)|Out-Null
foreach($name in "Tempos-$Version-win-x64-update.zip",'update-win-x64.json','update-win-x64.json.sig'){Copy-Item -LiteralPath (Join-Path $Assets $name) -Destination $job}
[IO.File]::WriteAllText((Join-Path $data 'update-ready'),$id)
$p=Start-Process -FilePath $exe -ArgumentList "--settings --data-dir ""$data""" -PassThru -WindowStyle Hidden
$window=WindowOf $p
[void][NativeUpdateTest]::PostMessage($window,0x111,1703,0)
if(!$p.WaitForExit(150000)){throw 'Original process did not exit.'}
$resultFile=Join-Path $data 'update-result.json'
for($i=0;$i -lt 600 -and !(Test-Path $resultFile);$i++){Start-Sleep -Milliseconds 200}
$result=Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json
if(!$result.success -or $result.version -ne $targetVersion -or $result.previous -ne $from){throw 'Missing committed update result.'}
$new=Get-Process -Id $result.restartedPid
if($new.Path -ne $exe -or (Get-Item $exe).VersionInfo.ProductVersion -ne $targetVersion){throw 'Wrong restarted executable.'}
if((Get-ItemProperty $registration).DisplayVersion -ne $targetVersion){throw 'Incorrect Windows installed version.'}
CheckInventory
$window=WindowOf $new
[void][NativeUpdateTest]::PostMessage($window,0x111,1507,0)
if(!$new.WaitForExit(30000)){throw 'Owned process failed to close.'}
$uninstall=Join-Path $work 'remove.exe'
Copy-Item -LiteralPath (Join-Path $install 'Uninstall Tempos.exe') -Destination $uninstall
$p=Start-Process $uninstall -ArgumentList "/S _?=$install" -PassThru -Wait -WindowStyle Hidden
if($p.ExitCode -or (Test-Path $appKey) -or (Test-Path $registration) -or (Test-Path $desktop) -or (Test-Path $menu)){throw 'Windows uninstall registration or shortcuts remain.'}
$remaining=@(Get-ChildItem $install -Recurse -File)
if($remaining.Count -ne 1 -or $remaining[0].FullName -ne $note -or [IO.File]::ReadAllText($note) -ne 'preserve' -or !([IO.File]::ReadAllText($settings).Contains('verified-clock'))){throw 'Uninstall payload or profile mismatch.'}
Write-Output "PASS $from -> $targetVersion installed ZIP, startup acknowledgement, registration, payload and uninstall."
