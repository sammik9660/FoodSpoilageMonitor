const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
let tests=0;
function environment(){
 let time=Date.parse('2026-09-28T12:00:00Z'),uuid=0,fail=null,locked=false,forceLockBusy=false,lockAdvance=0;
 let waitLockCalls=0,tryLockCalls=0;
 const cache=new Map();
 class Sheet {
  constructor(name){this.name=name;this.rows=[];this.maxRows=1000;}
  getLastRow(){let n=this.rows.length;while(n&&!this.rows[n-1].some(x=>x!==''))n--;return n;}
  getLastColumn(){return Math.max(0,...this.rows.map(r=>r.length));}
  getMaxRows(){return this.maxRows;}getMaxColumns(){return 26;}
  insertRowsAfter(_,n){this.maxRows+=n;}setFrozenRows(){}
  getDataRange(){return this.getRange(1,1,this.getLastRow(),this.getLastColumn());}
  getRange(r,c,n=1,m=1){const s=this;return {
   getValues(){return Array.from({length:n},(_,i)=>Array.from({length:m},(_,j)=>s.rows[r+i-1]?.[c+j-1]??''));},
   setValues(values){
    if(fail&&fail(s.name,r,c,values)){fail=null;throw Error('injected write interruption');}
    values.forEach((row,i)=>row.forEach((v,j)=>{s.rows[r+i-1]??=[];s.rows[r+i-1][c+j-1]=v;}));return this;
   },setValue(v){return this.setValues([[v]]);}
  };}
 }
 const all=new Map(), sensor=new Sheet('SensorData');all.set(sensor.name,sensor);
 sensor.rows=[['timestamp','temperature','humidity','pressure','gas_resistance'],['historical',20,40,1000,70]];
 const book={getSheetByName:n=>all.get(n),insertSheet:n=>{assert(!all.has(n));let s=new Sheet(n);all.set(n,s);return s;},getSheets:()=>[...all.values()],getId:()=> 'existing-id'};
 class Clock extends Date{constructor(...args){super(...(args.length?args:[time]));}static now(){return time;}}
 const props=new Map([['SPREADSHEET_ID','existing-id']]);
 const api={Date:Clock,console,JSON,Number,String,Math,Array,Object,Error,
  LockService:{getScriptLock:()=>({waitLock(){waitLockCalls++;if(forceLockBusy||locked)throw Error('잠금 시간초과');locked=true;time+=lockAdvance;},
    tryLock(){tryLockCalls++;if(forceLockBusy||locked)return false;locked=true;time+=lockAdvance;return true;},releaseLock(){locked=false;}})},
  PropertiesService:{getScriptProperties:()=>({getProperty:k=>props.get(k)})},
  SpreadsheetApp:{openById:id=>{assert.equal(id,'existing-id');return book;},flush(){}},
  Utilities:{getUuid:()=>String(++uuid).padStart(32,'0')},
  CacheService:{getScriptCache:()=>({get:k=>cache.get(k),put:(k,v)=>cache.set(k,v),remove:k=>cache.delete(k)})},
  ContentService:{MimeType:{JSON:'json',CSV:'csv'},createTextOutput:text=>({text,filename:null,setMimeType(){return this;},downloadAsFile(name){if(/[^\x20-\x7E]/.test(name))throw Error('잘못된 인수: '+name);this.filename=name;return this;}})},
  ScriptApp:{getService:()=>({getUrl:()=> 'https://example.invalid/exec'})},
  HtmlService:{createTemplateFromFile:name=>({evaluate(){return {setTitle(){return {name,url:this.webAppUrl};}};}})}
 };
 vm.createContext(api);vm.runInContext(fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Code.gs'),'utf8'),api);
 api.setupReliability();
 return {api,all,sensor,props,cache,setTime:t=>time=t,now:()=>time,setFail:f=>fail=f,
  setLockBusy:v=>forceLockBusy=v,setLockAdvance:v=>lockAdvance=v,
  lockCounts:()=>({wait:waitLockCalls,try:tryLockCalls})};
}
const boot='a'.repeat(32);
function sample(seq,time,extra={}){return {uid:boot+'-'+seq,seq,ms:seq*2000,nrf_boot:42,captured_at:time,quality:'ntp',t:25,h:50,p:1000,g:70,...extra};}
function send(e,ss){return e.api.ingest({version:2,samples:ss});}
function v3(seq,step,time){return {...sample(seq,time,{ms:seq*140}),sample_schema:3,
 scan_seq:1,heater_step:step,gas_index:step,meas_index:step,sensor_status:0xb0,
 gas_valid:true,heater_stable:true,measurement_valid:true,profile_id:354};}
function test(name,f){f();tests++;console.log('PASS '+name);}
test('migration preserves historical rows and appends device ID after existing schema',()=>{const e=environment();assert.deepEqual(e.sensor.rows[1],['historical',20,40,1000,70]);assert.equal(e.sensor.rows[0].length,23);assert.equal(e.sensor.rows[0][22],'device_id');});
test('device lookup extends legacy sheet headers without changing historical rows',()=>{
 const e=environment(),history=e.all.get('EnvironmentHistory');
 e.sensor.rows[0]=e.sensor.rows[0].slice(0,12);
 e.all.get('Sessions').rows[0]=e.all.get('Sessions').rows[0].slice(0,4);
 e.all.get('Unassigned').rows[0]=e.all.get('Unassigned').rows[0].slice(0,22);
 e.all.get('DeviceRegistry').rows[0]=e.all.get('DeviceRegistry').rows[0].slice(0,5);
 history.rows=[history.rows[0].slice(0,22),['historical',20,40,1000,'']];
 const before=history.rows[1].slice();
 const devices=e.api.getDashboardDevices();
 assert.deepEqual(Array.from(devices,x=>x.device_id),['legacy']);
 assert.equal(history.rows[0][22],'device_id');assert.deepEqual(history.rows[1],before);
 assert.equal(e.sensor.rows[0][22],'device_id');assert.deepEqual(e.sensor.rows[1],['historical',20,40,1000,70]);
 assert.equal(e.all.get('Sessions').rows[0][4],'device_id');
 assert.equal(e.all.get('Unassigned').rows[0][22],'device_id');
 assert.equal(e.all.get('DeviceRegistry').rows[0][5],'ota_state');
});
test('migration refuses occupied columns without recognized headers',()=>{
 const e=environment();e.sensor.rows[0]=e.sensor.rows[0].slice(0,12);e.sensor.rows[1][12]='unknown data';
 assert.throws(()=>e.api.setupReliability(),/occupied column/);assert.equal(e.sensor.rows[1][12],'unknown data');
});
test('v2 pending journal replays into wider schema without resetting rows',()=>{
 const e=environment(),row=[new Date(e.now()).toISOString(),25,50,1000,70,boot+'-1','EXP-OLD',1,140,'ntp',new Date(e.now()).toISOString(),42];
 e.sensor.rows[0]=e.sensor.rows[0].slice(0,12);
 e.all.get('IngestState').rows[1]=[boot,0,JSON.stringify({high:1,groups:[{sheet:'SensorData',start:3,rows:[row]}]})];
 e.api.setupReliability();assert.equal(e.sensor.rows[0].length,23);
 assert.deepEqual(e.sensor.rows[2].slice(0,12),row);assert(e.sensor.rows[2].slice(12).every(x=>x===''));
 assert.equal(e.all.get('IngestState').rows[1][1],1);
});
test('v3 ten-step records preserve fingerprint columns and exact ACK',()=>{
 const e=environment(),t=e.now();e.api.startRecording('fridge apple');
 const samples=Array.from({length:10},(_,i)=>v3(i+1,i,t));
 const payload={version:3,samples};
 const fullBatch={version:3,samples:Array.from({length:32},(_,i)=>({...v3(i+1,i%10,t),rx_ms:4294967295})),
  health:{ble_connected:true,sensor_ok:true,queue_depth:128,spool_depth:4096,
   latest:{t:-40,h:100,p:1100,g:1000000000},gateway_uptime_ms:4294967295}};
 assert(JSON.stringify(fullBatch).length<64000);
 const r=e.api.ingest(payload);assert.equal(r.version,3);assert.equal(r.acks.length,10);
 assert.equal(e.sensor.getLastRow(),12);
 for(let i=0;i<10;i++){const row=e.sensor.rows[i+2];assert.equal(row[12],3);assert.equal(row[13],1);
  assert.equal(row[14],i);assert.equal(row[15],i);assert.equal(row[17],0xb0);assert.equal(row[21],354);}
 assert.equal(e.api.ingest(payload).acks[0].status,'duplicate');assert.equal(e.sensor.getLastRow(),12);
 const csv=e.api.getCsvData();assert(csv.includes('scan_seq'));assert(csv.includes('heater_step'));
 assert.throws(()=>e.api.ingest({version:3,samples:[v3(12,0,t),v3(11,1,t)]}),/Batch must/);
});
test('shared BLE vector maps through v3 ingest and CSV metadata',()=>{
 const line=fs.readFileSync(path.join(__dirname,'v3_vector.txt'),'utf8').trim();
 const fields=Object.fromEntries(line.split(',').map(part=>part.split('=')));
 const e=environment();e.api.startRecording('vector');
 const s={...sample(Number(fields.SEQ),e.now(),{ms:Number(fields.MS),nrf_boot:parseInt(fields.BID,16),
  t:Number(fields.T),h:Number(fields.H),p:Number(fields.P),g:Number(fields.G)}),sample_schema:Number(fields.V),
  scan_seq:Number(fields.SCAN),heater_step:Number(fields.STEP),gas_index:Number(fields.GI),
  meas_index:Number(fields.MI),sensor_status:Number(fields.ST),profile_id:Number(fields.PID),
  gas_valid:true,heater_stable:true,measurement_valid:true};
 assert.equal(e.api.ingest({version:3,samples:[s]}).acks[0].status,'stored');
 const row=e.sensor.rows[2];assert.equal(row[8],123456);assert.equal(row[13],2);
 assert.equal(row[14],3);assert.equal(row[16],44);assert(e.api.getCsvData().includes(',"354"'));
});
test('v3 rejects false status, invalid index/profile and gas=0',()=>{
 const e=environment(),t=e.now();for(const change of [{sensor_status:0x80},{gas_valid:false},
  {heater_step:10},{gas_index:1},{profile_id:999},{g:0},{measurement_valid:false}])
  assert.throws(()=>e.api.ingest({version:3,samples:[{...v3(1,0,t),...change}]}));
 assert.equal(e.sensor.getLastRow(),2);
});
test('v3 upload can replay a v2 spool record with original UID',()=>{
 const e=environment();e.api.startRecording('legacy');const old={...sample(1,e.now()),sample_schema:2};
 const r=e.api.ingest({version:3,samples:[old]});assert.equal(r.version,3);assert.equal(r.acks[0].uid,old.uid);
 assert.equal(e.sensor.rows[2][12],2);assert.equal(e.sensor.rows[2][13],'');
});
test('STOPPED samples ACKed but not saved to SensorData',()=>{const e=environment();let r=send(e,[sample(1,e.now())]);assert.equal(r.acks[0].status,'not_recording');assert.equal(e.sensor.getLastRow(),2);});
test('duplicate retry retains one row; ACK retry after restart',()=>{const e=environment();e.api.startRecording('food A');const s=sample(1,e.now());send(e,[s]);let r=send(e,[s]);assert.equal(e.sensor.getLastRow(),3);assert.equal(r.acks[0].status,'duplicate');assert.equal(e.sensor.rows[2][5],s.uid);});
test('capture-time Start/Stop boundaries and delayed prior session',()=>{
 const e=environment(),t=e.now();e.api.startRecording('A');e.setTime(t+10000);e.api.stopRecording();e.setTime(t+20000);e.api.startRecording('B');
 const r=send(e,[sample(1,t-1),sample(2,t),sample(3,t+9999),sample(4,t+10000),sample(5,t+20000)]);
 assert.deepEqual(Array.from(r.acks,a=>a.status),['not_recording','stored','stored','not_recording','stored']);
 assert.equal(e.sensor.rows[2][6],e.sensor.rows[3][6]);assert.notEqual(e.sensor.rows[3][6],e.sensor.rows[4][6]);
});
test('unknown time quarantined, never assigned by arrival',()=>{const e=environment();e.api.startRecording('A');const r=send(e,[sample(1,null,{quality:'unknown'})]);assert.equal(r.acks[0].status,'unassigned_time');assert.equal(e.sensor.getLastRow(),2);assert.equal(e.all.get('Unassigned').getLastRow(),2);});
test('strict types, ranges, UID, order, trailing JSON and batch bound',()=>{
 const e=environment();for(const change of [{t:null},{h:''},{p:true},{g:NaN},{uid:'bad'},{captured_at:e.now()+6000},{seq:0}])assert.throws(()=>send(e,[sample(1,e.now(),change)]));
 assert.throws(()=>send(e,[sample(2,e.now()),sample(1,e.now())]));assert.throws(()=>send(e,Array.from({length:33},(_,i)=>sample(i+1,e.now()))));
 assert.equal(JSON.parse(e.api.doPost({postData:{contents:'{}garbage'}}).text).success,false);
 assert.equal(e.sensor.getLastRow(),2);
});
test('legacy short and actual baseline long keys accepted',()=>{const e=environment();e.api.startRecording('manual');e.api.ingest({T:25,H:50,P:1000,G:80});e.api.ingest({temperature:25,humidity:50,pressure:1000,gas:80});assert.equal(e.sensor.getLastRow(),4);assert.equal(e.sensor.rows[2][9],'legacy_server');});
test('write fails before rows: no ACK; retry finishes journal once',()=>{
 const e=environment();e.api.startRecording('A');e.setFail(name=>name==='SensorData');const s=sample(1,e.now());assert.throws(()=>send(e,[s]));
 assert.equal(e.sensor.getLastRow(),2);send(e,[s]);assert.equal(e.sensor.getLastRow(),3);assert.equal(e.all.get('IngestState').rows[1][2],'');
});
test('write succeeds but state commit fails: dedup recovery',()=>{
 const e=environment();e.api.startRecording('A');e.setFail((name,r,c,v)=>name==='IngestState'&&r>1&&v[0][2]==='');const s=sample(1,e.now());
 assert.throws(()=>send(e,[s]));assert.equal(e.sensor.getLastRow(),3);const ack=send(e,[s]);assert.equal(ack.acks[0].status,'duplicate');assert.equal(e.sensor.getLastRow(),3);
});
test('journal refuses to overwrite conflicting external row',()=>{
 const e=environment();e.api.startRecording('A');e.setFail(name=>name==='SensorData');const s=sample(1,e.now());assert.throws(()=>send(e,[s]));
 e.sensor.rows[2]=['external data'];assert.throws(()=>send(e,[s]),/conflict/);assert.equal(e.sensor.rows[2][0],'external data');
});
test('one active session, STOP idempotent, no auto recreate',()=>{
 const e=environment();e.api.startRecording('A');assert.throws(()=>e.api.startRecording('B'));e.api.stopRecording();e.api.stopRecording();
 e.props.delete('SPREADSHEET_ID');assert.equal(send(e,[]).stored,0);assert.throws(()=>send(e,[sample(1,e.now())]),/missing/);assert.equal(e.sensor.getLastRow(),2);
});
test('OTA lease and recording start are mutually exclusive per device',()=>{
 const e=environment(),a='111111111111',b='222222222222';
 e.api.startRecording('active',a);
 assert.throws(()=>e.api.ingest({version:2,device_id:a,samples:[],health:{ota_state:'UPDATING'}}),/Cannot update firmware while recording/);
 e.api.stopRecording(a);
 e.api.ingest({version:2,device_id:a,samples:[],health:{ota_state:'UPDATING'}});
 assert.throws(()=>e.api.startRecording('blocked',a),/Firmware update is active/);
 const other=e.api.startRecording('independent',b);assert.equal(other.state,'RECORDING');
 e.api.ingest({version:2,device_id:a,samples:[],health:{ota_state:'VERIFYING'}});
 assert.throws(()=>e.api.startRecording('blocked while verifying',a),/Firmware update is active/);
 e.api.ingest({version:2,device_id:a,samples:[],health:{ota_state:'IDLE'}});
 assert.equal(e.api.startRecording('released',a).state,'RECORDING');
});
test('CSV route uses safe filenames, session filter and absolute deployed link template',()=>{
 const e=environment();const r=e.api.startRecording('바나나껍데기');send(e,[sample(1,e.now())]);const session=e.api.doGet({parameter:{download:'csv',session:r.session_id}});
 assert(session.text.includes('sample_uid'));assert(session.text.includes(boot+'-1'));assert(!session.text.includes('historical'));
 assert.match(session.filename,/^food-spoilage-session-EXP-[A-Za-z0-9_-]+\.csv$/);assert(!/[^\x20-\x7E]/.test(session.filename));
 assert.equal(e.api.doGet({parameter:{download:'csv'}}).filename,'food-spoilage-all.csv');
 e.api.stopRecording();const stopped=e.api.getDashboard().recording;
 assert.equal(stopped.state,'STOPPED');assert.equal(stopped.download_session_id,r.session_id);assert.equal(stopped.download_kind,'LATEST');
 const ascii=e.api.startRecording('banana-peel');send(e,[sample(2,e.now())]);
 assert.equal(e.api.doGet({parameter:{download:'csv',session:ascii.session_id}}).filename,'banana-peel.csv');
 const html=fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Dashboard.html'),'utf8');assert(html.includes("base='<?= webAppUrl ?>'"));assert(html.includes("&device='+encodeURIComponent(id)"));
});
test('dashboard cannot report live solely from successful RPC',()=>{const e=environment();assert(!e.api.getDashboard().state.includes('ONLINE'));e.api.ingest({version:2,samples:[],health:{ble_connected:true,sensor_ok:false,last_valid_age_ms:100}});assert.equal(e.api.getDashboard().state,'SENSOR ERROR');e.setTime(e.now()+31000);assert(e.api.getDashboard().state.startsWith('STALE'));});
test('large CSV requires explicit pages and never silently truncates',()=>{
 const e=environment();for(let i=0;i<10001;i++)e.sensor.rows.push(['row'+i,25,50,1000,70,'uid'+i,'A']);
 assert.throws(()=>e.api.getCsvData(''),/explicit/);
 const page=e.api.getCsvData('',10000,10000);assert(page.includes('row9999'));assert(!page.includes('row9998'));
 assert.throws(()=>e.api.getCsvData('',0,10001),/Invalid CSV/);
});
test('dashboard defines device-scoped panels, polling, controls and paged CSV links',()=>{
 const html=fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Dashboard.html'),'utf8');
 const code=fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Code.gs'),'utf8');
 assert(code.includes('const DASHBOARD_REFRESH_MS = 5000;'));
 assert(html.includes('getDashboardDevices()'));assert(html.includes('getDashboard(id)'));
 assert(html.includes('startRecording(el(id,\'label\').value,id)'));assert(html.includes('stopRecording(id)'));
 assert(html.includes('새 실험 시작'));assert(html.includes('실험 종료'));assert(html.includes('장치 전체 데이터 다운로드 (CSV)'));
 assert(html.includes('최근 실험 데이터 다운로드 (CSV)'));assert(html.includes('(page-1)*10000'));
 assert(html.includes('h.ota_state===\'UPDATING\''));
 assert(html.includes('ESP32 OTA — gateway firmware'));assert(html.includes('nRF52840 DFU — sensor firmware'));
 assert(html.includes("espOtaLink.href=local+'/'"));assert(html.includes("nrfDfuLink.href=local+'/nrf-dfu'"));
 assert(html.includes("device='+encodeURIComponent(id)"));assert(html.includes("session='+encodeURIComponent(session)"));
 assert(html.includes('section.device'));assert(html.includes('panels=new Map()'));
 const script=html.match(/<script>([\s\S]*?)<\/script>/)[1].replace('<?= refreshMs ?>','5000').replace('<?= webAppUrl ?>','https://example.invalid/exec');
 assert.doesNotThrow(()=>new Function(script));
});
test('dashboard renders two virtual devices and scopes control, errors, and CSV links',()=>{
 const e=environment(),a='111111111111',b='222222222222',elements=new Map();
 e.api.ingest({version:2,device_id:a,samples:[],health:{ble_connected:true}});
 e.api.ingest({version:2,device_id:b,samples:[],health:{ble_connected:false}});
 function element(id){if(!elements.has(id))elements.set(id,{id,value:id.startsWith('label-')?'label-'+id.slice(6):'0',href:'',textContent:'',hidden:false,disabled:false,className:'',remove(){elements.delete(id);}});return elements.get(id);}
 element('loading');const devices=element('devices');devices.insertAdjacentHTML=(_,html)=>{for(const match of html.matchAll(/id="([^"]+)"/g))element(match[1]);};
 let success=null,failure=null,offlineB=false;
 const runner={withSuccessHandler(fn){success=fn;return this;},withFailureHandler(fn){failure=fn;return this;},
  getDashboardDevices(){success(e.api.getDashboardDevices());},getDashboard(id){if(id===b&&offlineB){failure(Error('unit B offline'));return;}success(e.api.getDashboard(id));},
  startRecording(label,id){try{success(e.api.startRecording(label,id));}catch(err){failure(err);}},stopRecording(id){try{success(e.api.stopRecording(id));}catch(err){failure(err);}}};
 const html=fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Dashboard.html'),'utf8');
 const script=html.match(/<script>([\s\S]*?)<\/script>/)[1].replace('<?= refreshMs ?>','5000').replace('<?= webAppUrl ?>','https://example.invalid/exec');
 const ctx={document:{getElementById:element},google:{script:{run:runner}},setInterval(){},setTimeout(){},Number,String,JSON,encodeURIComponent,Map,Date};
 vm.createContext(ctx);vm.runInContext(script,ctx);
 assert(element('panel-'+a)&&element('panel-'+b));
 ctx.show(a,{state:'STALE',age:20000,server_time:e.now(),health:{sensor_mode:'ENV',environment:{t:24.5,h:47.25,p:1002.5}},
  recording:{state:'STOPPED',session_id:null,download_session_id:'EXP-recent',download_experiment_label:'바나나껍데기',download_started_at:e.now(),download_kind:'LATEST'}});
 assert.equal(element('state-'+a).textContent,'STALE');
 assert.equal(element('values-'+a).textContent,'24.50 °C · 47.25 % · 1002.50 hPa');
 assert.equal(element('age-'+a).textContent,'Last environment sample: 20.0 s ago');
 assert.equal(element('recording-'+a).textContent,'실험 종료됨');assert.equal(element('sessionName-'+a).textContent,'바나나껍데기');
 assert(element('sessionDate-'+a).textContent.startsWith('실험 시작: '));assert.equal(element('sessionId-'+a).textContent,'EXP-recent');
 element('page-'+a).value='2';ctx.csvLinks(a);assert(element('all-'+a).href.includes('offset=10000'));assert(element('sessionCsv-'+a).href.includes('offset=10000'));
 ctx.show(a,{state:'LIVE / ONLINE',age:1000,server_time:e.now(),health:{sensor_mode:'ENV',environment:{t:24.5,h:47.25,p:1002.5}},
  recording:{state:'UNKNOWN',session_id:null,download_session_id:null}});
 assert.equal(element('start-'+a).disabled,true);assert.equal(element('stop-'+a).disabled,true);
 ctx.control(a,true);assert.equal(e.api.getDashboard(a).recording.state,'RECORDING');assert.equal(e.api.getDashboard(b).recording.state,'STOPPED');
 assert.equal(element('message-'+a).textContent,'새 실험을 시작했습니다.');
 assert(element('all-'+a).href.includes('device='+a));assert(element('all-'+b).href.includes('device='+b));
 offlineB=true;ctx.poll();assert.equal(element('state-'+a).textContent,e.api.getDashboard(a).state);assert.equal(element('state-'+b).textContent,'CLOUD/UPLOAD ERROR (dashboard request)');
 ctx.control(a,false);assert.equal(e.api.getDashboard(a).recording.state,'STOPPED');assert.equal(e.api.getDashboard(b).recording.state,'STOPPED');
 assert.equal(element('message-'+a).textContent,'실험을 종료했습니다.');
});
test('device loading displays empty, failure, malformed response, and delayed lookup states',()=>{
 const html=fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Dashboard.html'),'utf8');
 const script=html.match(/<script>([\s\S]*?)<\/script>/)[1].replace('<?= refreshMs ?>','5000').replace('<?= webAppUrl ?>','https://example.invalid/exec');
 function run(mode){
  const loading={textContent:'Loading devices…',className:''},devices={insertAdjacentHTML(){}},element=id=>id==='loading'?loading:devices;
  let timer,success,failure;
  const runner={withSuccessHandler(fn){success=fn;return this;},withFailureHandler(fn){failure=fn;return this;},
   getDashboardDevices(){if(mode==='empty')success([]);else if(mode==='failure')failure(Error('sheet unavailable'));
    else if(mode==='throw')throw Error('RPC unavailable');
    else if(mode==='malformed')success({devices:[]});}};
  const ctx={document:{getElementById:element},google:{script:{run:runner}},setInterval(){},setTimeout(fn){timer=fn;},Number,String,JSON,encodeURIComponent,Map,Date};
  vm.createContext(ctx);vm.runInContext(script,ctx);return {loading,timer};
 }
 assert.equal(run('empty').loading.textContent,'No devices registered yet');
 assert.match(run('failure').loading.textContent,/CLOUD ERROR: sheet unavailable/);
 assert.match(run('throw').loading.textContent,/CLOUD ERROR: RPC unavailable/);
 assert.match(run('malformed').loading.textContent,/Invalid device list response/);
 const pending=run('pending');pending.timer();assert.match(pending.loading.textContent,/taking longer than expected/);
});
test('environment history uses schema 4 without gas, keeps SensorData and ACK identity',()=>{
 const e=environment(),t=e.now();const env={...sample(1,t),sample_schema:4};delete env.g;
 const reply=e.api.ingest({version:3,samples:[env]});
 assert.equal(reply.acks[0].status,'stored');assert.equal(e.sensor.getLastRow(),2);
 const history=e.all.get('EnvironmentHistory');assert.equal(history.getLastRow(),2);
 assert.equal(history.rows[1][4],'');assert.equal(history.rows[1][12],4);
 assert.equal(e.api.ingest({version:3,samples:[env]}).acks[0].status,'duplicate');
 assert.equal(history.getLastRow(),2);
 for(const extra of [{g:0},{heater_step:0},{scan_seq:1},{gas_index:0},
                     {meas_index:0},{sensor_status:0},{profile_id:354},
                     {gas_valid:false},{heater_stable:false},{measurement_valid:false}])
  assert.throws(()=>e.api.ingest({version:3,samples:[{...env,uid:boot+'-2',seq:2,...extra}]}));
});
test('environment journal retries without losing existing SensorData',()=>{
 const e=environment(),env={...sample(1,e.now()),sample_schema:4};delete env.g;
 e.setFail(name=>name==='EnvironmentHistory');
 assert.throws(()=>e.api.ingest({version:3,samples:[env]}));
 assert.equal(e.sensor.getLastRow(),2);
 assert.equal(e.api.ingest({version:3,samples:[env]}).acks[0].status,'duplicate');
 assert.equal(e.all.get('EnvironmentHistory').getLastRow(),2);
});
test('delayed environment history keeps capture-time session membership',()=>{
 const e=environment(),first=e.api.startRecording('first'),captured=e.now();
 e.setTime(captured+1000);e.api.stopRecording();e.api.startRecording('second');
 const env={...sample(1,captured),sample_schema:4};delete env.g;
 assert.equal(e.api.ingest({version:3,samples:[env]}).acks[0].status,'stored');
 assert.equal(e.all.get('EnvironmentHistory').rows[1][6],first.session_id);
});
test('dashboard labels scan and cooling as paused with aged ambient telemetry',()=>{
 const e=environment(),t=e.now();
 e.api.ingest({version:3,samples:[],health:{ble_connected:true,sensor_ok:false,
  sensor_mode:'GAS',environment_ok:false,environment:{t:25,h:50,p:1000,age_ms:6000}}});
 const scan=e.api.getDashboard();assert.equal(scan.state,'GAS SCAN / ENVIRONMENT PAUSED');
 assert.equal(scan.age,6000);assert.equal(scan.health.environment.g,undefined);
 e.setTime(t+5000);e.api.ingest({version:3,samples:[],health:{ble_connected:true,
  sensor_mode:'COOL',environment:{t:25,h:50,p:1000,age_ms:11000}}});
 assert.equal(e.api.getDashboard().state,'COOLING / ENVIRONMENT PAUSED');
 const html=fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Dashboard.html'),'utf8');
 assert(!html.includes('Last measured (not current): '));assert(html.includes("el(id,'measurement')"));
 assert(html.includes('`${v.t.toFixed(2)} °C · ${v.h.toFixed(2)} % · ${v.p.toFixed(2)} hPa`'));
 assert(html.includes("GAS:'HP-354 gas scan'"));assert(!html.includes('v.g.toFixed'));
});
test('health-only heartbeat refreshes live environment without storing history',()=>{
 const e=environment(),history=e.all.get('EnvironmentHistory'),rows=history.getLastRow();
 const response=e.api.ingest({version:3,device_id:'111111111111',sensor_id:'a1b2c3d4e5f60708',samples:[],
  health:{ble_connected:true,sensor_ok:true,sensor_mode:'ENV',environment_ok:true,
   environment:{t:26.5,h:48.25,p:1003.75,age_ms:900}}});
 assert.equal(response.stored,0);assert.deepEqual(Array.from(response.acks),[]);
 assert.equal(history.getLastRow(),rows);
 const dashboard=e.api.getDashboard('111111111111');
 assert.equal(dashboard.health.environment.t,26.5);assert.equal(dashboard.age,900);
});
test('routine heartbeats become lock-free while data and OTA state changes stay serialized',()=>{
 const e=environment(),a='111111111111',heartbeat={version:3,device_id:a,sensor_id:'a1b2c3d4e5f60708',samples:[],
  health:{ota_state:'IDLE',sensor_mode:'ENV',environment:{t:24,h:45,p:1001,age_ms:200}}};
 const first=e.api.ingest(heartbeat),afterFirst=e.lockCounts();assert.equal(first.recording.state,'STOPPED');assert(afterFirst.try>0);
 const deviceRows=e.all.get('DeviceRegistry').getLastRow();e.setLockBusy(true);e.setTime(e.now()+5000);
 const second=e.api.ingest({...heartbeat,health:{...heartbeat.health,environment:{t:25,h:46,p:1002,age_ms:300}}});
 assert.equal(second.success,true);assert.equal(second.recording.state,'STOPPED');assert.deepEqual(e.lockCounts(),afterFirst);
 assert.equal(e.all.get('DeviceRegistry').getLastRow(),deviceRows);
 const pending=sample(1,e.now());delete pending.g;pending.sample_schema=4;
 assert.throws(()=>e.api.ingest({version:3,device_id:a,samples:[pending]}),/잠금 시간초과/);
 e.cache.delete('HEARTBEAT_STATE_V1:'+a);
 assert.throws(()=>e.api.ingest(heartbeat),/Cloud state busy/);
 e.setLockBusy(false);e.api.startRecording('concurrent session',a);e.setLockBusy(true);
 assert.equal(e.api.ingest(heartbeat).recording.state,'RECORDING');
 e.setLockBusy(false);e.api.stopRecording(a);assert.equal(e.api.ingest(heartbeat).recording.state,'STOPPED');
 e.setLockBusy(true);
 assert.throws(()=>e.api.ingest({...heartbeat,health:{...heartbeat.health,ota_state:'UPDATING'}}),/잠금 시간초과/);
 e.setLockBusy(false);e.api.ingest({...heartbeat,health:{...heartbeat.health,ota_state:'UPDATING'}});
 e.setLockBusy(true);
 assert.throws(()=>e.api.ingest(heartbeat),/Cloud state busy/);
 e.setLockBusy(false);assert.equal(e.api.ingest(heartbeat).recording.state,'STOPPED');
});
test('dashboard uses cached devices and reports UNKNOWN recording on a contended cache miss',()=>{
 const e=environment(),a='111111111111',heartbeat={version:3,device_id:a,samples:[],
  health:{sensor_mode:'ENV',environment:{t:23,h:44,p:1000,age_ms:100}}};
 e.api.ingest(heartbeat);const devices=e.api.getDashboardDevices();assert.equal(devices[0].device_id,a);
 e.cache.delete('RECORDING_V1:'+a);e.setLockBusy(true);
 const dashboard=e.api.getDashboard(a);assert.equal(dashboard.recording.state,'UNKNOWN');assert.equal(dashboard.health.environment.t,23);
 assert.equal(e.api.getDashboardDevices()[0].device_id,a);
 e.cache.delete('DEVICE_LIST_V1');assert.throws(()=>e.api.getDashboardDevices(),/temporarily busy/);
});
test('lock processing time advances server_seen_at and sample ages without creating history',()=>{
 const e=environment(),a='111111111111',history=e.all.get('EnvironmentHistory'),rows=history.getLastRow();
 e.setLockAdvance(5000);const arrival=e.now();
 e.api.ingest({version:3,device_id:a,samples:[],health:{ota_state:'IDLE',sensor_mode:'ENV',last_valid_age_ms:2000,
  environment:{t:22,h:43,p:999,age_ms:1000}}});
 const dashboard=e.api.getDashboard(a);
 assert.equal(dashboard.health.server_seen_at,arrival+5000);assert.equal(dashboard.health.environment.age_ms,6000);
 assert.equal(dashboard.health.last_valid_age_ms,7000);assert.equal(dashboard.age,6000);assert.equal(history.getLastRow(),rows);
});
test('a delayed older request cannot replace a newer heartbeat environment sample',()=>{
 const e=environment(),a='111111111111',newer={version:3,device_id:a,samples:[],
  health:{sensor_mode:'ENV',environment:{t:26,h:48,p:1004,age_ms:100}}};
 e.api.ingest(newer);const oldArrival=e.now()-5000;
 e.api.cacheHealth({version:3,health:{sensor_mode:'ENV',environment:{t:20,h:40,p:998,age_ms:100}}},
  {sensorId:''},[],a,oldArrival);
 const dashboard=e.api.getDashboard(a);
 assert.equal(dashboard.health.environment.t,26);assert.equal(dashboard.age,100);
});
test('two virtual gateways keep recording, live cache, registry, and CSV isolated',()=>{
 const e=environment(),a='111111111111',b='222222222222',t=e.now(),bootA='a'.repeat(32),bootB='b'.repeat(32);
 const recA=e.api.startRecording('unit one',a),recB=e.api.startRecording('unit two',b);
 assert.equal(recA.state,'RECORDING');assert.equal(recB.state,'RECORDING');
 const sa={...sample(1,t),uid:bootA+'-1'},sb={...sample(1,t),uid:bootB+'-1'};
 assert.equal(e.api.ingest({version:2,device_id:a,sensor_id:'aaaabbbbcccc',samples:[sa],health:{ble_connected:true,latest:{t:20,h:40,p:1000,g:55}}}).stored,1);
 assert.equal(e.api.ingest({version:2,device_id:b,sensor_id:'ddddeeeeffff',samples:[sb],health:{ble_connected:false,latest:{t:30,h:60,p:1000,g:88}}}).stored,1);
 assert.equal(e.sensor.rows[2][22],a);assert.equal(e.sensor.rows[3][22],b);
 assert.equal(e.api.getDashboard(a).health.latest.g,55);assert.equal(e.api.getDashboard(b).health.latest.g,88);
 assert.deepEqual(Array.from(e.api.getDashboardDevices(),x=>x.device_id).sort(),[a,b]);
 e.api.stopRecording(a);assert.equal(e.api.getDashboard(a).recording.state,'STOPPED');
 assert.equal(e.api.getDashboard(b).recording.state,'RECORDING');
 assert(e.api.getCsvData(recA.session_id,0,100,a).includes(bootA+'-1'));
 assert(!e.api.getCsvData(recA.session_id,0,100,a).includes(bootB+'-1'));
 assert(e.api.getCsvData(recB.session_id,0,100,b).includes(bootB+'-1'));
 assert(e.api.getCsvData('').includes(bootA+'-1')&&e.api.getCsvData('').includes(bootB+'-1'));
 assert.equal(e.api.ingest({version:2,device_id:a,samples:[sa]}).acks[0].status,'duplicate');
 assert.throws(()=>e.api.getDashboard('abc'),/Invalid device ID/);
 const html=fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Dashboard.html'),'utf8');
 assert(html.includes('.getDashboard(id)'));assert(html.includes('.startRecording(el(id,\'label\').value,id)'));
 assert(html.includes('.stopRecording(id)'));assert(html.includes("device='+encodeURIComponent(id)"));
});
console.log(`${tests} cloud tests passed (mock Sheets, no external services).`);
