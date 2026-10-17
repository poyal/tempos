param([switch]$Download, [switch]$Test)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dotnet = Join-Path $root '.tools/dotnet/dotnet.exe'
if (-not (Test-Path -LiteralPath $dotnet)) { $dotnet = (Get-Command dotnet.exe).Source }
$env:DOTNET_ROOT = Split-Path $dotnet -Parent
$env:DOTNET_CLI_HOME = Join-Path $root '.tools/dotnet-home'
$env:NUGET_PACKAGES = Join-Path $root '.tools/nuget'
$pins = Get-Content -Raw -LiteralPath (Join-Path $root 'third_party/sensors/dependencies.json') | ConvertFrom-Json
$pawn = Join-Path $root '.tools/sensors/PawnIO-2.2.0-setup.exe'
if (-not (Test-Path -LiteralPath $pawn)) {
  if (-not $Download) { throw 'Pinned PawnIO installer missing. Run build-sensors.ps1 -Download once.' }
  New-Item -ItemType Directory -Force -Path (Split-Path $pawn -Parent) | Out-Null
  Invoke-WebRequest -Uri $pins.pawnIO.url -OutFile $pawn
}
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $pawn).Hash -ne $pins.pawnIO.sha256) { throw 'PawnIO SHA-256 mismatch.' }
$signature = Get-AuthenticodeSignature -LiteralPath $pawn
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=namazso.eu') { throw 'PawnIO signature validation failed.' }
$project = Join-Path $root 'sensors/Tempos.Sensors/Tempos.Sensors.csproj'
& $dotnet restore $project --locked-mode
if ($LASTEXITCODE) { throw 'Sensor dependency restore failed.' }
$stage = Join-Path $root ('out/sensors/package-' + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $stage 'payload'
& $dotnet publish $project -c Release --no-restore -o $payload
if ($LASTEXITCODE) { throw 'Sensor publish failed.' }
if ($Test) {
  $tests = Join-Path $root 'sensors/Tempos.Sensors.Tests/Tempos.Sensors.Tests.csproj'
  & $dotnet restore $tests --locked-mode
  if ($LASTEXITCODE) { throw 'Sensor test restore failed.' }
  & $dotnet run --project $tests -c Release --no-restore
  if ($LASTEXITCODE) { throw 'Sensor tests failed.' }
}
$controller = Join-Path $root 'out/release/Tempos.SensorControl.exe'
if (-not (Test-Path -LiteralPath $controller)) { throw 'Build native release before packaging sensors.' }
Copy-Item -LiteralPath $controller -Destination $payload
Copy-Item -LiteralPath (Join-Path $root 'third_party/sensors') -Destination (Join-Path $payload 'licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $payload 'licenses/Tempos.NOTICES.md')
# Preserve package license files and metadata for the complete locked dependency tree.
$dependencyNotices = [Collections.Generic.List[string]]::new()
$dependencyNotices.Add('# Bundled sensor dependencies')
$lock = Get-Content -Raw -LiteralPath (Join-Path (Split-Path $project -Parent) 'packages.lock.json') | ConvertFrom-Json
$seenPackages = @{}
foreach ($framework in $lock.dependencies.PSObject.Properties) {
  foreach ($dependency in $framework.Value.PSObject.Properties) {
    $id = $dependency.Name.ToLowerInvariant()
    $version = $dependency.Value.resolved
    $key = "$id/$version"
    if ($seenPackages.ContainsKey($key)) { continue }
    $seenPackages[$key] = $true
    $packageDir = Join-Path $env:NUGET_PACKAGES $key
    $noticeDir = Join-Path $payload "licenses/nuget/$id/$version"
    New-Item -ItemType Directory -Force -Path $noticeDir | Out-Null
    $metadata = Get-ChildItem -LiteralPath $packageDir -File -Filter '*.nuspec' | Select-Object -First 1
    if (-not $metadata) { throw "Missing package metadata: $key" }
    Copy-Item -LiteralPath $metadata.FullName -Destination $noticeDir
    [xml]$xml = Get-Content -LiteralPath $metadata.FullName -Raw
    $license = $xml.SelectSingleNode("//*[local-name()='metadata']/*[local-name()='license']")
    $dependencyNotices.Add("- $key : $($license.InnerText)")
    foreach ($file in Get-ChildItem -LiteralPath $packageDir -Recurse -File |
        Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE|THIRD.PARTY)' -and $_.Extension -notin '.dll','.pdb' }) {
      $relative = [IO.Path]::GetRelativePath($packageDir, $file.FullName)
      $destination = Join-Path $noticeDir $relative
      New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
      Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
  }
}
$runtime = Join-Path $env:NUGET_PACKAGES 'microsoft.netcore.app.runtime.win-x64/10.0.12'
Copy-Item -LiteralPath (Join-Path $runtime 'LICENSE.TXT') -Destination (Join-Path $payload 'licenses/dotnet-LICENSE.TXT')
Copy-Item -LiteralPath (Join-Path $runtime 'THIRD-PARTY-NOTICES.TXT') -Destination (Join-Path $payload 'licenses/dotnet-THIRD-PARTY-NOTICES.TXT')
[IO.File]::WriteAllLines((Join-Path $payload 'licenses/DEPENDENCIES.md'), $dependencyNotices, [Text.UTF8Encoding]::new($false))
function Quote-Nsis([string]$value) { return $value.Replace('$','$$').Replace('"','$\"') }
$install = [Collections.Generic.List[string]]::new()
$uninstall = [Collections.Generic.List[string]]::new()
$manifest = @()
foreach ($file in Get-ChildItem -LiteralPath $payload -Recurse -File | Sort-Object FullName) {
  if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse point in sensor payload.' }
  $relative = [IO.Path]::GetRelativePath($payload, $file.FullName)
  $dir = Split-Path $relative -Parent
  $install.Add('SetOutPath "$INSTDIR'+$(if ($dir) {'\'+(Quote-Nsis $dir)})+'"')
  $install.Add('File "'+(Quote-Nsis $file.FullName)+'"')
  $uninstall.Add('Delete "$INSTDIR\'+(Quote-Nsis $relative)+'"')
  $manifest += @{path=$relative;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
}
foreach ($dir in Get-ChildItem -LiteralPath $payload -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending) {
  $uninstall.Add('RMDir "$INSTDIR\'+(Quote-Nsis ([IO.Path]::GetRelativePath($payload,$dir.FullName)))+'"')
}
$installFile = Join-Path $stage 'install.nsh'
$uninstallFile = Join-Path $stage 'uninstall.nsh'
[IO.File]::WriteAllLines($installFile, $install, [Text.UTF8Encoding]::new($true))
[IO.File]::WriteAllLines($uninstallFile, $uninstall, [Text.UTF8Encoding]::new($true))
$compiler = Join-Path $root '.tools/nsis/Bin/makensis.exe'
if (-not (Test-Path -LiteralPath $compiler)) { $compiler = (Get-Command makensis.exe).Source }
$output = Join-Path $root 'out/release/Tempos.Sensors.Setup.exe'
& $compiler /V3 /INPUTCHARSET UTF8 "/DSENSOR_OUT=$output" "/DSENSOR_INSTALL=$installFile" "/DSENSOR_UNINSTALL=$uninstallFile" "/DCONTROLLER=$controller" "/DPAWN_INSTALLER=$pawn" (Join-Path $root 'installer/sensors.nsi')
if ($LASTEXITCODE) { throw 'Sensor NSIS packaging failed.' }
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $stage 'files.json') -Encoding utf8
Get-FileHash -LiteralPath $output -Algorithm SHA256
