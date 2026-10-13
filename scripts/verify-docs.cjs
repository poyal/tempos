// Verify local manual references and the exact screenshot set, in a checkout or ZIP.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root=path.resolve(process.argv[2]||'.');
const files=['README.md',...fs.readdirSync(path.join(root,'docs'),{recursive:true})
  .filter(f=>f.endsWith('.md')||f.replaceAll('\\','/')==='assets/v0.1.0/gallery.html')
  .map(f=>path.join('docs',f))];
let references=0;const failures=[];
const slug=s=>s.toLowerCase().replace(/<[^>]+>/g,'').replace(/[^\p{L}\p{N}_\-\s]/gu,'').replace(/ /g,'-');
for(const file of files){
  const content=fs.readFileSync(path.join(root,file),'utf8');
  for(const match of content.matchAll(/\]\(([^)]+)\)|(?:href|src)="([^"]+)"/g)){
    const link=match[1]||match[2]; if(/^[a-z][a-z0-9+.-]*:/i.test(link))continue;
    const [href,anchor]=link.split('#');
    const target=href?path.resolve(root,path.dirname(file),decodeURIComponent(href)):path.join(root,file);
    references++;
    if(!fs.existsSync(target)){failures.push(file+' -> '+link);continue;}
    if(anchor && /\.(md|html)$/.test(target)){
      const text=fs.readFileSync(target,'utf8');
      const ids=[...text.matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]);
      for(const heading of text.matchAll(/^#{1,6}\s+(.+)$/gm))ids.push(slug(heading[1].trim()));
      if(!ids.includes(decodeURIComponent(anchor)))failures.push(file+' missing anchor -> '+link);
    }
  }
}
const dir=path.join(root,'docs/assets/v0.1.0');
const manifest=JSON.parse(fs.readFileSync(path.join(dir,'captures.json'),'utf8'));
const entries=[...manifest.captures,...manifest.boards];
if(manifest.captures.filter(c=>/^(clock|weather|cpu|gpu|memory|system|network|disk|calendar)-(s|slim|m|l|xl)\.png$/.test(c.file)).length!==37)failures.push('Expected 37 widget sizes');
if(manifest.captures.filter(c=>c.file.startsWith('usage-')).length!==17)failures.push('Expected 17 usage captures');
for(const entry of entries){
  const bytes=fs.readFileSync(path.join(dir,entry.file));
  if(crypto.createHash('sha256').update(bytes).digest('hex')!==entry.sha256)failures.push('Hash mismatch '+entry.file);
  if(bytes.subarray(1,4).toString()!=='PNG'||bytes.readUInt32BE(16)<100||bytes.readUInt32BE(20)<50)failures.push('Invalid PNG '+entry.file);
}
const pngs=fs.readdirSync(dir).filter(f=>f.endsWith('.png'));
if(pngs.length!==entries.length||new Set(entries.map(e=>e.file)).size!==entries.length)failures.push('Untracked or duplicate release images');
const installerDir=path.join(root,'docs/assets/installer-v0.1.0');
let installerImages=0;
if(fs.existsSync(installerDir)){
  const installer=JSON.parse(fs.readFileSync(path.join(installerDir,'captures.json'),'utf8').replace(/^\uFEFF/,''));
  for(const capture of installer.captures){
    const bytes=fs.readFileSync(path.join(installerDir,capture.file));
    if(crypto.createHash('sha256').update(bytes).digest('hex')!==capture.sha256)failures.push('Installer screenshot hash mismatch: '+capture.file);
    installerImages++;
  }
  if(installerImages!==2)failures.push('Expected folder and completion installer screenshots');
}
if(failures.length){console.error(failures.join('\n'));process.exit(1);}
console.log(`PASS ${files.length} documents, ${references} local references/anchors, ${entries.length} PNG hashes, 37 widget sizes, 17 usage captures, ${installerImages} installer captures`);
