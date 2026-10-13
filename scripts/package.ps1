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
# Historical design/source references stay online; the manual and its screenshots
# are included locally. Avoid broken relative links without shipping source trees.
foreach($doc in Get-ChildItem -LiteralPath (Join-Path $folder 'docs') -Recurse -File -Filter '*.md'){
  $content=[IO.File]::ReadAllText($doc.FullName)
  $rewritten=[regex]::Replace($content,'\]\((?<link>(?:\.\./)+(?:design/[^)#\s]+|AGENTS\.md))(?<anchor>#[^)]*)?\)',[Text.RegularExpressions.MatchEvaluator]{param($match)
    $target=[IO.Path]::GetFullPath((Join-Path $doc.DirectoryName $match.Groups['link'].Value))
    $relative=[IO.Path]::GetRelativePath($folder,$target).Replace('\','/')
    if($relative.StartsWith('../')){throw 'Documentation reference escapes package root.'}
    $escaped=($relative.Split('/') | ForEach-Object {[Uri]::EscapeDataString($_)}) -join '/'
    return '](https://github.com/poyal/tempos/blob/v'+$Version+'/'+$escaped+$match.Groups['anchor'].Value+')'
  })
  if($content -ne $rewritten){[IO.File]::WriteAllText($doc.FullName,$rewritten,[Text.UTF8Encoding]::new($false))}
}
if((Get-Item -LiteralPath (Join-Path $folder 'Tempos.exe')).VersionInfo.ProductVersion -ne $Version.Split('-')[0]){throw 'Package version does not match executable resources.'}
if(Test-Path (Join-Path $folder 'Tempos.TestHost.exe')){throw 'TestHost must not be distributed.'}
$zip=Join-Path $artifactRoot ('Tempos-'+$Version+'-win-x64.zip')
Compress-Archive -LiteralPath $folder -DestinationPath $zip -Force
$hash=Get-FileHash -LiteralPath $zip -Algorithm SHA256
($hash.Hash+'  '+(Split-Path $zip -Leaf)) | Set-Content -LiteralPath ($zip+'.sha256') -Encoding ascii
[pscustomobject]@{folder=$folder;zip=$zip;sha256=$hash.Hash;executableSha256=(Get-FileHash -LiteralPath (Join-Path $folder 'Tempos.exe') -Algorithm SHA256).Hash} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifactRoot 'package.json') -Encoding utf8
Get-Content -LiteralPath (Join-Path $artifactRoot 'package.json')
