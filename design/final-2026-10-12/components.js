/* Independent HTML design specification. DIP coordinates are shared across all boards;
   app captures are stored separately and are never used as the design reference. */
const sizes={S:[160,160],Slim:[336,72],M:[336,160],L:[336,336],XL:[1008,840]};
const names={clock:'시계',cpu:'CPU',gpu:'GPU',memory:'메모리',system:'시스템 정보',network:'네트워크',disk:'디스크',calendar:'캘린더'};
const paths={clock:'M1 5H23V19H1Z M5 9H8V15H5 M16 9H19V15H16 M12 10V11 M12 13V14',cpu:'M5 5H19V19H5Z M9 9H15V15H9Z M8 1V5 M12 1V5 M16 1V5 M8 19V23 M12 19V23 M16 19V23 M1 8H5 M1 12H5 M1 16H5 M19 8H23 M19 12H23 M19 16H23',gpu:'M3 5H22V18H3Z M1 3V21 M7 18V21 M17 18V21 M7 9H12V14H7Z M16 9H19 M16 13H19',memory:'M1 6H23V18H1Z M5 9H9V14H5Z M14 9H18V14H14Z M5 18V22 M9 18V22 M15 18V22 M19 18V22',system:'M1 3H23V18H1Z M12 18V22 M7 22H17',network:'M12 2V7 M5 7H19V15 M5 7V15 M1 15H9V21H1Z M15 15H23V21H15Z',disk:'M5 3H19L23 14H1Z M1 14H23V21H1Z M17 17H19',calendar:'M2 4H22V22H2Z M2 10H22 M7 1V7 M17 1V7 M7 15H10 M14 15H17'};
const esc=s=>String(s).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('"','&quot;');
function text(s,x,y,w,h,size=16,bold=false,color='white',align='left'){return `<div class="text${bold?' bold':''}" style="left:${x}px;top:${y+2}px;width:${w}px;height:${h}px;font-size:${size}px;color:${color};text-align:${align}">${esc(s)}</div>`;}
function line(x,y,x2,y2){return `<i class="line" style="left:${x}px;top:${y}px;width:${Math.max(1,x2-x)}px;height:${Math.max(1,y2-y)}px"></i>`;}
function bar(x,y,w,pct){return `<div class="bar" style="left:${x}px;top:${y}px;width:${w}px"><i style="width:${pct}%"></i></div>`;}
function graph(x,y,w,h,kind='cpu',color='white',second=false){let points=[];for(let i=0;i<60;i++){let value=(kind==='network'?(second?10:48):kind==='memory'?39:kind==='gpu'?26:22)+Math.sin(i*.7)*(kind==='memory'?2:kind==='gpu'?8:kind==='network'?19:6);points.push(`${i*w/59},${h-value*h/100}`);}return `<svg class="plot" style="left:${x}px;top:${y}px;width:${w}px;height:${h}px" viewBox="0 0 ${w} ${h}"><path d="M${points.join(' L')}" fill="none" stroke="${color}" stroke-width="2"/><path d="M0 ${h}H${w}" fill="none" stroke="white" stroke-opacity=".2"/></svg>`;}
function heading(kind,size,title){let slim=size==='Slim';return `<svg class="glyph" data-header-icon="${kind}" style="left:${slim?17:20}px;top:${slim?26:20}px;width:20px;height:20px" viewBox="0 0 24 24"><path d="${paths[kind]}" fill="none" stroke="white" stroke-width="2.04"/></svg>`+text(title,slim?47:48,slim?12:17,slim?113:sizes[size][0]-68,25,slim||size==='S'?14:16,true);}
function widget(kind,size){const [w,h]=sizes[size],slim=size==='Slim',small=size==='S',medium=size==='M',large=size==='L',width=w-40;
 if(kind==='weather')return `<div class="card weather" style="width:${w}px;height:${h}px" aria-label="확정 날씨 원본"></div>`;
 let title=names[kind];if(kind==='disk')title=large?'전체 디스크':slim?'C:':'디스크 C:';if(kind==='calendar'&&(large||size==='XL'))title='2026년 10월';
 let s=heading(kind,size,title);
 if(['cpu','gpu','memory'].includes(kind)){
  const value={cpu:22,gpu:26,memory:39}[kind],detail={cpu:'AMD Ryzen 7 9700X',gpu:'NVIDIA GeForce RTX 4070 Ti',memory:'12.4 GiB / 31.7 GiB'}[kind];
  s+=text(value+'%',slim?47:20,slim?31:49,slim?119:medium?146:width,slim?34:59,slim?26:42,true);
  if(!slim)s+=text(detail,20,111,medium?146:width,23,small?11:12);
  if(small)s+=bar(20,139,width,value);
  else if(slim)s+=graph(184,22,132,30,kind);
  else if(medium)s+=line(176,56,176,140)+graph(192,58,124,53,kind)+text('최근 60초',192,123,124,22,12);
  else s+=graph(20,164,width,98,kind)+text('최근 60초',20,272,width,20,12)+text('평균 '+value+'%',20,301,143,24,15)+text('최대 '+(value+8)+'%',173,301,143,24,15);
 }else if(kind==='clock'){
  if(slim)s+=text('22:47',47,31,119,34,26,true)+line(176,19,176,53)+text('10월 12일',193,15,126,24,14)+text('월요일',193,39,126,22,12);
  else if(small)s+=text('22:47',20,53,120,49,37,true)+text('10월 12일',20,109,120,22,13)+text('월요일',20,131,120,20,12);
  else if(medium)s+=text('22:47',20,53,175,61,51,true)+line(197,56,197,140)+text('2026년',214,61,102,22,13)+text('10월 12일',214,88,102,24,16,true)+text('월요일',214,118,102,22,13);
  else s+=text('22:47',20,91,width,100,86,true)+text('2026년 10월 12일',20,208,width,32,22)+line(20,253,316,253)+text('월요일',20,268,width,25,16);
 }else if(kind==='system'){
  if(slim)s+=text('POYAL',47,34,119,26,17,true)+line(176,19,176,53)+text('Windows 11',193,14,126,25,14)+text('1일 9시간',193,40,126,22,12);
  else if(small)s+=text('POYAL',20,55,width,34,24,true)+text('Windows 11',20,94,width,23,12)+line(20,120,140,120)+text('가동 1일 9시간',20,129,width,22,12);
  else {s+=text('POYAL',20,53,width,medium?31:50,medium?24:32,true);const rows=medium?[['CPU','AMD Ryzen 7 9700X'],['GPU','NVIDIA GeForce RTX 4070 Ti']]:[['OS','Windows 11 · 빌드 26300'],['CPU','AMD Ryzen 7 9700X'],['GPU','NVIDIA GeForce RTX 4070 Ti'],['가동 시간','1일 9시간']];if(!medium)s+=line(20,113,316,113);rows.forEach((r,i)=>{let y=(medium?87:127)+i*(medium?23:49),label=medium?39:76;s+=text(r[0],20,y,label,22,12)+text(r[1],20+label,y,width-label,40,medium?12:14);if(!medium&&i<rows.length-1)s+=line(20,y+39,316,y+39);});if(medium)s+=text('Windows 11 · 가동 1일 9시간',20,137,width,19,11);}
 }else if(kind==='disk'){
  if(slim)s+=text('디스크',47,37,85,22,12)+line(126,19,126,53)+text('여유 380.0 GB',143,13,173,28,17,true)+bar(143,48,173,62);
  else if(small)s+=text('62%',20,49,width,58,42,true)+text('사용',20,101,width,23,13)+text('380.0 GB 여유',20,121,width,20,11)+bar(20,144,width,62);
  else if(medium)s+=text('380.0 GB',20,55,165,52,34,true)+text('사용 가능',20,117,165,23,14)+line(187,56,187,140)+text('620.0 GB / 1.0 TB',204,61,112,23,12)+bar(204,96,112,62)+text('62% 사용',204,117,112,23,14);
  else {s+=text('3개 드라이브',20,53,width,24,14);[['C: 시스템','620.0 GB / 1.0 TB','380.0 GB',62],['D: 데이터','720.0 GB / 2.0 TB','1.3 TB',36],['E: 작업','210.0 GB / 500.0 GB','290.0 GB',42]].forEach((r,i)=>{let y=92+i*74;s+=text(r[0],20,y,127,24,16,true)+text(r[1],151,y+2,165,22,12,false,'white','right')+bar(20,y+33,152,r[3])+text(r[2]+' 여유',184,y+28,132,24,12,false,'white','right');if(i<2)s+=line(20,y+61,316,y+61);});}
 }else if(kind==='network'){
  if(slim)s+=text('↓ 8.00 Mbps',47,36,129,24,14,true)+line(184,19,184,53)+text('송신',200,12,116,24,14)+text('↑ 1.00 Mbps',200,36,116,24,14,true,'#c3ffe9');
  else {s+=text('↓ 8.00 Mbps',20,57,medium?146:width,36,small?17:medium?19:27,true)+text('↑ 1.00 Mbps',20,99,medium?146:width,30,small?15:medium?17:22,false,'#c3ffe9');if(!small){let x=medium?192:20,y=medium?58:164,gw=medium?124:width,gh=medium?53:98;if(medium)s+=line(176,56,176,140);s+=graph(x,y,gw,gh,kind)+graph(x,y,gw,gh,kind,'#c3ffe9',true)+text('최근 60초',x,medium?123:272,gw,22,12);}if(large)s+=text('전체 네트워크 · ↓ 수신 / ↑ 송신',20,301,width,23,12);}
 }else if(kind==='calendar'){
  if(slim)s+=text('10/12',47,31,119,34,26,true)+line(176,19,176,53)+text('●  14:00 디자인 검토',193,26,123,24,12);
  else if(small)s+=text('12',20,49,120,59,42,true)+text('2026년 10월',20,108,120,21,12)+text('●  14:00 디자인 검토',20,133,120,22,11);
  else if(medium)s+=text('12',20,49,146,59,42,true)+text('10월',22,118,106,23,14)+line(176,56,176,140)+text('오늘 일정',192,55,124,23,12,true)+text('●  14:00 디자인 검토',192,87,124,24,12)+text('●  17:00 주간 회의',192,119,124,24,12);
  else {const xl=size==='XL',pad=xl?24:20,cw=(w-pad*2)/7,top=xl?110:83,ch=xl?(840-110-24)/5:28;['일','월','화','수','목','금','토'].forEach((d,i)=>s+=text(d,pad+i*cw,top-(xl?46:23),cw,25,xl?16:13,false,'white','center'));for(let i=0;i<35;i++){let day=i-3,x=pad+(i%7)*cw,y=top+Math.floor(i/7)*ch; if(xl){s+=line(x,y,x+cw,y)+line(x,y,x,y+ch);}if(day===12)s+=`<i style="position:absolute;left:${x+cw/2-12}px;top:${y}px;width:24px;height:24px;background:#ffffff30;border-radius:50%"></i>`;s+=text(day<1?day+30:day>31?day-31:day,x,y,cw,25,xl?18:13,false,day<1?'#bfbcdb':'white','center');if(xl&&day===12)s+=text('14:00 디자인 검토',x+8,y+36,cw-16,23,12)+text('17:00 주간 회의',x+8,y+62,cw-16,23,12);}if(!xl)s+=line(20,237,316,237)+text('오늘 일정',20,246,width,23,14,true)+text('●  14:00 디자인 검토',20,273,width,24,14)+text('●  17:00 주간 회의',20,298,width,22,13);}
 }
 return `<div class="card" data-kind="${kind}" data-size="${size}" style="width:${w}px;height:${h}px">${s}</div>`;
}
const board=document.getElementById('board'),kind=new URLSearchParams(location.search).get('board')||'clock';
function slot(k,size,x,y,scale=1,label=true){let e=document.createElement('section');e.className='slot';e.style.cssText=`left:${x}px;top:${y}px`;e.innerHTML=(label?`<div class="label">${size} · ${sizes[size].join(' × ')}</div>`:'')+widget(k,size);e.querySelector('.card').style.transform=`scale(${scale})`;board.append(e);}
const title=kind==='overview'?'전체 위젯 · 공통 구성':kind==='monthly'?'캘린더 · 월간 일정':kind==='settings'?'선택한 위젯의 설정':kind==='rules'?'같은 크기, 같은 기준':names[kind];
board.innerHTML=`<div class="eyebrow">TEMPOS / FINAL DESIGN</div><div class="board-title">${title}</div><div class="subtitle">${kind==='overview'?'같은 배율 · 같은 제목 기준 · 날씨 확정본 유지':'확정 날씨의 디자인 계열을 유지하고, 나머지는 공통 구성 요소로 정리'}</div><div class="foot">디자인 기준 · 샘플 데이터 · 크기는 DIP 기준 · 모든 카드 조회 전용</div>`;
if(kind==='overview'){
 slot('clock','M',50,260,1.1,false);slot('weather','M',50,456,1.1,false);slot('network','M',50,652,1.1,false);
 slot('cpu','S',443,260,1.1,false);slot('gpu','S',638,260,1.1,false);slot('memory','M',443,456,1.1,false);slot('system','M',443,652,1.1,false);
 slot('disk','L',836,260,1.1,false);slot('calendar','L',1229,260,1.1,false);slot('system','Slim',836,652,1.1,false);slot('clock','Slim',1229,652,1.1,false);
}else if(kind==='monthly'){slot('calendar','XL',424,255,.70,false);}
else if(kind==='settings'){
 const panel=(title,x,w,html)=>`<section class="rule-card" style="left:${x}px;top:265px;width:${w}px;height:554px"><h2>${title}</h2>${html}</section>`;
 const field=(label,value)=>`<div class="field-label">${label}</div><div class="field">${value}</div>`;
 board.innerHTML+=panel('공통 설정',50,410,field('크기 / 확대','M (336 × 160) / 100%')+field('테마 / 모니터','전체 테마 따르기 / 모니터 선택')+field('투명도','배경 10% / 전체 0%')+'<p>□ 클릭 통과　□ 위치 잠금<br>□ 항상 위</p><div class="hint">새로고침은 위젯 종류에 맞는 주기만 표시</div>')+panel('시계 선택',484,362,field('초 표시','□ 초 표시')+field('시간대','Windows 설정 따르기')+field('표시 옵션','□ 12시간 표시')+'<div class="hint">시간 관련 항목만 표시</div>')+panel('날씨 선택',870,362,field('날씨 배경','☑ 날씨 배경 자동')+field('날씨 지역','서울특별시 종로구')+field('지역 선택','현재 위치로 찾기')+'<div class="hint">한 번 찾은 지역을 적용하여 저장</div>'+field('표시 옵션','□ 화씨 표시'))+panel('장치 / 데이터',1256,366,field('네트워크','전체 네트워크 / 개별 장치')+field('디스크 S · Slim · M','볼륨 선택')+field('디스크 L','전체 로컬 볼륨 · 선택 필드 숨김')+field('시스템 정보','PC 이름 / OS / CPU / GPU / 가동 시간'));
}else if(kind==='rules'){
 ['cpu','gpu','memory','system','network','disk','calendar','clock'].forEach((k,i)=>slot(k,'S',50+i*200,285,1,false));
 board.innerHTML+=`<section class="rule-card" style="left:50px;top:500px;width:1560px;height:306px"><h2>공통 배치 규칙</h2><p>S · M · L : 아이콘 (20, 20), 20 DIP / 제목 (48, 17) / 좌우 여백 20 DIP<br>S 제목 14 DIP · M/L 제목 16 DIP / 기본 수치 42 DIP / 모서리 반경 24 DIP<br>Slim : 아이콘 (17, 26), 제목 (47, 12), 본문 (47, 31~37) / 반경 24 DIP<br>시계·디스크·캘린더는 정보 특성에 따른 본문 구성. 제목 줄은 동일. 날씨는 확정 예외.</p></section>`;
}else {slot(kind,'S',65,420,1.75);slot(kind,'Slim',398,294,1.75);slot(kind,'M',398,554,1.75);slot(kind,'L',1013,266,1.75);}
