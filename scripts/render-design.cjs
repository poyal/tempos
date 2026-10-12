// Reproducible native HTML/CSS design boards; requires local Chrome and Node 22+.
const fs = require('node:fs');
const path = require('node:path');
const {spawn} = require('node:child_process');
const {pathToFileURL} = require('node:url');
const root = path.resolve(__dirname, '..');
const folder = path.join(root, 'design/final-2026-10-12');
const output = path.join(root, 'artifacts/final-design-render');
fs.mkdirSync(output, {recursive:true});
const profile = path.join(output, 'profile-' + Date.now());
const browser = spawn(process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe',
  ['--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check', '--disable-background-networking',
   '--disable-extensions', '--disable-sync', '--disable-component-update', '--remote-debugging-port=0', '--user-data-dir='+profile, 'about:blank'],
  {windowsHide:true, stdio:'ignore'});
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
let socket;
const timeout = setTimeout(() => { console.error('Design rendering timed out'); socket?.close(); browser.kill(); process.exit(1); }, 55000);
(async () => {
  let active;
  for (let i=0;i<100;i++) { try { active=fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8'); break; } catch {} await sleep(100); }
  if (!active) throw Error('Chrome did not start');
  const targets=await (await fetch('http://127.0.0.1:'+active.split('\n')[0]+'/json/list')).json();
  socket=new WebSocket(targets.find(t=>t.type==='page').webSocketDebuggerUrl);
  await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
  let next=0; const pending=new Map(), errors=[];
  socket.addEventListener('message', e=>{const m=JSON.parse(e.data);if(m.method==='Runtime.exceptionThrown') errors.push(m.params.exceptionDetails);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result);}});
  socket.addEventListener('close',()=>{for(const p of pending.values())p.reject(Error('Chrome disconnected'));pending.clear();});
  const send=(method,params={})=>new Promise((resolve,reject)=>{let id=++next;pending.set(id,{resolve,reject});socket.send(JSON.stringify({id,method,params}));});
  const evaluate=async expression=>{let r=await send('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;};
  await send('Page.enable');await send('Runtime.enable');
  await send('Emulation.setDeviceMetricsOverride',{width:1672,height:941,deviceScaleFactor:1,mobile:false});
  const boards={clock:'02-clock',cpu:'03-cpu',gpu:'04-gpu',memory:'05-memory',system:'06-system',network:'07-network',disk:'08-disk',calendar:'10-calendar',overview:'13-desktop-grid',settings:'14-widget-settings',monthly:'15-calendar-monthly',rules:'18-common-rules'};
  const report=[];
  for(const [kind,file] of Object.entries(boards)) {
    await send('Page.navigate',{url:pathToFileURL(path.join(folder,'boards.html')).href+'?board='+kind});
    for(let i=0;i<100;i++){if(await evaluate("document.querySelector('.foot')!==null"))break;await sleep(50);}
    await evaluate('document.fonts.ready'); await sleep(80);
    const violations=await evaluate(`(()=>{let issues=[];for(const card of document.querySelectorAll('.card[data-kind]')){const size=card.dataset.size, icon=card.querySelector('[data-header-icon]');if(!icon||parseFloat(icon.style.left)!==(size==='Slim'?17:20)||parseFloat(icon.style.top)!==(size==='Slim'?26:20)||parseFloat(icon.style.width)!==20)issues.push('Header anchor '+card.dataset.kind+'/'+size);for(const t of card.querySelectorAll('.text')){if(t.offsetLeft<0||t.offsetTop<0||t.offsetLeft+t.offsetWidth>card.offsetWidth+1||t.offsetTop+t.offsetHeight>card.offsetHeight+3)issues.push('Bounds '+card.dataset.kind+'/'+size+': '+t.textContent);}}return issues;})()`);
    if(violations.length)throw Error(violations.join('\n'));
    const shot=await send('Page.captureScreenshot',{format:'png'});
    fs.writeFileSync(path.join(folder,file+'.png'),Buffer.from(shot.data,'base64'));
    report.push({kind,file:file+'.png',bounds:true,commonHeader:true});console.log('PASS '+kind+' / common header and bounds');
  }
  if(errors.length)throw Error(JSON.stringify(errors));
  fs.writeFileSync(path.join(output,'checks.json'),JSON.stringify(report,null,2)+'\n');
  await send('Browser.close').catch(()=>{});
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(()=>{clearTimeout(timeout);socket?.close();browser.kill();});
