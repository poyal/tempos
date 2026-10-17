// Offline report. Screenshots are unchanged; HTML overlays identify modified regions.
// node scripts/layout-change-report.cjs <ui-review-folder> <temperature-folder> <windows-real-folder> <weather-folder>
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root=process.cwd(),[ui,thermal,windows,weather]=process.argv.slice(2).map(p=>path.resolve(p));
const output=path.join(root,'docs/validation/2026-10-17-layout-report.html');
const old=path.join(root,'test-results/e2e-20261017-134558-597');
const previousThermal=path.join(root,'test-results/e2e-20261017-134901-062');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const sources=[];
function shot(file,label,boxes=[],dim=[336,160]){
  const bytes=fs.readFileSync(file);
  if(dim[0]===842)dim=[bytes.readUInt32BE(16)/1.5,bytes.readUInt32BE(20)/1.5];
  sources.push({file:path.relative(root,file).replaceAll('\\','/'),sha256:hash(file)});
  const overlay=boxes.length?'<svg class="marks" viewBox="0 0 '+dim.join(' ')+'" aria-label="빨간 테두리: 변경 영역">'+boxes.map(([x,y,w,h,n])=>
    '<rect x="'+x+'" y="'+y+'" width="'+w+'" height="'+h+'" rx="3"/><circle cx="'+(x+4)+'" cy="'+(y+4)+'" r="7"/><text x="'+(x+4)+'" y="'+(y+7)+'">'+n+'</text>').join('')+'</svg>':'';
  return '<div class="shot" style="max-width:'+dim[0]*1.5+'px"><img loading="lazy" src="data:image/png;base64,'+bytes.toString('base64')+'" alt="'+esc(label)+'">'+overlay+'</div>';
}
const kinds=[
  ['clock','시계','디지털 시간·날짜·요일·초/시간대 설정을 유지했습니다.'],
  ['weather','날씨','지역·현재 기온·날씨 배경·공급자·섭씨/화씨 설정을 유지했습니다.'],
  ['cpu','CPU','사용률 수집·0~100% 그래프·L 평균/최대·크기·색상을 유지했습니다.'],
  ['gpu','GPU','GPU 선택·사용률 수집·0~100% 그래프·L 평균/최대를 유지했습니다.'],
  ['memory','메모리','사용률·사용/전체 용량·60초 데이터·L 평균/최대를 유지했습니다.'],
  ['system','시스템 정보','PC·OS·CPU·GPU·가동 시간과 표시 옵션을 유지했습니다.'],
  ['network','네트워크','수신/송신 수집·전체/개별 어댑터·그래프·속도 단위를 유지했습니다.'],
  ['disk','디스크','볼륨 선택·용량·여유 공간·L 전체 목록·자동 페이지 순환을 유지했습니다.'],
  ['calendar','캘린더','S/Slim/M/L/XL·Google 연동·일정·월 이동·시간대 설정을 유지했습니다.']
];
const sizes=['S','Slim','M','L','XL'],dims=[[160,160],[336,72],[336,160],[336,336],[1008,840]];
function changes(k,s){
  if(k===1&&s>=2)return [
    '① 날씨 상태와 최저·최고 기온을 하단 한 줄에 맞췄습니다. 상태는 왼쪽, 최저·최고는 오른쪽에 정렬했습니다.'+
    (s===3?' ② 시간별 예보를 ‘앞으로 4일’로 교체했습니다. 내일부터 날짜·요일·날씨 아이콘·최고/최저·최대 강수확률을 표시하며 자료가 없는 날짜는 ‘예보 없음’으로 남깁니다.':''),
    [[17,116,302,28,1],...(s===3?[[17,153,302,165,2]]:[])]];
  if([2,3].includes(k)&&s<3){
    if(s===2)return ['① 온도를 사용률 오른쪽으로 옮겼습니다. 모델명은 맨 아래 전체 폭 한 줄로 유지했습니다.',[[17,48,156,76,1]]];
    return ['① 작은 '+sizes[s]+' 크기에서는 모델명을 숨겼습니다. 사용률·온도·그래프/막대는 유지하며 전체 모델명은 접근성 요약에서 확인할 수 있습니다.',
      [s===0?[17,134,126,23,1]:[14,53,308,17,1]]];
  }
  if(k===6&&s===2)return ['① 최근 60초 문구 아래 여백을 확보하고 그래프를 아래로 내렸습니다. 수신/송신 값과 그래프 기간 문구는 유지했습니다.',[[189,72,130,72,1]]];
  if(k===8&&s===0)return ['① 연도와 월을 날짜 오른쪽에 나란히 배치했습니다. 아래 일정 표시는 유지했습니다.',[[17,46,126,83,1]]];
  if(k===8&&s===4)return ['① 요일 표의 위쪽에 빠져 있던 가로 테두리를 추가했습니다. 월 이동·일정·나머지 표는 유지했습니다.',[[20,58,968,12,1]]];
  return ['변경 없음. '+kinds[k][2],[]];
}
const runs=[ui,thermal,windows,weather].map(folder=>{
  const result=read(path.join(folder,'results.json'));
  if(!result.completed||result.results.some(r=>r.passed===false))throw Error('Completed passing run required: '+folder);
  return {folder:path.relative(root,folder).replaceAll('\\','/'),...result};
});
if(new Set(runs.map(r=>r.executableSha256)).size!==1 ||
   runs[0].executableSha256.toLowerCase()!==hash(path.join(root,'out/release/Tempos.TestHost.exe')))
  throw Error('All final runs must use the current TestHost build');
const failedRuns=['test-results/e2e-20261017-141303-324','test-results/e2e-20261017-141859-278']
  .map(folder=>({folder,...read(path.join(root,folder,'results.json'))}));
let changed=0,same=0;
const sections=kinds.map(([slug,title,kept],k)=>{
  let cards='';
  for(let s=0;s<(k===8?5:4);s++){
    const [description,boxes]=changes(k,s),altered=boxes.length>0;altered?changed++:same++;
    cards+='<article class="comparison" data-state="'+(altered?'changed':'same')+'" data-kind="'+slug+'"><div class="row-title"><h3>'+title+' <span>'+sizes[s]+'</span></h3><b class="'+(altered?'changed':'unchanged')+'">'+(altered?'변경':'변경 없음')+'</b></div><div class="pair"><figure><figcaption>AS-IS <small>2026-10-17 · 1차 수정</small></figcaption>'+
      shot(path.join(old,'design-kind-'+k+'-size-'+s+'.png'),'변경 전 '+title+' '+sizes[s],[],dims[s])+
      '</figure><figure><figcaption class="'+(altered?'changed':'')+'">TO-BE <small>2026-10-17 · 2차 수정</small></figcaption>'+
      shot(path.join(ui,'design-kind-'+k+'-size-'+s+'.png'),'변경 후 '+title+' '+sizes[s],boxes,dims[s])+
      '</figure></div><p class="'+(altered?'changed':'')+'">'+description+'</p>'+(altered?'<p class="kept">유지: '+kept+'</p>':'')+'</article>';
  }
  return '<section class="widget-section" id="'+slug+'"><h2>'+title+'</h2>'+cards+'</section>';
}).join('');
const settings='<div class="pair"><figure><figcaption>AS-IS · 앱 설정</figcaption>'+
  shot(path.join(previousThermal,'temperature-settings.png'),'1차 CPU 온도 설정',[],[842,695])+
  '</figure><figure><figcaption>TO-BE · CPU 온도 설정 유지</figcaption>'+
  shot(path.join(thermal,'temperature-settings.png'),'2차 CPU 온도 설정 유지',[],[842,695])+'</figure></div>'+
  '<div class="pair" style="margin-top:24px"><figure><figcaption>AS-IS · 디스크 설정</figcaption>'+
  shot(path.join(old,'settings-kind-7.png'),'기존 디스크 설정',[],[842,695])+
  '</figure><figure><figcaption class="changed">TO-BE · 사용 가능한 드라이브만 선택 목록에 포함</figcaption>'+
  shot(path.join(windows,'windows-disk-picker-expanded.png'),'실제 Windows 디스크 선택 목록',[[250,382,557,130,1]],[842,695])+'</figure></div>';
const references=[['날씨 L',[[1,3]]],['CPU / GPU M',[[2,2],[3,2]]],['CPU L · 유지',[[2,3]]],
  ['네트워크 M',[[6,2]]],['디스크 M · 표시 유지 / 선택 목록 변경',[[7,2]]],['캘린더 S',[[8,0]]],
  ['캘린더 XL',[[8,4]]],['CPU Slim',[[2,1]]]].map(([title,ids],index)=>
  '<div class="panel"><h3>'+title+'</h3><div class="pair"><figure><figcaption>AS-IS · 사용자가 제공한 원본</figcaption>'+
  shot(path.join(root,'docs/validation/layout-2026-10-17-v2-input','request-'+(index+1)+'.png'),title+' 요청 원본',[],dims[ids[0][1]])+
  '</figure><figure><figcaption class="'+(ids.some(([k,s])=>changes(k,s)[1].length)?'changed':'')+'">TO-BE · '+
  (ids.some(([k,s])=>changes(k,s)[1].length)?'수정한':'유지한')+' 실제 UI / 예제 데이터</figcaption>'+
  ids.map(([k,s])=>'<div style="margin-bottom:18px">'+shot(path.join(ui,'design-kind-'+k+'-size-'+s+'.png'),kinds[k][1]+' 변경 후',changes(k,s)[1],dims[s])+'</div>').join('')+
  '</figure></div></div>').join('');
const edge=[
  ['layout-example-2-1-0','CPU Slim · 100% / 105°C · 예제'],
  ['layout-example-3-2-0','GPU M · 긴 모델명 / 105°C · 예제'],
  ['layout-example-2-temperature-30','CPU 미지원 · 온도 — · 예제'],
  ['layout-example-2-temperature-32','CPU 오래된 샘플 · 온도 — · 예제'],
  ['layout-example-2-no-graph','CPU 그래프 끔 · 예제'],
  ['layout-example-weather-fahrenheit-negative','날씨 화씨 변환 · 예제'],
  ['layout-example-weather-stale','날씨 갱신 지연 · 예제']
].map(([file,label])=>'<figure><figcaption>'+label+'</figcaption>'+shot(path.join(thermal,file+'.png'),label,[],file.includes('-1-0')?[336,72]:[336,160])+'</figure>').join('')+
  [['design-weather-daily-partial','4일 중 2일만 제공된 응답 · HTTP 목업'],
   ['design-weather-daily-missing','날짜별 예보가 없는 응답 · HTTP 목업']]
  .map(([file,label])=>'<figure><figcaption>'+label+'</figcaption>'+shot(path.join(ui,file+'.png'),label,[],[336,336])+'</figure>').join('');
const isSkip=r=>Boolean(r.skipped||r.status==='SKIP');
const rows=runs.map(r=>'<tr><td>'+esc(r.mode)+'</td><td>'+r.results.filter(x=>x.passed===true&&!isSkip(x)).length+
  ' PASS / '+r.results.filter(isSkip).length+' SKIP</td><td>'+esc(r.apiMode)+' / '+esc(r.systemMode)+'</td><td><code>'+esc(r.folder)+'</code></td></tr>').join('');
const skips=runs.flatMap(r=>r.results.filter(isSkip).map(x=>'<li>'+esc(r.mode)+': '+esc(x.name)+' — '+esc(x.reason||x.detail||'')+'</li>')).join('');
const behavior=[
  ['날씨 데이터','다음 4시간 예보','Open-Meteo는 오늘 포함 5일을 요청하고 내일부터 4일 표시. 기상청은 날짜별 최저·최고, 가장 강한 날씨, 최대 강수확률을 집계. 누락은 예보 없음.',true],
  ['날씨 캐시','현재 날씨·시간별 자료','4일 예보를 보호된 캐시에 저장. 재시작·통신 실패 때 유지하고 자정이 지나면 지난 날짜를 제거. 구버전 Open-Meteo 캐시는 새 필드 갱신 요청.',true],
  ['디스크 선택','읽을 수 없는 볼륨도 선택 목록에 노출','실제 용량 조회가 성공한 로컬 드라이브만 선택 가능. 빈 이동식·잠김·제거·접근 불가 볼륨 제외. ‘여유 공간만 보기’ 옵션은 추가하지 않았습니다.',true],
  ['디스크 표시','사용 가능 용량·사용량·전체 용량·L 목록','표시 구성은 유지. L 전체 목록의 조회 불가 상태도 유지하며 선택 목록에서만 사용 불가 항목을 제외.',false],
  ['CPU/GPU 온도','M은 사용률 아래, L은 아래, Slim은 옆','M을 사용률 옆으로 이동. L·S·Slim의 온도 위치와 센서 수집 방식은 유지.',true],
  ['CPU/GPU 모델명','모든 크기에서 최하단 표시','S·Slim에서는 숨김. M·L의 최하단 모델명과 접근성 전체 이름은 유지.',true],
  ['센서 설정·설치·오류 처리','1차 수정의 CPU 선택 설치·GPU D3DKMT·미지원 —·센서 테스트 오류창 수정','변경 없음. 센서 도우미 테스트와 온도 UI 회귀 검사를 다시 실행했습니다.',false],
  ['저장·공유','CPU 옵션 로컬 저장 / 내보내기 제외','변경 없음. 위젯 크기·단위·선택 장치·시작 등록과 저장/재시작 동작 유지.',false],
  ['검사·보고서','1차 보고서와 이전 검증 결과','1차 보고서를 보존하고 37개 구성을 다시 촬영. 새 비교 기준은 1차 → 2차이며 수정하지 않은 26개 구성도 전후 캡처를 포함.',true],
  ['공통 디자인','9개 테마·색상·아이콘·모서리·크기·투명도','변경 없음. 기존 설정 유지.',false],
  ['Google·위치 연동','Google Calendar/OAuth, 위치 선택','변경 없음. 이번 일반 검사는 외부 API 목업만 사용. 실제 API 검사나 실제 위치 조회는 실행하지 않음.',false],
  ['기존 설치와 데이터','사용자 앱·배치·시작 등록','변경 없음. 설치 앱을 종료/업데이트/제거하지 않고 TestHost와 실행별 폴더에서 검사.',false]
].map(([a,b,c,d])=>'<tr><td>'+a+'</td><td>'+b+'</td><td class="'+(d?'changed':'')+'">'+c+'</td></tr>').join('');
let html=fs.readFileSync(path.join(root,'scripts/layout-change-report.template.html'),'utf8');
const tokens={
  CHANGED:changed,SAME:same,SECTIONS:sections,SETTINGS:settings,REFERENCES:references,EDGE:edge,ROWS:rows,SKIPS:skips,BEHAVIOR:behavior,
  CORE:fs.readFileSync(path.join(root,'artifacts/layout-v2-core-tests.log'),'utf8').match(/PASS (\d+) checks/)[1],
  OPTIONS:kinds.map(([slug,title])=>'<option value="'+slug+'">'+title+'</option>').join(''),
  NAV:kinds.map(([slug,title])=>'<a href="#'+slug+'">'+title+'</a>').join(''),
  REAL:[2,3].map(k=>'<figure><figcaption>'+kinds[k][1]+' · 실제 로컬 Windows 수집</figcaption>'+shot(path.join(windows,'windows-live-kind-'+k+'.png'),'실제 로컬 '+kinds[k][1])+'</figure>').join(''),
  SOURCES:esc(JSON.stringify(sources,null,2)),
  HASHES:runs.map(r=>'<p class="fine hash">'+esc(r.mode)+': '+esc(r.executableSha256)+'</p>').join('')
};
html=html.replace(/\{\{([A-Z]+)\}\}/g,(_,key)=>tokens[key]??(()=>{throw Error('Unknown token '+key)})());
fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,html);
fs.writeFileSync(output.replace('.html','.json'),JSON.stringify({generatedAt:new Date().toISOString(),changed,unchanged:same,layouts:37,
  executableSha256:hash(path.join(root,'out/release/Tempos.exe')),coreChecks:Number(tokens.CORE),sources,runs,failedRuns},null,2)+'\n');
console.log(JSON.stringify({output,layouts:changed+same,changed,unchanged:same,images:sources.length,bytes:Buffer.byteLength(html)}));
