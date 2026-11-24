// Sign the packaging receipt's exact ZIP and bind it to the key compiled into Tempos.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '../artifacts');
const receipt = JSON.parse(fs.readFileSync(path.join(root, 'installer-package.json'), 'utf8').replace(/^\uFEFF/, ''));
const key = crypto.createPrivateKey(fs.readFileSync(process.argv[2] === '-' ? 0 : process.env.TEMPOS_UPDATE_PRIVATE_KEY_FILE));
if (key.asymmetricKeyType !== 'ed25519') throw Error('Expected Ed25519 key');
const pub = crypto.createPublicKey(key);
const raw = Buffer.from(pub.export({format:'jwk'}).x, 'base64url').toString('hex');
const name = `Tempos-${receipt.version}-win-x64-update.zip`;
if (receipt.updateZip !== name) throw Error('Unexpected update name');
const binary = fs.readFileSync(path.join(receipt.payload,'Tempos.exe'));
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
if (!binary.includes(Buffer.from(raw)) || hash(binary) !== receipt.executableSha256.toLowerCase() ||
    raw === 'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a')
  throw Error('Signing key must match the production executable');
const bytes = fs.readFileSync(path.join(root,name));
if (hash(bytes) !== receipt.updateZipSha256.toLowerCase()) throw Error('ZIP changed after packaging');
const body = Buffer.from(JSON.stringify({format:'tempos-update-v1',platform:'win-x64',version:receipt.version,name,bytes:bytes.length,sha256:hash(bytes)}));
const sig = crypto.sign(null,body,key);
if (!crypto.verify(null,body,pub,sig)) throw Error('Signature verification failed');
fs.writeFileSync(path.join(root,'update-win-x64.json'),body);
fs.writeFileSync(path.join(root,'update-win-x64.json.sig'),sig);
console.log('Signed '+name+' · '+hash(bytes));
