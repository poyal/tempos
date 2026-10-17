// node scripts/layout-refinement-report.cjs <ui-review-folder> <temperature-folder>
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root=process.cwd(),[ui,thermal]=process.argv.slice(2).map(p=>path.resolve(p));
const before=path.join(root,'test-results/e2e-20261017-141511-377');
const priorThermal=path.join(root,'test-results/e2e-20261017-141642-654');
const output=path.join(root,'docs/validation/2026-10-17-layout-report.html');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const runs=[ui,thermal].map(folder=>({folder:path.relative(root,folder).replaceAll('\\','/'),...read(path.join(folder,'results.json'))}));
for(const run of runs)
  if(!run.completed||run.results.some(r=>r.passed===false)||run.apiMode!=='mock'||
     run.executableSha256.toLowerCase()!==hash(path.join(root,'out/release/Tempos.TestHost.exe')))
    throw Error('Completed passing mock-API run of current build required: '+run.folder);
const sources=[];
function shot(file,label,boxes=[],dim=[336,160]){
  const bytes=fs.readFileSync(file);
  if(dim[0]===842)dim=[bytes.readUInt32BE(16)/1.5,bytes.readUInt32BE(20)/1.5];
  sources.push({file:path.relative(root,file).replaceAll('\\','/'),sha256:hash(file)});
  const overlay=boxes.length?'<svg class="marks" viewBox="0 0 '+dim.join(' ')+'" aria-label="빨간 테두리: 변경 영역">'+
    boxes.map(([x,y,w,h,n])=>'<rect x="'+x+'" y="'+y+'" width="'+w+'" height="'+h+'" rx="3"/><circle cx="'+(x+4)+'" cy="'+(y+4)+'" r="7"/><text x="'+(x+4)+'" y="'+(y+7)+'">'+n+'</text>').join('')+'</svg>':'';
  return '<div class="shot" style="max-width:'+dim[0]*1.5+'px"><img loading="lazy" src="data:image/png;base64,'+bytes.toString('base64')+'" alt="'+esc(label)+'">'+overlay+'</div>';
}
const kinds=[['clock','시계'],['weather','날씨'],['cpu','CPU'],['gpu','GPU'],['memory','메모리'],
  ['system','시스템 정보'],['network','네트워크'],['disk','디스크'],['calendar','캘린더']];
const sizes=['S','Slim','M','L','XL'],dims=[[160,160],[336,72],[336,160],[336,336],[1008,840]];
const retained=[
  '시간·날짜·요일·초/12시간제·시간대와 전체 배치',
  '현재 날씨·상태/최저·최고 한 줄·4일 데이터·단위·공급자·캐시',
  '글자 크기·가로 위치·온도 수집·모델명·그래프·최근 60초·L 평균/최대',
  'GPU 선택·글자 크기·가로 위치·온도 수집·모델명·그래프·최근 60초·L 평균/최대',
  '사용률·사용/전체 용량·그래프·최근 60초·L 평균/최대',
  'PC·OS·CPU/GPU·가동 시간과 표시 옵션',
  '수신/송신·전체/개별 어댑터·단위·그래프와 상단 기간',
  '사용 가능한 드라이브 선택·여유/사용/전체 용량·L 목록·자동 순환',
  '일정·월 이동·주 시작 요일·시간대·Google 연동·XL 위 테두리'
];
function change(k,s){
  if([2,3].includes(k)){
    const layout=s===2?'사용률과 온도를 함께 24 DIP 아래로 내렸습니다. ':s===0||s===3?'값 앞의 ‘온도’ 문구를 제거했습니다. ':'';
    return ['① '+layout+'사용률과 온도 각각에 흰색·주황색·빨간색 표시 기준을 추가했습니다. 아래 기본 예제는 흰색 구간이며 높은 값의 화면은 색상 검증에서 확인할 수 있습니다.',
      [s===1?[44,30,137,26,1]:s===2?[17,72,156,54,1]:[17,49,s===0?126:156,78,1]]];
  }
  if(k===4)return ['① 현재 사용률과 사용량 막대에 80%부터 주황색·90%부터 빨간색을 적용했습니다. 현재 예제 값은 흰색 구간입니다. 그래프·평균·최대는 유지했습니다.',
    [s===1?[44,30,275,24,1]:[17,49,s===0?126:156,80,1]]];
  if(k===7)return ['① 사용률 숫자·용량 막대에 80%부터 주황색·90%부터 빨간색을 적용했습니다. L은 각 드라이브 막대를 개별 판정합니다. 여유 용량 숫자·드라이브 선택은 유지했습니다.',
    [s===1?[140,45,179,10,1]:s===0?[17,49,126,100,1]:s===2?[201,92,118,49,1]:[17,90,302,210,1]]];
  if(k===1&&s===3)return ['① ‘앞으로 4일’ 제목을 제거하고 날짜·아이콘·기온 사이의 세로 간격을 정리했습니다. 다음 4일 예보 데이터는 그대로 표시합니다.',[[17,153,302,165,1]]];
  if(k===8&&s===0)return ['① 연도를 제거했습니다. 월을 왼쪽에 작게, 날짜를 오른쪽에 크게 배치해 ‘10월 17’ 순서로 읽히게 했습니다. 날짜 글자 크기는 42→46 DIP입니다.',[[17,48,126,78,1]]];
  return ['변경 없음. '+retained[k]+'을 유지했습니다.',[]];
}
let changed=0,same=0;
const sections=kinds.map(([slug,title],k)=>{
  let cards='';
  for(let s=0;s<(k===8?5:4);s++){
    const [description,boxes]=change(k,s),altered=boxes.length>0;
    altered?changed++:same++;
    const name='design-kind-'+k+'-size-'+s+'.png';
    cards+='<article class="comparison" data-state="'+(altered?'changed':'same')+'" data-kind="'+slug+'"><div class="row-title"><h3>'+title+' <span>'+sizes[s]+'</span></h3><b class="'+(altered?'changed':'unchanged')+'">'+(altered?'변경':'변경 없음')+'</b></div><div class="pair"><figure><figcaption>AS-IS <small>2차 수정</small></figcaption>'+
      shot(path.join(before,name),'2차 '+title+' '+sizes[s],[],dims[s])+
      '</figure><figure><figcaption class="'+(altered?'changed':'')+'">TO-BE <small>3차 수정 · 새 캡처</small></figcaption>'+
      shot(path.join(ui,name),'3차 '+title+' '+sizes[s],boxes,dims[s])+
      '</figure></div><p class="'+(altered?'changed':'')+'">'+description+'</p>'+
      (altered?'<p class="kept">유지: '+retained[k]+'</p>':'')+'</article>';
  }
  return '<section class="widget-section" id="'+slug+'"><h2>'+title+'</h2>'+cards+'</section>';
}).join('');
const references=[['CPU / GPU M',[[2,2],[3,2]]],['날씨 L',[[1,3]]],['캘린더 S',[[8,0]]]].map(([title,ids],i)=>
  '<div class="panel"><h3>'+title+'</h3><div class="pair"><figure><figcaption>AS-IS · 요청 스크린샷</figcaption>'+
  shot(path.join(root,'docs/validation/layout-2026-10-17-v3-input','request-'+(i+1)+'.png'),title+' 요청 원본',[],dims[ids[0][1]])+
  '</figure><figure><figcaption class="changed">TO-BE · 실제 TestHost / 예제 데이터</figcaption>'+
  ids.map(([k,s])=>shot(path.join(ui,'design-kind-'+k+'-size-'+s+'.png'),kinds[k][1]+' 최신 배치',change(k,s)[1],dims[s])).join('<br>')+
  '</figure></div></div>').join('');
const edges=[
  [thermal,'layout-example-2-2-0','CPU M · 100% / 105°C',2],
  [thermal,'layout-example-3-2-0','GPU M · 100% / 105°C',2],
  [thermal,'layout-example-2-2-2','CPU M · 200% 배율',2],
  [thermal,'layout-example-2-no-graph','CPU M · 그래프 끔',2],
  [thermal,'layout-example-3-no-graph','GPU M · 그래프 끔',2],
  [thermal,'layout-example-2-temperature-30','CPU M · 미지원 온도 —',2],
  [ui,'design-weather-daily-partial','날씨 L · 일부 날짜 누락',3],
  [ui,'design-weather-daily-missing','날씨 L · 일별 예보 없음',3]
].map(([folder,name,label,s])=>'<figure><figcaption>'+label+' · 예제/목업</figcaption>'+shot(path.join(folder,name+'.png'),label,[],dims[s])+'</figure>').join('');
const settings='<div class="pair"><figure><figcaption>AS-IS · CPU 온도 설정</figcaption>'+
  shot(path.join(priorThermal,'temperature-settings.png'),'2차 온도 설정',[],[842,695])+
  '</figure><figure><figcaption>TO-BE · 설정 변경 없음</figcaption>'+
  shot(path.join(thermal,'temperature-settings.png'),'3차 온도 설정',[],[842,695])+'</figure></div>';
const isSkip=r=>Boolean(r.skipped||r.status==='SKIP');
const firstAttemptFolder='test-results/e2e-20261017-151026-885';
const firstAttempt=read(path.join(root,firstAttemptFolder,'results.json'));
const priorAttempts=[{folder:firstAttemptFolder,mode:firstAttempt.mode,executableSha256:firstAttempt.executableSha256,
  failures:firstAttempt.results.filter(r=>r.passed===false),
  cause:'디스크 막대의 90% 불투명도를 고려하지 않은 픽셀 검사 기대값. 실제 트랙 배경과 합성한 색으로 기대값을 수정했으며 앱 색상 코드는 변경하지 않음.'}];
const coreLog='artifacts/layout-v3-color-core.log';
const coreChecks=Number(fs.readFileSync(coreLog,'utf8').match(/PASS (\d+) checks/)?.[1]);
if(!coreChecks)throw Error('Passing current native test log required');
const colorEvidence=[2,3,4,7].map(k=>'<div class="panel"><h3>'+kinds[k][1]+' · 실제 색상 캡처</h3><div class="evidence">'+
  [[35,'79% / 79°C · 흰색'],[33,'80% / 80°C · 주황색'],[34,'95% / 90°C · 빨간색'],[36,'오래된 값 / 온도 — · 흰색']].map(([n,label])=>
    '<figure><figcaption>'+esc(kinds[k][1]+' · '+(k===2||k===3?label:label.replace(/ \/ [0-9]+°C| \/ 온도 —/g,'')))+'</figcaption>'+
    shot(path.join(thermal,'layout-example-color-'+k+'-'+n+'.png'),'색상 검증 예제 '+k+' '+n,[],dims[2])+'</figure>').join('')+'</div></div>').join('');
const colors='<section class="widget-section" id="colors"><h2>값에 따른 표시 색상</h2><div class="panel"><p>하드웨어마다 최대 온도가 달라 아래 값은 제조사 공통 규격이 아닌 Tempos의 표시 기준입니다. 흰색은 기본 표시, 주황·빨강은 값이 높은 정도를 뜻하며 고장·안전을 판정하지 않습니다. 보고서의 빨간 점선은 변경 영역, 위젯 안의 붉은 숫자는 실제 앱의 높은 값 표시입니다.</p>'+
  '<div class="table-scroll"><table><thead><tr><th>지표</th><th>흰색</th><th>주황색</th><th>빨간색</th></tr></thead><tbody><tr><td>CPU·GPU 온도</td><td>80°C 미만</td><td>80°C 이상, 90°C 미만</td><td>90°C 이상</td></tr><tr><td>CPU·GPU 사용률</td><td>80% 미만</td><td>80% 이상, 95% 미만</td><td>95% 이상</td></tr><tr><td>메모리·디스크 사용률</td><td>80% 미만</td><td>80% 이상, 90% 미만</td><td>90% 이상</td></tr></tbody></table></div>'+
  '<p>현재값과 막대에만 적용했습니다. 미지원 온도는 흰색 —, 오래된 사용률은 흰색으로 돌아갑니다. 고대비 모드는 Windows 글자색을 우선합니다. 날씨 기온·강수확률·네트워크 속도·과거 그래프·평균·최대에는 이 기준을 적용하지 않습니다. 주황 #FFCF8A · 빨강 #FFA0A0.</p>'+
  '<p>참고: <a href="https://www.amd.com/de/products/processors/desktops/ryzen/9000-series/amd-ryzen-7-9700x.html">AMD 9700X 사양</a>의 Tjmax는 95°C, <a href="https://www.nvidia.cn/en-us/geforce/graphics-cards/40-series/rtx-4070-4070ti/">NVIDIA 4070 사양</a>의 최대 온도는 90°C입니다. <a href="https://www.intel.com/content/www/us/en/support/articles/000005597/processors.html">Intel 온도 안내</a>도 모델·설계·부하에 따라 온도가 다르고 온도만으로 문제를 판단할 수 없다고 설명합니다. 이 자료는 온도 기준이 기기별로 다르다는 근거이며 위 앱 색상 임계값의 공식 인증이 아닙니다. 실제 최대 온도 자동 탐지는 추가하지 않았습니다.</p></div>'+colorEvidence+'</section>';
const count=run=>run.results.filter(r=>r.passed===true&&!isSkip(r)).length;
const rows=runs.map(run=>'<tr><td>'+esc(run.mode)+'</td><td>'+count(run)+' PASS / '+run.results.filter(isSkip).length+' SKIP</td><td>'+esc(run.apiMode)+' / '+esc(run.systemMode)+'</td><td><code>'+esc(run.folder)+'</code></td></tr>').join('');
const template=fs.readFileSync(path.join(root,'scripts/layout-change-report.template.html'),'utf8');
const style=template.match(/<style>([\s\S]*?)<\/style>/)[1],js=template.match(/<script>([\s\S]*?)<\/script>/)[1];
const toolbar='<div class="toolbar"><button class="active" data-filter="all">전체 37</button><button data-filter="changed">변경 '+changed+'</button><button data-filter="same">변경 없음 '+same+'</button><select id="kind" aria-label="위젯 종류"><option value="all">모든 위젯</option>'+
  kinds.map(([slug,title])=>'<option value="'+slug+'">'+title+'</option>').join('')+'</select><span id="shown">37개 표시</span><label><input id="marks" type="checkbox" checked> 빨간 변경 영역 표시</label></div>';
const html='<!doctype html><html lang="ko"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Tempos 3차 수정 · As-is / To-be</title><style>'+style+'</style></head><body>'+
  '<header><div class="eyebrow">TEMPOS / CHANGE REPORT / 2026.10.17</div><h1>숫자 배치·상태 색상·캘린더 개선<br>As-is → To-be</h1><p class="intro">CPU·GPU M의 사용률과 온도를 아래로 내리고 모든 크기에서 값 앞의 ‘온도’ 문구를 없앴습니다. 높은 온도·사용률에 주황색과 빨간색을 적용했습니다. 날씨 L은 예보 제목을 없애고, 캘린더 S는 연도를 빼고 월·날짜를 한 줄로 모았습니다.</p>'+
  '<div class="stats"><div class="stat"><strong>37</strong>전후 비교</div><div class="stat changed"><strong>'+changed+'</strong>변경 구성</div><div class="stat"><strong>'+same+'</strong>변경 없음</div></div>'+
  '<div class="notice"><strong class="changed">빨간 설명·테두리 = 이번에 바꾼 내용과 위치</strong><p>2차 → 3차 수정 비교입니다. 변경하지 않은 구성도 실제 앱에서 다시 촬영했습니다. 캡처는 예제/목업이며 수치·날짜 차이는 레이아웃 변경이 아닙니다. 빨간 표시는 HTML 주석이고 원본 이미지는 수정하지 않았습니다.</p><p class="fine"><a href="2026-10-17-layout-report-v2.html">2차 보고서</a> · <a href="2026-10-17-layout-report-v1.html">1차 보고서</a> · 이전 수정·검증·실패 기록도 보존했습니다.</p></div></header><main>'+
  toolbar+'<section class="widget-section" id="request"><h2>요청한 화면과 비교</h2>'+references+'</section><nav>'+
  kinds.map(([slug,title])=>'<a href="#'+slug+'">'+title+'</a>').join('')+'<a href="#colors">색상 기준</a><a href="#validation">검증 범위</a></nav>'+sections+colors+
  '<section class="widget-section" id="settings"><h2>설정과 내부 동작 · 유지</h2><div class="panel">'+settings+
  '<p>센서 수집·온도 미지원 처리, 날씨 파서·4일 예보·캐시, 사용 가능한 드라이브 선택, Google/OAuth·위치 연동, 저장·재시작·내보내기와 공통 테마·투명도는 변경하지 않았습니다. 앱의 변경 범위는 문구·좌표·날짜 글자 크기와 현재 수치의 색상 규칙입니다.</p></div></section>'+
  '<section class="widget-section" id="edge"><h2>배율·경계 조건</h2><div class="panel"><p>온도·사용률은 배치 검사용 예제입니다. 100/150/200%와 긴 모델명·미지원/오래됨·그래프 끔을 검사했습니다.</p><div class="evidence">'+edges+'</div></div></section>'+
  '<section class="widget-section" id="validation"><h2>이번 검증과 제한</h2><div class="panel"><div class="table-scroll"><table><thead><tr><th>검사</th><th>결과</th><th>API / 시스템</th><th>실행 기록</th></tr></thead><tbody>'+rows+'</tbody></table></div>'+
  '<p>이번 E2E 합계 '+runs.reduce((n,r)=>n+count(r),0)+'개 통과. 실제 Windows UI·저장·재시작을 사용했으며 외부 API와 센서 응답/설치 결과는 목업입니다. 설치된 앱·원본 레이아웃·시작 등록의 전후 비교를 통과했습니다.</p>'+
  '<p>센서 도우미 22개 검사는 패키징에서 별도로 재실행했습니다. 네이티브 핵심 '+coreChecks+'개도 통과했습니다. 날씨 전용·실제 센서 검사의 이전 결과는 2차 보고서에 보존하고 이번 통과 수와 합산하지 않았습니다. 실제 CPU 서비스/PawnIO 설치·제거, CPU 실측·OS 드래그·실제 외부 API는 이번 검사 범위에 포함하지 않습니다.</p>'+
  '<details><summary>최초 실패와 검사 보완</summary><p><code>test-results/e2e-20261017-151026-885</code>에서 디스크 막대 픽셀 검사가 실패했습니다. 막대가 90% 불투명도로 트랙과 합성되는데 검사가 불투명한 글자색을 기대했습니다. 미사용 트랙 픽셀을 기준으로 합성 결과를 계산하도록 검사를 고쳤으며, 앱 색상 코드는 변경하지 않았습니다. 최초 실패를 보존하고 그 실행의 통과 수는 최종 수에 더하지 않았습니다.</p></details><p>사용자 앱을 종료하거나 업데이트하지 않았습니다. 새 배포 파일에 수정한 앱과 보고서를 포함했습니다.</p>'+
  '<details><summary>캡처와 빌드 무결성</summary>'+runs.map(r=>'<p class="fine hash">'+esc(r.mode)+': '+esc(r.executableSha256)+'</p>').join('')+
  '<pre class="fine hash" style="white-space:pre-wrap">'+esc(JSON.stringify(sources,null,2))+'</pre></details></div></section></main>'+
  '<footer>Tempos · 2026-10-17 3차 수정 · 실제 Windows UI 캡처 / 디자인 예제</footer><script>'+js+'</script></body></html>';
fs.writeFileSync(output,html);
fs.writeFileSync(output.replace('.html','.json'),JSON.stringify({generatedAt:new Date().toISOString(),iteration:3,changed,unchanged:same,layouts:37,
  executableSha256:hash(path.join(root,'out/release/Tempos.exe')),native:{log:coreLog,checks:coreChecks},sources,runs,priorAttempts,previousReport:'2026-10-17-layout-report-v2.html'},null,2)+'\n');
console.log(JSON.stringify({output,changed,unchanged:same,images:sources.length,passes:runs.reduce((n,r)=>n+count(r),0)}));
