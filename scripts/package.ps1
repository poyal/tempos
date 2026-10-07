param([switch]$SkipBuild,[ValidatePattern('^\d+\.\d+\.\d+(-[a-z0-9.]+)?$')][string]$Version='0.1.0')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $SkipBuild){& (Join-Path $PSScriptRoot 'build.ps1') -Preset release -Clean -Test}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$artifactRoot=Join-Path $root 'artifacts'
$stage=Join-Path $artifactRoot ('package-'+[Guid]::NewGuid().ToString('N'))
$folder=Join-Path $stage ('Tempos-'+$Version+'-win-x64')
& $cmake --install (Join-Path $root 'out/release') --prefix $folder
if($LASTEXITCODE){throw 'Package installation failed.'}
Copy-Item -LiteralPath (Join-Path $root 'docs') -Destination $folder -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root 'plan.md') -Destination (Join-Path $folder 'plan.md')
if(Test-Path (Join-Path $folder 'Tempos.TestHost.exe')){throw 'TestHost must not be distributed.'}
$zip=Join-Path $artifactRoot ('Tempos-'+$Version+'-win-x64.zip')
Compress-Archive -LiteralPath $folder -DestinationPath $zip -Force
$hash=Get-FileHash -LiteralPath $zip -Algorithm SHA256
($hash.Hash+'  '+(Split-Path $zip -Leaf)) | Set-Content -LiteralPath ($zip+'.sha256') -Encoding ascii
[pscustomobject]@{folder=$folder;zip=$zip;sha256=$hash.Hash;executableSha256=(Get-FileHash -LiteralPath (Join-Path $folder 'Tempos.exe') -Algorithm SHA256).Hash} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifactRoot 'package.json') -Encoding utf8
Get-Content -LiteralPath (Join-Path $artifactRoot 'package.json')
