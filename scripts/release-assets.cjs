// Validate the exact installer, portable ZIP and signed update set before upload.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '..');
const dir = path.resolve(process.argv[2] || path.join(root, 'artifacts'));
const version = process.argv[3];
if (!/^\d+\.\d+\.\d+$/.test(version)) throw Error('Pass artifact directory and version');
const base = `Tempos-${version}-win-x64`;
const exe = base+'.exe', zip = base+'.zip', patch = base+'-update.zip';
const metadata = 'update-win-x64.json';
const read = n => fs.readFileSync(path.join(dir,n));
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const names = [exe,zip,patch,metadata,metadata+'.sig',exe+'.sha256',zip+'.sha256'];
const hashes = new Map(names.map(n=>[n,digest(read(n))]));
const raw = fs.readFileSync(path.join(root,'resources/update-public-key.txt'),'utf8').trim();
const pub = crypto.createPublicKey({format:'jwk',key:{kty:'OKP',crv:'Ed25519',x:Buffer.from(raw,'hex').toString('base64url')}});
if (!crypto.verify(null,read(metadata),pub,read(metadata+'.sig'))) throw Error('Bad update signature');
const m = JSON.parse(read(metadata));
if (m.format!=='tempos-update-v1' || m.platform!=='win-x64' || m.version!==version ||
    m.name!==patch || m.bytes!==fs.statSync(path.join(dir,patch)).size || m.sha256!==hashes.get(patch))
  throw Error('Patch differs from signed metadata');
for (const n of [exe,zip]) {
  const parts=read(n+'.sha256').toString('utf8').trim().split(/\s+/);
  if (parts.length!==2 || parts[0].toLowerCase()!==hashes.get(n) || parts[1]!==n) throw Error('Checksum mismatch: '+n);
}
const sums=names.map(n=>hashes.get(n)+'  '+n+'\n').join('');
if (process.argv.includes('--write-sums')) fs.writeFileSync(path.join(dir,'SHA256SUMS.txt'),sums);
else if(read('SHA256SUMS.txt').toString('utf8')!==sums) throw Error('Incomplete release checksums');
console.log([...names,'SHA256SUMS.txt'].join('\n'));
