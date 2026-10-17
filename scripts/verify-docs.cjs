// Verify local manual references and every archived/current screenshot gallery.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root=path.resolve(process.argv[2]||'.');
const files=['README.md',...fs.readdirSync(path.join(root,'docs'),{recursive:true})
  .filter(f=>f.endsWith('.md')||f.replaceAll('\\','/').endsWith('/gallery.html'))
  .map(f=>path.join('docs',f))];
let references=0,images=0,widgetSizes=0,usageImages=0;
const failures=[];
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
  if(file==='README.md'){
    for(const image of content.matchAll(/!\[([^\]]*)\]\([^)]+\)|<img\b[^>]*>/g)){
      if(image[1]!==undefined ? !image[1].trim() : !/\balt="[^"]+"/.test(image[0]))
        failures.push('README image has no alternative text');
    }
  }
}
const legacyUsage=['first-launch','add-apply','arrange','arrange-widget','clock-options',
  'system-options','network','weather-region','location-applied','weather-open-meteo','weather-kma',
  'calendar-month','calendar-connected','app-startup','backup-exported','backup-restored','diagnostics']
  .map(name=>'usage-'+name+'.png');
const assets=path.join(root,'docs/assets');
const sets=fs.readdirSync(assets,{withFileTypes:true}).filter(d=>d.isDirectory()
  &&fs.existsSync(path.join(assets,d.name,'captures.json'))
  &&fs.existsSync(path.join(assets,d.name,'gallery.html')));
if(!sets.length)failures.push('No screenshot galleries found');
for(const set of sets){
  const dir=path.join(assets,set.name);
  const manifest=JSON.parse(fs.readFileSync(path.join(dir,'captures.json'),'utf8'));
  const entries=[...manifest.captures,...manifest.boards];
  const widgets=manifest.captures.filter(c=>/^(clock|weather|cpu|gpu|memory|system|network|disk|calendar)-(s|slim|m|l|xl)\.png$/.test(c.file));
  const expectedWidgets=['clock','weather','cpu','gpu','memory','system','network','disk','calendar']
    .flatMap(kind=>(kind==='calendar'?['s','slim','m','l','xl']:['s','slim','m','l']).map(size=>kind+'-'+size+'.png'));
  if(widgets.length!==37||expectedWidgets.some(file=>!widgets.some(c=>c.file===file)))
    failures.push(set.name+': Expected all 37 widget sizes');
  const required=manifest.requiredUsageCaptures||legacyUsage;
  const usage=manifest.captures.filter(c=>c.file.startsWith('usage-'));
  if(legacyUsage.some(file=>!required.includes(file))||new Set(required).size!==required.length
    ||usage.length!==required.length||required.some(file=>!usage.some(c=>c.file===file)))
    failures.push(set.name+': Missing, duplicate or undeclared usage captures');
  if(manifest.requiredUsageCaptures){
    if(manifest.conditions.api!=='mock'||manifest.conditions.data!=='sample'
      ||manifest.captures.some(c=>c.apiMode!=='mock'||c.systemMode!=='fixture')
      ||new Set(manifest.captures.map(c=>c.executableSha256)).size!==1)
      failures.push(set.name+': Inconsistent mock/sample provenance or TestHost build');
  }
  for(const entry of entries){
    const file=path.join(dir,entry.file);
    if(!fs.existsSync(file)){failures.push(set.name+': Missing PNG '+entry.file);continue;}
    const bytes=fs.readFileSync(file);
    if(crypto.createHash('sha256').update(bytes).digest('hex')!==entry.sha256)failures.push(set.name+': Hash mismatch '+entry.file);
    if(bytes.length<24||bytes.subarray(1,4).toString()!=='PNG'||bytes.readUInt32BE(16)<100||bytes.readUInt32BE(20)<50)
      failures.push(set.name+': Invalid PNG '+entry.file);
    if(entry.inputs?.some(input=>!manifest.captures.some(c=>c.file===input)))
      failures.push(set.name+': Unknown board input '+entry.file);
  }
  const pngs=fs.readdirSync(dir).filter(f=>f.endsWith('.png'));
  if(pngs.length!==entries.length||new Set(entries.map(e=>e.file)).size!==entries.length)
    failures.push(set.name+': Untracked or duplicate gallery images');
  images+=entries.length;widgetSizes+=widgets.length;usageImages+=usage.length;
}
const installerDir=path.join(assets,'installer-v0.1.0');
let installerImages=0;
if(fs.existsSync(installerDir)){
  const installer=JSON.parse(fs.readFileSync(path.join(installerDir,'captures.json'),'utf8').replace(/^\uFEFF/,''));
  for(const capture of installer.captures){
    const bytes=fs.readFileSync(path.join(installerDir,capture.file));
    if(crypto.createHash('sha256').update(bytes).digest('hex')!==capture.sha256)failures.push('Installer screenshot hash mismatch: '+capture.file);
    installerImages++;
  }
  if(installerImages!==2)failures.push('Expected archived folder and completion installer screenshots');
}
if(failures.length){console.error(failures.join('\n'));process.exit(1);}
console.log('PASS '+files.length+' documents, '+references+' local references/anchors, '+sets.length+
  ' galleries, '+images+' PNG hashes, '+widgetSizes+' widget sizes, '+usageImages+
  ' usage captures, '+installerImages+' archived installer captures');
