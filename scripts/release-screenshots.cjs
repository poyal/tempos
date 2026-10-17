// Compose genuine Windows widget captures. This does not redraw widget UI.
// node scripts/release-screenshots.cjs <runs.json> --output docs/assets/<capture-set> --version <version>
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn,spawnSync,execFileSync} = require('node:child_process');
const {pathToFileURL} = require('node:url');
const root = process.cwd();
const runs = JSON.parse(fs.readFileSync(process.argv[2], 'utf8').replace(/^\uFEFF/, ''));
const option = name => { const i=process.argv.indexOf(name); if(i<0)return undefined; if(!process.argv[i+1]||process.argv[i+1].startsWith('--'))throw Error('Missing value for '+name); return process.argv[i+1]; };
const version = option('--version') || fs.readFileSync(path.join(root,'CMakeLists.txt'),'utf8').match(/project\(Tempos VERSION ([\d.]+)/)[1];
if(!/^\d+\.\d+\.\d+$/.test(version))throw Error('Expected a numeric release version');
const output = path.resolve(root, option('--output') || 'docs/assets/v'+version);
if(path.dirname(output)!==path.join(root,'docs/assets'))throw Error('Output must be a capture-set directory directly under docs/assets');
const sourceCommit = execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim();
const work = path.join(root, 'artifacts/release-screenshots');
fs.mkdirSync(output, {recursive:true}); fs.mkdirSync(work, {recursive:true});
const kinds = [
  ['clock','시계','시간과 날짜를 또렷하게. 모든 크기에서 디지털 시계.'],
  ['weather','날씨','지역의 날씨와 낮밤에 따라 바뀌는 배경.'],
  ['cpu','CPU','지금의 사용률부터 최근 60초의 변화까지.'],
  ['gpu','GPU','그래픽 사용량을 같은 눈금과 구성으로.'],
  ['memory','메모리','사용 중인 메모리와 시간에 따른 변화를 함께.'],
  ['system','시스템 정보','PC 이름부터 OS · CPU · GPU · 가동 시간까지.'],
  ['network','네트워크','전체 네트워크의 수신과 송신을 한눈에.'],
  ['disk','디스크','작은 크기는 선택한 볼륨, L은 전체 드라이브.'],
  ['calendar','캘린더','간단한 날짜부터 일정이 담긴 큰 달력까지.']
];
const sizes = ['S','Slim','M','L','XL'];
const dimensions = ['160 × 160','336 × 72','336 × 160','336 × 336','1008 × 840'];
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const captures = [];
function copy(run, name, destination) {
  const source = path.join('test-results',run.folder,name+'.png');
  const result = JSON.parse(fs.readFileSync(path.join(root,'test-results',run.folder,'results.json'),'utf8'));
  if(result.apiMode!=='mock'||result.systemMode!=='fixture'||result.results.some(r=>r.passed===false))
    throw Error('Only successful sample/mock runs can be published: '+run.folder);
  fs.copyFileSync(path.join(root,source),path.join(output,destination));
  captures.push({file:destination,source:source.replaceAll('\\','/'),sha256:hash(path.join(output,destination)),
    started:result.started,completed:result.completed,mode:result.mode,apiMode:result.apiMode,
    systemMode:result.systemMode,executable:result.executable,executableSha256:result.executableSha256});
}
const ui = runs.find(r=>r.mode==='ui-review' && r.exitCode===0);
const docs = runs.find(r=>r.mode==='readme' && r.exitCode===0);
if(!ui || !docs) throw Error('Successful --ui-review and --readme runs required');
const requiredUsageCaptures = ['first-launch','add-apply','arrange','arrange-widget','clock-options',
  'system-options','network','weather-region','location-applied','weather-open-meteo','weather-kma',
  'calendar-month','calendar-connected','app-startup','backup-exported','backup-restored','diagnostics',
  'cpu-temperature'].map(name=>'usage-'+name+'.png');
for(let k=0;k<kinds.length;k++) for(let s=0;s<(k===8?5:4);s++)
  copy(ui,`design-kind-${k}-size-${s}`,`${kinds[k][0]}-${sizes[s].toLowerCase()}.png`);
for(const file of fs.readdirSync(path.join(root,'test-results',docs.folder)).filter(f=>/^usage-[a-z-]+\.png$/.test(f)))
  copy(docs,file.slice(0,-4),file);
copy(docs,'design-usage-arrange-widget','usage-arrange-widget.png');
for(const file of requiredUsageCaptures)if(!captures.some(c=>c.file===file))throw Error('Missing required manual capture: '+file);
if(new Set(captures.map(c=>c.executableSha256)).size!==1)throw Error('All captures must use the same TestHost build');
for(const [scenario,label] of [[0,'clear'],[2,'rain'],[4,'night']])
  copy(ui,`design-weather-scene-${scenario}`,`weather-${label}.png`);
const style = `*{box-sizing:border-box}body{margin:0;background:#edf4fa;color:#153563;font-family:'Malgun Gothic',sans-serif}.board{width:1200px;height:720px;position:relative;padding:38px 40px}.eyebrow{font-size:16px;font-weight:700;letter-spacing:2px;color:#476890}h1{font-size:44px;letter-spacing:-2px;margin:10px 0 4px}p{font-size:19px;margin:0;color:#456083}.number{position:absolute;right:44px;top:44px;font-size:17px}.slot{position:absolute}.slot span{display:block;margin-bottom:12px;font-size:18px;font-weight:700}.slot small{font-size:15px;font-weight:400;color:#536e8d}.slot img{display:block;width:100%;height:auto}.s{left:40px;top:285px;width:192px}.slim{left:288px;top:225px;width:403.2px}.m{left:288px;top:409px;width:403.2px}.l{left:744px;top:225px;width:403.2px}footer{position:absolute;bottom:27px;font-size:14px;color:#536e8d}.brand{position:absolute;right:45px;bottom:27px;font-weight:700;font-size:15px;color:#345c93}`;
for(let k=0;k<kinds.length;k++) {
  const [slug,title,description] = kinds[k];
  const slots = sizes.slice(0,4).map((size,s)=>`<div class="slot ${size.toLowerCase()}"><span>${size} <small>· ${dimensions[s]} DIP</small></span><img src="${pathToFileURL(path.join(output,slug+'-'+size.toLowerCase()+'.png')).href}" alt="${title} ${size}"></div>`).join('');
  fs.writeFileSync(path.join(work,slug+'.html'),`<!doctype html><html lang="ko"><meta charset="utf-8"><title>${title}</title><style>${style}</style><main class="board"><div class="eyebrow">TEMPOS / WINDOWS</div><div class="number">${String(k+1).padStart(2,'0')} / 09</div><h1>${title}</h1><p>${description}</p>${slots}<footer>실제 앱 캡처 · 예제 데이터 · 크기 간 비율 유지${k===8?' · XL은 별도 화면 참조':''}</footer><div class="brand">Tempos ${version}</div></main></html>`);
}
const profile = path.join(work,'chrome-'+Date.now());
const browser = spawn('C:/Program Files/Google/Chrome/Application/chrome.exe',[
  '--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check',
  '--disable-background-networking','--disable-extensions','--disable-sync','--disable-component-update',
  '--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{windowsHide:true,stdio:'ignore'});
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
let socket;
const watchdog=setTimeout(()=>{console.error('Screenshot composition timed out');socket?.close();browser.kill();process.exit(1);},60000);
(async()=>{
  let active;
  for(let n=0;n<100;n++){try{active=fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8');break;}catch{}await sleep(100);}
  if(!active)throw Error('Headless Chrome startup failed');
  const targets=await(await fetch('http://127.0.0.1:'+active.split('\n')[0]+'/json/list')).json();
  socket=new WebSocket(targets.find(t=>t.type==='page').webSocketDebuggerUrl);
  await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
  let next=0;const pending=new Map();
  socket.addEventListener('message',e=>{const m=JSON.parse(e.data);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result);}});
  const send=(method,params={})=>new Promise((resolve,reject)=>{const id=++next;pending.set(id,{resolve,reject});socket.send(JSON.stringify({id,method,params}));});
  const evaluate=async expression=>{const r=await send('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;};
  await send('Page.enable');await send('Runtime.enable');
  await send('Emulation.setDeviceMetricsOverride',{width:1200,height:720,deviceScaleFactor:1,mobile:false});
  const boards=[];
  const overview=spawnSync('pwsh',['-NoProfile','-File',path.join(root,'scripts/readme-screenshots.ps1'),'-Results',path.join(root,'test-results',ui.folder),'-Output',path.relative(root,path.join(output,'overview.png'))],{windowsHide:true,encoding:'utf8'});
  if(overview.status!==0)throw Error('Overview composition failed: '+overview.stderr);
  boards.push({file:'overview.png',sha256:hash(path.join(output,'overview.png')),inputs:kinds.map(([slug])=>slug+'-m.png')});
  for(const [slug] of kinds){
    await send('Page.navigate',{url:pathToFileURL(path.join(work,slug+'.html')).href});
    for(let n=0;n<60;n++){if(await evaluate("document.querySelectorAll('.slot img').length===4"))break;await sleep(50);}
    if(!await evaluate("Promise.all([...document.images].map(im=>im.decode().then(()=>true,()=>false))).then(a=>a.length===4&&a.every(Boolean))"))throw Error('Missing widget image');
    await evaluate('document.fonts.ready.then(()=>true)');
    const shot=await send('Page.captureScreenshot',{format:'png'});
    const file=slug+'-sizes.png';fs.writeFileSync(path.join(output,file),Buffer.from(shot.data,'base64'));
    boards.push({file,sha256:hash(path.join(output,file)),inputs:captures.filter(c=>c.file.startsWith(slug+'-')&&sizes.slice(0,4).some(s=>c.file===slug+'-'+s.toLowerCase()+'.png')).map(c=>c.file)});
    console.log('COMPOSED '+file);
  }
  const gallery=`<!doctype html><html lang="ko"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Tempos ${version} 실제 화면</title><style>body{max-width:1200px;margin:32px auto;padding:0 18px;background:#edf4fa;color:#153563;font-family:'Malgun Gothic',sans-serif}img{display:block;max-width:100%;height:auto}nav{display:flex;flex-wrap:wrap;gap:16px}section{margin:40px 0}a{color:#234f99}p{line-height:1.7}</style><h1>Tempos · 위젯별 모든 크기</h1><p>Windows 앱의 실제 화면을 예제 데이터로 촬영했습니다. API와 위치는 목업입니다. 위젯 내부는 수정하지 않고 제목과 크기 설명만 덧붙였습니다. 크기는 DIP 기준이며 XL은 별도 배율입니다.</p><nav>${kinds.map(([slug,title])=>`<a href="#${slug}">${title}</a>`).join('')}</nav>${kinds.map(([slug,title],k)=>`<section id="${slug}"><h2>${title}</h2><a href="${slug}-sizes.png"><img src="${slug}-sizes.png" alt="${title} S·Slim·M·L 실제 크기별 화면"></a><p>원본: ${sizes.slice(0,k===8?5:4).map(size=>`<a href="${slug}-${size.toLowerCase()}.png">${size}</a>`).join(' · ')}</p>${k===8?'<a href="calendar-xl.png"><img src="calendar-xl.png" alt="캘린더 XL 전체 월간 일정"></a>':''}</section>`).join('')}<p><a href="../../../README.md">사용자 매뉴얼</a> · <a href="captures.json">촬영 기록과 해시</a></p></html>`;
  fs.writeFileSync(path.join(output,'gallery.html'),gallery);
  fs.writeFileSync(path.join(output,'captures.json'),JSON.stringify({version,sourceCommit,requiredUsageCaptures,composedAt:new Date().toISOString(),conditions:{platform:'Windows 11 x64',api:'mock',data:'sample',dpi:144,scale:100,widgetBackgroundTransparency:0,backdrop:'#edf4fa',composition:'native screenshots uniformly scaled; widget contents unedited'},captures,boards},null,2)+'\n');
  await send('Page.navigate',{url:pathToFileURL(path.join(output,'gallery.html')).href});
  await sleep(150);
  if(!await evaluate("Promise.all([...document.images].map(im=>im.decode().then(()=>true,()=>false))).then(a=>a.length===10&&a.every(Boolean))"))throw Error('Gallery image decode failed');
  for(const width of [1200,390]){
    await send('Emulation.setDeviceMetricsOverride',{width,height:900,deviceScaleFactor:1,mobile:false});await sleep(100);
    if(!await evaluate('document.documentElement.scrollWidth<=innerWidth'))throw Error('Gallery horizontal overflow at '+width);
  }
  console.log('PASS gallery: all 10 images decode, desktop/mobile fit');
  await send('Browser.close').catch(()=>{});
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>{clearTimeout(watchdog);socket?.close();browser.kill();});
