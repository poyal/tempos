param([ValidateSet('release','debug','asan')][string]$Preset = 'release', [switch]$Test, [switch]$Clean)
$ErrorActionPreference = 'Stop'
$env:VSLANG = '1033'
# Keep the compiler's localized include diagnostics and CMake's detector in the same encoding.
& chcp.com 65001 | Out-Null
[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false)
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools not found.' }
$dev = Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1'
& $dev -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:VSLANG = '1033'
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninjaDir = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$env:PATH = "$ninjaDir;$env:PATH"
Push-Location $root
try {
  & $cmake --fresh --preset $Preset
  if ($LASTEXITCODE) { throw 'CMake configure failed.' }
  if ($Clean) { & $cmake --build --preset $Preset --clean-first }
  else { & $cmake --build --preset $Preset }
  if ($LASTEXITCODE) { throw 'Build failed.' }
  if ($Test) {
    & (Join-Path (Split-Path $cmake -Parent) 'ctest.exe') --preset $Preset
    if ($LASTEXITCODE) { throw 'Tests failed.' }
  }
} finally { Pop-Location }
