import fs from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const pkg = 'C:/Program Files/WindowsApps/OpenAI.CodexPrimaryRuntime.v26-1112-737-0_26.1112.737.0_x64__3k8sg7r9htsxt/dependencies/node/node_modules/@oai/artifact-tool/dist/artifact_tool.mjs';
const { Presentation, PresentationFile } = await import(pathToFileURL(pkg).href);
const root = 'C:/Users/user/Desktop/FoodSpoilageMonitor/deliverables/presentation_20261118';
const build = path.join(root, 'build');
const media = path.join(build, 'media');
const output = path.join(root, 'output');
await fs.mkdir(path.join(build, 'render'), { recursive: true });
await fs.mkdir(output, { recursive: true });

const C = { navy:'#142440', ink:'#15233B', blue:'#245CC2', teal:'#197D79', orange:'#C15C2E',
  muted:'#546377', pale:'#F7F8F5', line:'#C8D4DF', white:'#FFFFFF', green:'#16805A' };
const font='Malgun Gothic';
function shape(slide,geometry,x,y,w,h,fill='none',stroke='none',strokeWidth=0){
  return slide.shapes.add({geometry,position:{left:x,top:y,width:w,height:h},fill,
    line:{style:'solid',fill:stroke,width:strokeWidth}});
}
function txt(slide,text,x,y,w,h,size=24,color=C.ink,bold=false){
  const s=shape(slide,'textbox',x,y,w,h);
  s.text=text;s.text.style={fontFamily:font,fontSize:size,color,bold};return s;
}
function line(slide,x,y,w,color=C.line,h=2){shape(slide,'rect',x,y,w,h,color);}
async function img(slide,file,x,y,w,h,fit='contain',alt=''){
  const bytes=await fs.readFile(file);const ext=path.extname(file).toLowerCase();
  slide.images.add({blob:bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength),
    contentType:ext==='.jpg'||ext==='.jpeg'?'image/jpeg':'image/png',alt,
    fit,position:{left:x,top:y,width:w,height:h}});
}
function top(slide,n,title){
  slide.background.fill=C.pale;
  txt(slide,title,62,34,1150,62,34,C.navy,true);
  line(slide,62,112,1156,C.blue,3);
  txt(slide,'FOOD SPOILAGE MONITOR  ·  2026.11.18',62,672,930,20,13,C.muted);
  txt(slide,String(n).padStart(2,'0'),1172,670,46,24,15,C.muted,true);
}
function header(slide,title,notes=''){
  const n=slide._number||0;top(slide,n,title);slide.speakerNotes.text=notes;
}

const deck=Presentation.create({slideSize:{width:1280,height:720}});
{
  const s=deck.slides.add();s._number=1;s.background.fill=C.navy;
  txt(s,'스마트 음식 보관 모니터',80,145,960,100,54,C.white,true);
  txt(s,'용기별 보관 시간·환경·가스 변화를 기록하고 상태를 표시',80,260,1000,72,27,'#E6EDF7');
  line(s,80,365,915,'#5C8BE1',3);
  txt(s,'BME688  ·  nRF52840  ·  ESP32-S3  ·  Apps Script',80,405,1050,44,23,'#C8D9F5');
  txt(s,'가천대학교 캡스톤디자인  ·  2026년 11월 18일',80,620,960,28,18,'#B9C9E2');
  s.speakerNotes.text='원본 26장 발표 자료의 제목을 현재 구현 범위에 맞게 수정. 식품 섭취 안전을 판정하는 장치라고 주장하지 않는다.';
}
{
  const s=deck.slides.add();s._number=2;header(s,'사용자가 하는 일은 두 가지',
    '자료: 원본 발표자료 슬라이드 2,6; 현재 Dashboard.html/StorageSessions.gs.');
  txt(s,'1',72,175,100,95,76,C.blue,true);txt(s,'음식과 실제 투입 시각 입력',178,192,930,60,34,C.ink,true);
  txt(s,'장치별 보관 세션을 만들고, 재부팅 후에도 같은 시작 시각을 유지합니다.',178,255,980,58,23,C.muted);
  line(s,70,336,1120);
  txt(s,'2',72,370,100,95,76,C.blue,true);txt(s,'스마트폰에서 상태 확인',178,388,930,60,34,C.ink,true);
  txt(s,'보관 경과, 최근 온습도, GAS 변화, 통신 상태를 한 화면에서 봅니다.',178,451,970,58,23,C.muted);
  txt(s,'표시 단계: 신선 단계 추정 → 소비 우선 권장 → 변질 징후 추정',178,552,970,44,23,C.teal,true);
}
{
  const s=deck.slides.add();s._number=3;header(s,'장치 구성과 데이터 경로',
    '자료: 원본 발표자료 슬라이드 3,4의 실험용 용기 사진; docs/ENVIRONMENT_AND_GAS.md; docs/RELIABILITY.md.');
  txt(s,'보관용기 내부',74,154,570,43,28,C.blue,true);
  txt(s,'BME688 → nRF52840',74,218,620,55,31,C.navy,true);
  txt(s,'온도·습도·기압과 10단계 GAS 저항을 측정',74,278,620,48,23,C.ink);
  txt(s,'BLE → ESP32-S3 → Wi-Fi → Apps Script',74,382,630,57,31,C.navy,true);
  txt(s,'ESP32는 음식 후보와 GAS 변화를 계산하고,\n전송 실패 데이터는 SPIFFS에 보관한 뒤 재전송합니다.',74,446,630,105,23,C.ink);
  await img(s,path.join(media,'image-4-1.jpg'),765,135,395,490,'contain','실험용 보관용기 내부 센서 노드');
  txt(s,'실험용 용기 내부 배치',805,630,340,28,17,C.muted);
}
{
  const s=deck.slides.add();s._number=4;header(s,'측정·저장 방식',
    '자료: docs/ENVIRONMENT_AND_GAS.md; docs/GAS_SCAN_V3.md; docs/RELIABILITY.md. 측정 간격은 펌웨어 목표 주기이며 실기 지연이 있을 수 있다.');
  txt(s,'약 2분',72,158,310,65,45,C.blue,true);txt(s,'히터 OFF 환경 측정',72,231,320,44,23,C.ink);
  txt(s,'약 10분',472,158,310,65,45,C.blue,true);txt(s,'10단계 GAS 스캔',472,231,330,44,23,C.ink);
  txt(s,'0…9',872,158,310,65,45,C.blue,true);txt(s,'완성 지문만 AI 입력',872,231,340,44,23,C.ink);
  line(s,70,332,1140);
  txt(s,'측정이 빠진 단계는 채워 넣지 않습니다.',74,373,1090,58,29,C.navy,true);
  txt(s,'BLE 수신 후 고유 UID를 붙여 전송하고, 클라우드 ACK 전에는 큐·스풀에서 제거하지 않습니다.',74,446,1090,92,24,C.ink);
  txt(s,'원본 측정값과 환경 이력은 별도로 저장합니다.',74,571,1090,42,22,C.muted);
}
{
  const s=deck.slides.add();s._number=5;header(s,'대시보드의 세 가지 결과',
    '자료: 2026-11-17 사용자 제공 운영 화면; cloud/apps_script/Dashboard.html; docs/ASSUMED_PROGRESS_DEMO.md. 보관 단계는 사용자 승인 초반·후반 가정과 경과 시간 기반. GAS 변화는 별도 실험 지표.');
  txt(s,'보관 진행',80,155,330,45,26,C.blue,true);
  txt(s,'신선 추정  →  소비 우선  →  변질 징후 추정',80,207,1110,65,34,C.navy,true);
  line(s,80,300,1100);
  txt(s,'ESP32 음식 후보',80,342,430,43,26,C.blue,true);
  txt(s,'10단계 GAS 지문을 여섯 음식의 기준 패턴과 비교',80,392,1080,44,23,C.ink);
  txt(s,'GAS 변화',80,476,430,43,26,C.blue,true);
  txt(s,'같은 용기의 초기 지문과 얼마나 달라졌는지 별도 표시',80,526,1070,55,23,C.ink);
  txt(s,'세 결과는 서로 다른 근거이며, 섭취 안전을 보증하지 않습니다.',80,611,1100,38,20,C.orange,true);
}
{
  const s=deck.slides.add();s._number=6;header(s,'온도에 따라 GAS 반응이 크게 달라졌다',
    '자료: 원본 발표자료 슬라이드 14 및 해당 실험 그래프. 동일 닭가슴살 시료의 냉장·상온 이동 관측. 저항 하한은 부패 확정 지표가 아니며 시료 상태 라벨이 없다.');
  await img(s,path.join(media,'image-14-1.png'),52,142,790,492,'contain','닭가슴살 냉장과 상온 가스 저항 시계열');
  txt(s,'같은 시료',865,176,335,42,26,C.blue,true);
  txt(s,'냉장 약 77시간에는\n전 단계 측정 하한이 없었습니다.',865,236,330,105,23,C.ink);
  txt(s,'상온 이동 뒤 약 6.9시간에\n전 단계가 5.68 kΩ 하한에 닿았습니다.',865,385,330,118,23,C.ink);
  txt(s,'한 실험의 센서 반응입니다.\n부패 시작 시각은 확인되지 않았습니다.',865,550,330,70,19,C.orange);
}
{
  const s=deck.slides.add();s._number=7;header(s,'학습 입력과 품질 검사',
    '자료: PROJECT_STATE.md 2026-11-16; data/gas-ai-20261116/audit.json; docs/GAS_AI_EDGE.md. SensorData 원본 37,578행, 완성 지문 3,385개, 품질 및 음식명 통과 2,973개.');
  const xs=[72,365,658,951],nums=['37,578','3,385','2,973','2,043'],labs=['원본 센서 행','완성 10단계 지문','품질·음식명 통과','독립 평가 가능 지문'];
  xs.forEach((x,i)=>{txt(s,nums[i],x,190,250,72,44,C.blue,true);txt(s,labs[i],x,278,250,70,20,C.ink);});
  line(s,72,384,1120);
  txt(s,'누락 단계, 중복, 비정상 값과 5.68 kΩ 고정 구간은 AI 입력에서 구분했습니다.',75,433,1100,80,26,C.navy,true);
  txt(s,'원본 행은 삭제하지 않고, 평가할 때 같은 음식 노출 실험을 학습·평가 양쪽에 섞지 않았습니다.',75,556,1110,57,20,C.muted);
  txt(s,'집계 기준: 2026.11.16 로컬 데이터 사본',75,621,1080,29,17,C.muted);
}
{
  const s=deck.slides.add();s._number=8;header(s,'AI 성능과 해석 범위',
    '자료: docs/GAS_AI_EDGE.md; data/gas-ai-20261116/evaluation.json; docs/ASSUMED_PROGRESS_DEMO.md. 54.04%는 평가 가능 음식 지문 2,043개 중 1,104개를 맞힌 음식 종류 분류 정확도. 삶은 달걀·숙주는 독립 노출 1개라 평가 불가.');
  txt(s,'54.04%',72,162,455,104,70,C.blue,true);
  txt(s,'음식 종류 분류\n1,104 / 2,043 지문',72,278,470,125,28,C.navy,true);
  line(s,605,164,2,C.line,390);
  txt(s,'이 숫자가 뜻하지 않는 것',650,176,570,50,29,C.orange,true);
  txt(s,'• 실제 부패/정상 판정 정확도\n• 처음 보는 음식의 자동 식별 정확도\n• 개별 예측의 신뢰 확률',650,249,540,155,24,C.ink);
  txt(s,'삶은 달걀·숙주는 독립 노출이 각 1회여서 음식별 정확도를 검증하지 못했습니다.',650,463,540,98,22,C.muted);
  txt(s,'11/16 데이터 사본 기준 · 보관 단계는 실험용 시간 모델, GAS 변화 점수는 별도입니다.',72,595,1110,50,20,C.teal,true);
}
{
  const s=deck.slides.add();s._number=9;header(s,'실기 확인과 데이터 보존',
    '자료: PROJECT_STATE.md 2026-11-09 nRF DFU 사용자 실기 성공, 2026-11-17 ESP2.0.29 사용자 보고; docs/REMOTE_UPDATES.md; docs/RELIABILITY.md. 장시간 전체 장치 검증은 미완료.');
  txt(s,'확인한 동작',72,160,530,45,29,C.blue,true);
  txt(s,'• ESP32-S3 → Wi-Fi → Apps Script 기록\n• ESP32 원격 OTA와 nRF52840 BLE DFU\n• 센서 재연결과 새 측정 데이터 복귀',72,224,530,220,24,C.ink);
  txt(s,'데이터 보호',665,160,520,45,29,C.blue,true);
  txt(s,'• ACK 후에만 전송 대기 데이터 제거\n• 실패 시 SPIFFS 스풀로 재전송\n• 누락 측정값을 임의로 채우지 않음',665,224,530,230,24,C.ink);
  line(s,72,527,1120);
  txt(s,'실험 중 원격 업데이트는 기록 종료·미전송 데이터 배출 후 진행',74,568,1100,50,22,C.orange,true);
}
{
  const s=deck.slides.add();s._number=10;s.background.fill=C.navy;
  txt(s,'현재 구현과 남은 검증',77,76,1110,74,44,C.white,true);
  line(s,77,174,1100,'#6990D4',3);
  txt(s,'구현',80,225,230,48,29,'#91B6FF',true);
  txt(s,'용기별 기록 · 환경/GAS 측정 · 모바일 화면 · 원격 OTA/DFU',80,285,1080,70,27,C.white);
  txt(s,'한계',80,405,230,48,29,'#F3A979',true);
  txt(s,'음식 분류 정확도 54.04%는 제한된 평가 지문 기준\nGAS 변화만으로 실제 부패를 확정하지 않음',80,465,1080,120,26,C.white);
  txt(s,'시연: 음식·투입 시각 설정 → 실시간 기록 → 보관 상태 확인',80,624,1090,42,23,'#D5E5FF',true);
  s.speakerNotes.text='발표 시 운영 대시보드를 열어 실제 장치 연결 상태와 측정 시각을 확인한다. 음식 분류와 보관 단계·GAS 변화를 하나의 검증된 부패 확률로 표현하지 않는다.';
}

const poster=Presentation.create({slideSize:{width:2245.04,height:3178.58}});
{
  const s=poster.slides.add();s.background.fill=C.pale;
  shape(s,'rect',0,0,2245,405,C.navy);
  txt(s,'스마트 음식 보관 모니터',95,74,2010,112,87,C.white,true);
  txt(s,'용기별 보관 시간·온습도·10단계 GAS 지문 기록',97,213,1970,76,49,'#D8E8FF');
  txt(s,'BME688  ·  nRF52840  ·  ESP32-S3  ·  Apps Script',97,320,1900,46,31,'#AFC8F2');
  line(s,100,515,2045,C.blue,5);
  const metrics=[['2분','환경 측정 목표'],['10분','GAS 스캔 목표'],['3,385','완성 GAS 지문'],['54.04%','음식 분류*']];
  metrics.forEach(([big,label],i)=>{const x=104+i*520;txt(s,big,x,550,450,85,61,C.blue,true);txt(s,label,x,647,450,52,33,C.ink);});
  txt(s,'*2026.11.16 데이터 사본 기준 · 평가 가능 지문 2,043개 중 1,104개 정답. 실제 부패 판정 정확도가 아닙니다.',104,720,2020,52,27,C.muted);
  line(s,100,792,2045,C.line,3);

  txt(s,'시스템 구성',100,835,940,70,53,C.navy,true);
  await img(s,path.join(media,'image-4-1.jpg'),105,920,440,735,'contain','실험용 보관용기 내부 배치');
  txt(s,'실험용 용기 내부 센서 노드',105,1660,790,42,28,C.muted);
  txt(s,'BME688 + nRF52840\n↓ BLE\nESP32-S3\n↓ Wi-Fi\nApps Script / 스마트폰',583,963,445,290,36,C.ink,true);
  txt(s,'환경 측정과 GAS 스캔을\n분리합니다. 빠진 단계는\n만들어 넣지 않습니다.\nACK 전 자료는 스풀에 보존합니다.',583,1280,445,300,29,C.ink);
  line(s,1085,842,3,C.line,2050);

  txt(s,'대표 실험: 상온 두부',100,1768,930,70,53,C.navy,true);
  await img(s,path.join(media,'image-13-1.png'),92,1870,955,720,'contain','상온 두부 10단계 GAS 저항 그래프');
  txt(s,'step 1은 10.6시간에 정점에 도달한 뒤 17.9시간에 측정 하한으로 내려갔습니다. 센서 반응이며 부패 시작 시각은 확인되지 않았습니다.',111,2614,927,156,32,C.ink);

  txt(s,'화면에서 보는 결과',1160,835,960,70,53,C.navy,true);
  txt(s,'① 보관 단계',1162,940,920,48,38,C.blue,true);
  txt(s,'신선 단계 추정 → 소비 우선 권장 → 변질 징후 추정',1162,995,920,126,38,C.ink);
  txt(s,'② ESP32 음식 후보',1162,1182,920,48,38,C.blue,true);
  txt(s,'완성 GAS 지문을 여섯 음식의 패턴과 비교합니다.',1162,1240,920,105,35,C.ink);
  txt(s,'③ GAS 변화',1162,1402,920,48,38,C.blue,true);
  txt(s,'같은 용기의 초기 패턴과의 차이를 별도로 표시합니다.',1162,1460,920,110,35,C.ink);
  line(s,1160,1635,942,C.line,3);
  txt(s,'데이터와 평가',1160,1690,940,70,53,C.navy,true);
  txt(s,'37,578행 → 3,385개 완성 스캔\n→ 2,973개 품질·음식명 통과',1162,1790,930,145,41,C.blue,true);
  txt(s,'음식 종류 분류: 54.04%\n1,104 / 2,043 평가 가능 지문',1162,2000,930,145,41,C.ink,true);
  txt(s,'삶은 달걀·숙주는 독립 노출이 각 1회여서 음식별 정확도를 검증하지 못했습니다.',1162,2220,930,172,34,C.ink);
  line(s,1160,2445,942,C.line,3);
  txt(s,'검증 범위와 한계',1160,2490,940,67,49,C.navy,true);
  txt(s,'실기: 클라우드 기록, ESP32 OTA, nRF DFU\n한계: GAS 변화는 부패 확률이 아님\n보관 단계는 초반·후반 가정에 따른 추정',1162,2580,938,260,34,C.ink);

  line(s,100,2945,2045,C.blue,4);
  txt(s,'가천대학교 캡스톤디자인  ·  2026.11.18',104,2990,1510,55,34,C.navy,true);
  txt(s,'출처: 프로젝트 측정 기록·원본 발표자료 / 식품안전 판정이 아닌 실험적 보관 상태 표시',104,3062,2030,49,26,C.muted);
  s.speakerNotes.text='자료: 공급된 원본 발표자료 슬라이드 4,13; data/gas-ai-20261116/audit.json 및 evaluation.json; docs/GAS_AI_EDGE.md; docs/ENVIRONMENT_AND_GAS.md; PROJECT_STATE.md. A1 세로 594×841mm. 사진은 실험용 시제품, 그래프는 한 시료 관측.';
}

for(let i=0;i<deck.slides.items.length;i++){
  const png=await deck.export({slide:deck.slides.items[i],format:'png',scale:0.8});
  await fs.writeFile(path.join(build,'render',`deck-${String(i+1).padStart(2,'0')}.png`),new Uint8Array(await png.arrayBuffer()));
}
const posterPng=await poster.export({slide:poster.slides.items[0],format:'png',scale:0.5});
await fs.writeFile(path.join(build,'render','poster.png'),new Uint8Array(await posterPng.arrayBuffer()));
await (await PresentationFile.exportPptx(deck)).save(path.join(build,'deck-draft.pptx'));
await (await PresentationFile.exportPptx(poster)).save(path.join(build,'poster-draft.pptx'));
console.log('created',deck.slides.items.length,'slides and A1 poster');
