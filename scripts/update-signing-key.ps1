param([switch]$Initialize,[switch]$Sign)
$ErrorActionPreference='Stop'
if($Initialize -eq $Sign){throw 'Select either -Initialize or -Sign.'}
$dir=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'TemposRelease'
$keyFile=Join-Path $dir 'update-ed25519.dpapi'
$public=Join-Path (Split-Path $PSScriptRoot -Parent) 'resources/update-public-key.txt'
$node=(Get-Command node.exe).Source
if($Initialize){
  if((Test-Path -LiteralPath $keyFile) -or (Test-Path -LiteralPath $public)){throw 'A key already exists. Keep the existing release identity.'}
  $keys=& $node -e "const c=require('node:crypto'),{privateKey,publicKey}=c.generateKeyPairSync('ed25519');process.stdout.write(JSON.stringify({private:privateKey.export({format:'der',type:'pkcs8'}).toString('base64'),public:Buffer.from(publicKey.export({format:'jwk'}).x,'base64url').toString('hex')}))" | ConvertFrom-Json
  if($LASTEXITCODE -or $keys.public -notmatch '^[0-9a-f]{64}$'){throw 'Key generation failed.'}
  $secret=[Convert]::FromBase64String($keys.private)
  try {
    $protected=[Security.Cryptography.ProtectedData]::Protect($secret,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
    [IO.Directory]::CreateDirectory($dir) | Out-Null
    $stream=[IO.File]::Open($keyFile,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {$stream.Write($protected);$stream.Flush($true)} finally {$stream.Dispose()}
    [IO.File]::WriteAllText($public,$keys.public+[char]10)
  } finally {[Array]::Clear($secret);$keys=$null}
  Write-Output 'Initialized the Tempos release key. The public key is in resources/update-public-key.txt.'
} else {
  $secret=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($keyFile),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
  try {
    $pem="-----BEGIN PRIVATE KEY-----"+[char]10+[Convert]::ToBase64String($secret)+[char]10+"-----END PRIVATE KEY-----"
    $pem | & $node (Join-Path $PSScriptRoot 'sign-update.cjs') -
    if($LASTEXITCODE){throw 'Update signature generation failed.'}
  } finally {[Array]::Clear($secret);$pem=$null}
}
