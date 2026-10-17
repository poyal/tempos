param(
  [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version='0.1.2',
  [string]$PackageZip,
  [string]$NsisCompiler
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $PackageZip){$PackageZip=Join-Path $root "artifacts/Tempos-$Version-win-x64.zip"}
$PackageZip=(Resolve-Path -LiteralPath $PackageZip).Path
$expected=((Get-Content -LiteralPath ($PackageZip+'.sha256') -Raw).Trim() -split '\s+')[0]
$zipHash=(Get-FileHash -LiteralPath $PackageZip -Algorithm SHA256).Hash
if($zipHash -ne $expected){throw 'Input ZIP does not match its verified checksum.'}
if(-not $NsisCompiler){$NsisCompiler=$env:TEMPOS_MAKENSIS}
if(-not $NsisCompiler){
  $local=Join-Path $root '.tools/nsis/Bin/makensis.exe'
  if(Test-Path -LiteralPath $local){$NsisCompiler=$local}
  elseif(Get-Command makensis.exe -ErrorAction SilentlyContinue){$NsisCompiler=(Get-Command makensis.exe).Source}
}
if(-not $NsisCompiler){throw 'Set TEMPOS_MAKENSIS or pass -NsisCompiler with the NSIS Unicode compiler path.'}
$NsisCompiler=(Resolve-Path -LiteralPath $NsisCompiler).Path
$nsisRoot=Split-Path (Split-Path $NsisCompiler -Parent) -Parent
if(-not (Test-Path -LiteralPath (Join-Path $nsisRoot 'COPYING'))){$nsisRoot=Split-Path $NsisCompiler -Parent}
$stage=Join-Path $root ('artifacts/installer-'+[guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath $PackageZip -DestinationPath $stage
$payload=Join-Path $stage "Tempos-$Version-win-x64"
$exe=Join-Path $payload 'Tempos.exe'
$exeHash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
if((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -ne $Version){throw 'Executable version differs from installer version.'}
if(Test-Path -LiteralPath (Join-Path $payload 'Tempos.TestHost.exe')){throw 'TestHost must not be distributed.'}
# Refresh user documentation without rebuilding or replacing the tested executable.
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $payload -Force
Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination $payload -Force
Copy-Item -LiteralPath (Join-Path $root 'docs') -Destination $payload -Recurse -Force
Copy-Item -LiteralPath (Join-Path $nsisRoot 'COPYING') -Destination (Join-Path $payload 'licenses/NSIS.txt')
# Keep optional historical development/design references online.
foreach($doc in Get-ChildItem -LiteralPath (Join-Path $payload 'docs') -Recurse -File -Filter '*.md'){
  $content=[IO.File]::ReadAllText($doc.FullName)
  $updated=[regex]::Replace($content,'\]\((?<link>(?:\.\./)+(?:design/[^)#\s]+|AGENTS\.md))(?<anchor>#[^)]*)?\)',[Text.RegularExpressions.MatchEvaluator]{param($match)
    $target=[IO.Path]::GetFullPath((Join-Path $doc.DirectoryName $match.Groups['link'].Value))
    $relative=[IO.Path]::GetRelativePath($payload,$target).Replace('\','/')
    if($relative.StartsWith('../')){throw 'Documentation path escapes payload.'}
    return '](https://github.com/poyal/tempos/blob/v'+$Version+'/'+$relative+$match.Groups['anchor'].Value+')'
  })
  if($updated -ne $content){[IO.File]::WriteAllText($doc.FullName,$updated,[Text.UTF8Encoding]::new($false))}
}
function Quote-Nsis([string]$value){return $value.Replace('$','$$').Replace('"','$\"')}
$files=@(Get-ChildItem -LiteralPath $payload -Recurse -File | Sort-Object FullName)
$install=[Collections.Generic.List[string]]::new()
$uninstall=[Collections.Generic.List[string]]::new()
$manifest=@()
foreach($file in $files){
  $relative=[IO.Path]::GetRelativePath($payload,$file.FullName)
  if($relative.StartsWith('..') -or $file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Invalid payload path.'}
  $dir=Split-Path $relative -Parent
  $install.Add('SetOutPath "$INSTDIR'+$(if($dir){'\'+(Quote-Nsis $dir)})+'"')
  $install.Add('File "'+(Quote-Nsis $file.FullName)+'"')
  $uninstall.Add('Delete "$INSTDIR\'+(Quote-Nsis $relative)+'"')
  $manifest += [ordered]@{path=$relative.Replace('\','/');length=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
}
foreach($dir in Get-ChildItem -LiteralPath $payload -Recurse -Directory | Sort-Object {$_.FullName.Length} -Descending){
  $uninstall.Add('RMDir "$INSTDIR\'+(Quote-Nsis ([IO.Path]::GetRelativePath($payload,$dir.FullName)))+'"')
}
$installFile=Join-Path $stage 'install-files.nsh';$uninstallFile=Join-Path $stage 'uninstall-files.nsh'
[IO.File]::WriteAllLines($installFile,$install,[Text.UTF8Encoding]::new($true))
[IO.File]::WriteAllLines($uninstallFile,$uninstall,[Text.UTF8Encoding]::new($true))
$output=Join-Path $root "artifacts/Tempos-$Version-win-x64.exe"
$kib=[int][Math]::Ceiling(($files | Measure-Object Length -Sum).Sum/1024)
$icon=Join-Path $root 'resources/tempos.ico'
$sidebar=Join-Path $root 'resources/installer-sidebar.bmp'
& $NsisCompiler /V3 /INPUTCHARSET UTF8 "/DAPP_VERSION=$Version" "/DINSTALLER_OUT=$output" "/DAPP_ICON=$icon" "/DAPP_SIDEBAR=$sidebar" "/DPAYLOAD_INSTALL=$installFile" "/DPAYLOAD_UNINSTALL=$uninstallFile" "/DPAYLOAD_KIB=$kib" (Join-Path $root 'installer/tempos.nsi')
if($LASTEXITCODE){throw 'NSIS compilation failed.'}
$hash=(Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
($hash+'  '+(Split-Path $output -Leaf)) | Set-Content -LiteralPath ($output+'.sha256') -Encoding ascii
$sourceCommit=(& git -C $root rev-parse HEAD | Out-String).Trim()
$sourceDirty=-not [string]::IsNullOrWhiteSpace((& git -C $root status --porcelain | Out-String))
$record=[ordered]@{version=$Version;createdAt=[DateTimeOffset]::Now;sourceCommit=$sourceCommit;sourceDirty=$sourceDirty;installer=$output;sha256=$hash;size=(Get-Item -LiteralPath $output).Length;inputZip=Split-Path $PackageZip -Leaf;inputZipSha256=$zipHash;executableSha256=$exeHash;compilerVersion=(& $NsisCompiler /VERSION | Out-String).Trim();compilerSha256=(Get-FileHash -LiteralPath $NsisCompiler -Algorithm SHA256).Hash;payload=$payload;files=$manifest}
$record | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $root 'artifacts/installer-package.json') -Encoding utf8
[pscustomobject]@{installer=$output;sha256=$hash;payloadFiles=$files.Count;executableSha256=$exeHash} | ConvertTo-Json
