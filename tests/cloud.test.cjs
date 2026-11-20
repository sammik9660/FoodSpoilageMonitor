const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
let tests=0;
function environment(options={}){
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
  PropertiesService:{getScriptProperties:()=>({getProperty:k=>props.get(k),setProperty:(k,v)=>props.set(k,v)})},
  SpreadsheetApp:{openById:id=>{assert.equal(id,'existing-id');return book;},flush(){}},
  Utilities:{getUuid:()=>String(++uuid).padStart(32,'0')},
  CacheService:{getScriptCache:()=>({get:k=>cache.get(k),put:(k,v)=>cache.set(k,v),remove:k=>cache.delete(k)})},
  ContentService:{MimeType:{JSON:'json',CSV:'csv'},createTextOutput:text=>({text,filename:null,setMimeType(){return this;},downloadAsFile(name){if(/[^\x20-\x7E]/.test(name))throw Error('잘못된 인수: '+name);this.filename=name;return this;}})},
  ScriptApp:{getService:()=>({getUrl:()=> 'https://example.invalid/exec'})},
  HtmlService:{createTemplateFromFile:name=>({evaluate(){return {setTitle(){return {name,url:this.webAppUrl};}};}})}
 };
 vm.createContext(api);vm.runInContext(fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Code.gs'),'utf8'),api);
 vm.runInContext(fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Monitoring.gs'),'utf8'),api);
 vm.runInContext(fs.readFileSync(path.join(__dirname,'../cloud/apps_script/StorageSessions.gs'),'utf8'),api);
  if(!options.noProgress){
    vm.runInContext(fs.readFileSync(path.join(__dirname,'../cloud/apps_script/FoodProgressModel.gs'),'utf8'),api);
    vm.runInContext(fs.readFileSync(path.join(__dirname,'../cloud/apps_script/FoodProgress.gs'),'utf8'),api);
  }
 vm.runInContext(fs.readFileSync(path.join(__dirname,'../cloud/apps_script/FoodReview.gs'),'utf8'),api);
 vm.runInContext(fs.readFileSync(path.join(__dirname,'../cloud/apps_script/StorageRisk.gs'),'utf8'),api);
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
 assert(html.includes("loadBatteryHistory(id,'24h')"));assert(html.includes('.getBatteryHistory(id,range)'));
 assert(html.includes('p.measured_at-current[current.length-1].measured_at>900000'));
 assert(html.includes("espOtaLink.href=local+'/'"));assert(html.includes("nrfDfuLink.href=local+'/nrf-dfu'"));
 assert(html.includes("const local='http:'+'/'+'/'+h.local_ip+':8080'"));
 assert(html.includes("device='+encodeURIComponent(id)"));assert(html.includes("session='+encodeURIComponent(session)"));
 assert(html.includes('section.device'));assert(html.includes('panels=new Map()'));
 const script=html.match(/<script>([\s\S]*?)<\/script>/)[1].replace('<?= refreshMs ?>','5000').replace('<?= webAppUrl ?>','https://example.invalid/exec');
 assert.doesNotThrow(()=>new Function(script));
});
test('dashboard renders two virtual devices and scopes control, errors, and CSV links',()=>{
 const e=environment(),a='111111111111',b='222222222222',elements=new Map();
 e.api.ingest({version:2,device_id:a,samples:[],health:{ble_connected:true}});
 e.api.ingest({version:2,device_id:b,samples:[],health:{ble_connected:false}});
 function element(id){if(!elements.has(id))elements.set(id,{id,value:id.startsWith('label-')?'label-'+id.slice(6):'0',href:'',textContent:'',innerHTML:'',title:'',hidden:false,disabled:false,className:'',remove(){elements.delete(id);}});return elements.get(id);}
 element('loading');const devices=element('devices');devices.insertAdjacentHTML=(_,html)=>{for(const match of html.matchAll(/id="([^"]+)"/g))element(match[1]);};
 let success=null,failure=null,offlineB=false;
 const runner={withSuccessHandler(fn){success=fn;return this;},withFailureHandler(fn){failure=fn;return this;},
  getDashboardDevices(){success(e.api.getDashboardDevices());},getDashboard(id){if(id===b&&offlineB){failure(Error('unit B offline'));return;}success(e.api.getDashboard(id));},getBatteryHistory(id,range){success(e.api.getBatteryHistory(id,range));},
  startRecording(label,id){try{success(e.api.startRecording(label,id));}catch(err){failure(err);}},stopRecording(id){try{success(e.api.stopRecording(id));}catch(err){failure(err);}}};
 const html=fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Dashboard.html'),'utf8');
 const script=html.match(/<script>([\s\S]*?)<\/script>/)[1].replace('<?= refreshMs ?>','5000').replace('<?= webAppUrl ?>','https://example.invalid/exec');
 const ctx={document:{getElementById:element},google:{script:{run:runner}},setInterval(){},setTimeout(){},Number,String,JSON,encodeURIComponent,Map,Date};
 vm.createContext(ctx);vm.runInContext(script,ctx);
 assert(element('panel-'+a)&&element('panel-'+b));
 ctx.show(a,{state:'STALE',age:20000,server_time:e.now(),health:{server_seen_at:e.now(),sensor_mode:'ENV',environment:{t:24.5,h:47.25,p:1002.5},battery_mv:4090,battery_percent:89,battery_calibration_state:'COLLECTING',battery_min_mv:3600,battery_max_mv:4150,battery_measurement_valid:true,usb_power:false,local_ip:'192.168.0.142'},
  recording:{state:'STOPPED',session_id:null,download_session_id:'EXP-recent',download_experiment_label:'바나나껍데기',download_started_at:e.now(),download_kind:'LATEST'}});
 assert.equal(element('state-'+a).textContent,'STALE');
 assert.equal(element('values-'+a).textContent,'24.50 °C · 47.25 % · 1002.50 hPa');
 assert.equal(element('battery-'+a).textContent,'BAT 4.09 V · 89%');
 assert.equal(element('espOtaLink-'+a).href,'http://192.168.0.142:8080/');
 assert.equal(element('nrfDfuLink-'+a).href,'http://192.168.0.142:8080/nrf-dfu');
 assert(element('battery-'+a).title.includes('COLLECTING'));
 assert.equal(element('age-'+a).textContent,'Last environment sample: 20.0 s ago');
 assert.equal(element('recording-'+a).textContent,'실험 종료됨');assert.equal(element('sessionName-'+a).textContent,'바나나껍데기');
 assert(element('sessionDate-'+a).textContent.startsWith('실험 시작: '));assert.equal(element('sessionId-'+a).textContent,'EXP-recent');
 ctx.show(a,{state:'LIVE / ONLINE',age:1000,server_time:e.now(),health:{server_seen_at:e.now(),local_ip:'192.168.68.57',remote_capable:false},recording:{state:'STOPPED'}});
 assert.equal(element('espOtaLink-'+a).href,'http://192.168.68.57:8080/');
  ctx.show(a,{state:'LIVE / ONLINE',age:1000,server_time:e.now(),health:{server_seen_at:e.now(),local_ip:'10.0.0.57',remote_capable:false},recording:{state:'STOPPED'}});
  assert.equal(element('espOtaLink-'+a).href,'http://10.0.0.57:8080/');
  assert.equal(element('nrfDfuStatusLink-'+a).href,'http://10.0.0.57:8080/nrf-dfu/status');
  assert.equal(element('espOtaLink-'+a).hidden,false);
  assert.equal(ctx.privateLanIp('172.16.1.57'),true);
  assert.equal(ctx.privateLanIp('8.8.8.8'),false);
 assert(element('remotePrereq-'+a).textContent.includes('장치 키'));
 assert(element('remoteStart-'+a).disabled);
 ctx.show(a,{state:'STALE',age:120000,server_time:e.now(),health:{server_seen_at:e.now()-120000,local_ip:'192.168.68.57',remote_capable:true},recording:{state:'STOPPED'}});
 assert(!element('espOtaLink-'+a).hidden&&element('espOtaLink-'+a).title.includes('마지막 보고'));
 assert(element('localLinkStatus-'+a).textContent.includes('마지막 보고된 로컬 주소'));
 assert(element('remoteStart-'+a).disabled);
 assert(element('remotePrereq-'+a).textContent.includes('최근 클라우드'));
 element('page-'+a).value='2';ctx.csvLinks(a);assert(element('all-'+a).href.includes('offset=10000'));assert(element('sessionCsv-'+a).href.includes('offset=10000'));
 ctx.show(a,{state:'LIVE / ONLINE',age:1000,server_time:e.now(),health:{sensor_mode:'ENV',environment:{t:24.5,h:47.25,p:1002.5},battery_measurement_valid:false,usb_power:true,battery_last_percent:89},
  recording:{state:'UNKNOWN',session_id:null,download_session_id:null}});
 assert.equal(element('battery-'+a).textContent,'BAT USB · last 89%');
 assert.match(element('battery-'+a).title,/마지막 배터리 단독 추정치/);
 assert.equal(element('start-'+a).disabled,true);assert.equal(element('stop-'+a).disabled,true);
 ctx.control(a,true);assert.equal(e.api.getDashboard(a).recording.state,'RECORDING');assert.equal(e.api.getDashboard(b).recording.state,'STOPPED');
 assert.equal(element('message-'+a).textContent,'새 실험을 시작했습니다.');
 assert(element('all-'+a).href.includes('device='+a));assert(element('all-'+b).href.includes('device='+b));
 offlineB=true;ctx.poll();assert.equal(element('state-'+a).textContent,e.api.getDashboard(a).state);assert.equal(element('state-'+b).textContent,'CLOUD/UPLOAD ERROR (dashboard request)');
 ctx.control(a,false);assert.equal(e.api.getDashboard(a).recording.state,'STOPPED');assert.equal(e.api.getDashboard(b).recording.state,'STOPPED');
 assert.equal(element('message-'+a).textContent,'실험을 종료했습니다.');
});
 test('food section leads each device while management controls and two-device isolation remain',()=>{
 const html=fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Dashboard.html'),'utf8');
 const script=html.match(/<script>([\s\S]*?)<\/script>/)[1].replace('<?= refreshMs ?>','5000').replace('<?= webAppUrl ?>','https://example.invalid/exec');
  const ids=['111111111111','222222222222'],elements=new Map(),calls=[],storageCalls=[],backfillCalls=[],now=Date.now();let failSecond=false,holdSecond=false,pendingFood,foodStatus='EXPERIMENTAL_ONLY',foodAt=now,foodSensor='aaaabbbbccccdddd',anomalyIndex=1.2,riskWarning=true,progressCode=null;
 function element(id){if(!elements.has(id))elements.set(id,{id,value:'',href:'',textContent:'',innerHTML:'',children:[],hidden:false,disabled:false,
  appendChild(x){this.children.push(x);},remove(){elements.delete(id);}});return elements.get(id);}
 element('loading');element('remoteToken');element('remoteTokenStatus');element('devices').insertAdjacentHTML=(_,markup)=>{for(const m of markup.matchAll(/id="([^"]+)"/g))element(m[1]);};
 let success,failure;
 const runner={withSuccessHandler(fn){success=fn;return this;},withFailureHandler(fn){failure=fn;return this;},
   getDashboardDevices(){success(ids.map(device_id=>({device_id})));},getBatteryHistory(){success({points:[],range:'24h',total:0});},
   updateStorageDetails(...args){storageCalls.push(args);success({});},
   getStorageGasBackfill(id){backfillCalls.push(id);success({complete_scans:4,status:'READY'});},
  getDashboard(id){success({state:'LIVE / ONLINE',age:1000,server_time:now,health:{server_seen_at:now,environment:{t:25,h:50,p:1000}},recording:{state:'RECORDING',session_id:'EXP-'+id}});},
  getFoodReview(id,selected){calls.push([id,selected]);if(id===ids[1]&&failSecond){failure(Error('food RPC offline'));return;}
   const deliver=success,result={choices:[{id:'EXP-'+id,label:'현재 음식',stopped_at:null},{id:'EXP-past',label:'과거 음식',stopped_at:now-1000}],
     session:selected||'EXP-'+id,live:!selected,storage_started_at:now-3600000,food_name:selected?'과거 음식':'현재 음식',food_source:'실험명에 입력된 음식',elapsed_ms:3600000,
    health:{server_seen_at:now,ble_connected:true,sensor_id:'aaaabbbbccccdddd',
      environment:{t:24.5,h:63.2,age_ms:id===ids[0]?600000:60000},
       food_ai:id===ids[1]?{sensor_id:foodSensor,status:foodStatus,candidate:'chicken',inferred_at:foodAt}:null,
       gas_anomaly:id===ids[1]?{storage_session_id:'STO-'+'a'.repeat(32),sensor_id:'aaaabbbbccccdddd',status:'EXPERIMENTAL_INDEX',index:anomalyIndex,inferred_at:now}:null},
    storage_session:id===ids[1]?{state:'ACTIVE',storage_session_id:'STO-'+'a'.repeat(32),sensor_id:'aaaabbbbccccdddd'}:null,
    gas_status:{code:id===ids[0]?'DELAYED':'NORMAL',text:id===ids[0]?'GAS 주기 초과':'GAS 정상 주기 대기',at:now-120000},
    storage_risk:id===ids[0]?{score:null,reasons:['식품 상태에 맞는 기간 기준 없음'],period_score:null,temperature_score:100,observed_h:82.9,over_4c_h:65.4,temperature_warning:'4°C 초과 관측 시간이 2시간 이상입니다.'}:
       {score:54,reasons:[],period_score:8,temperature_score:100,observed_h:3,over_4c_h:2,temperature_warning:riskWarning?'냉장 기준 초과':null},
     progress:progressCode?{code:progressCode,index:50,at:now,basis:'model_time',model:'assumed-progress-v1',
       groups:10,evaluated:1139,label_basis:'초반·후반 가정',reason:'음식 종류와 보관 시간'}:null,
    decision:{code:'UNKNOWN',title:'판정 불가',icon:'⚪',tone:'unknown',reasons:['검증 레이블 부족'],judged_at:null}};
   if(id===ids[1]&&holdSecond){holdSecond=false;pendingFood=()=>deliver(result);return;}deliver(result);}};
  const opened=[];
  const ctx={document:{getElementById:element,createElement:()=>({})},google:{script:{run:runner}},
   window:{open:url=>opened.push(url)},setInterval(){},setTimeout(){},Number,String,JSON,Map,Date,encodeURIComponent};
 vm.createContext(ctx);vm.runInContext(script,ctx);
  for(const id of ids){assert(element('panel-'+id));assert(element('all-'+id).href.includes('device='+id));assert.equal(element('foodAiState-'+id).textContent,'');assert.equal(element('foodAiPanel-'+id).hidden,true);}
  const markup=ctx.panelMarkup(ids[0],0);assert(markup.indexOf('id="foodCard-'+ids[0])<markup.indexOf('id="health-'+ids[0]));
  assert(markup.includes('관리·개발 기능'));assert(markup.includes('실제 음식 투입 시각'));
  assert(markup.includes('manualLocalIp-'+ids[0]));
  assert(html.includes('id="fieldRecoveryIp"'));
  element('manualLocalIp-'+ids[1]).value='192.168.1.124';ctx.openManualLocal(ids[1],'/remote-config');
  assert.deepEqual(opened,['http://192.168.1.124:8080/remote-config']);
  element('fieldRecoveryIp').value='10.0.0.57';ctx.openFieldRecovery('/nrf-dfu/status');
  assert.equal(opened[1],'http://10.0.0.57:8080/nrf-dfu/status');
  element('manualLocalIp-'+ids[1]).value='8.8.8.8';ctx.openManualLocal(ids[1],'/');
  assert.equal(opened.length,2);
 assert(markup.includes('<details><summary>상세 진단 JSON 보기</summary>'));
 assert.equal(element('foodMeasure-'+ids[0]).textContent,'환경 측정 갱신 대기');
 assert.equal(element('foodMeasure-'+ids[1]).textContent,'환경 측정 정상');assert.equal(element('foodTemp-'+ids[1]).textContent,'24.5 °C');
 assert.equal(element('foodGasState-'+ids[0]).textContent,'GAS 주기 초과');
 assert.equal(element('foodGasState-'+ids[1]).textContent,'GAS 정상 주기 대기');
  assert.equal(element('foodRiskScore-'+ids[0]).textContent,'온도 노출 100 / 100');
 assert.equal(element('foodTitle-'+ids[0]).textContent,'음식 보관 중');
  assert.equal(element('foodAiState-'+ids[0]).textContent,'');
  assert(!/미평가|미검증|부족/.test([element('foodTitle-'+ids[0]).textContent,
    element('foodRiskScore-'+ids[0]).textContent,element('foodRiskReason-'+ids[0]).textContent,
    element('foodReason-'+ids[0]).textContent].join(' ')));
 assert.equal(element('foodRiskScore-'+ids[1]).textContent,'54 / 100');
 assert.equal(element('foodTempWarning-'+ids[1]).textContent,'냉장 기준 초과');
 assert.match(element('foodEdge-'+ids[1]).textContent,/닭가슴살/);
  assert.equal(element('foodAiCandidate-'+ids[1]).textContent,'닭가슴살 (추정)');
   assert.match(element('foodAiCandidateDetail-'+ids[1]).textContent,/ESP32 최근 GAS/);
  assert.equal(element('foodAiCandidate-'+ids[0]).textContent,'최근 추론 결과 없음');
  foodStatus='ABSTAIN_ONE_EXPOSURE';ctx.loadFoodReview(ids[1],true);
  assert.equal(element('foodAiCandidate-'+ids[1]).textContent,'닭가슴살 (추정)');
   assert.match(element('foodAiCandidateDetail-'+ids[1]).textContent,/ESP32 최근 GAS/);
  foodAt=now-1300000;ctx.loadFoodReview(ids[1],true);
  assert.equal(element('foodAiCandidate-'+ids[1]).textContent,'최근 추론 결과 없음');
  foodAt=now;foodSensor='ffffffffffffffff';ctx.loadFoodReview(ids[1],true);
  assert.equal(element('foodAiCandidate-'+ids[1]).textContent,'최근 추론 결과 없음');
  foodSensor='aaaabbbbccccdddd';foodStatus='EXPERIMENTAL_ONLY';ctx.loadFoodReview(ids[1],true);
 assert.match(element('foodAnomaly-'+ids[1]).textContent,/1.20/);
  assert.match(element('foodAnomaly-'+ids[1]).textContent,/가스 패턴 변화 관찰/);
  assert.equal(element('foodAnomaly-'+ids[1]).className,'food-signal watch');
  anomalyIndex=5;ctx.loadFoodReview(ids[1],true);
  assert.equal(element('foodAnomaly-'+ids[1]).className,'food-signal high');
  anomalyIndex=0.8;ctx.loadFoodReview(ids[1],true);
  assert.match(element('foodAnomaly-'+ids[1]).textContent,/변화 작음/);
  anomalyIndex=1.2;ctx.loadFoodReview(ids[1],true);
  riskWarning=false;progressCode='MIDDLE';ctx.loadFoodReview(ids[1],true);
   assert.equal(element('foodTitle-'+ids[1]).textContent,'소비 우선 권장');
  assert.equal(element('foodCard-'+ids[1]).className,'food-status suspect');
   assert.match(element('foodAiReason-'+ids[1]).textContent,/음식 종류와 보관 시간/);
   assert.equal(element('foodAiPanel-'+ids[1]).hidden,false);
  assert.match(element('foodModelEvidence-'+ids[1]).textContent,/실측 부패 정확도/);
  riskWarning=true;progressCode='EARLY';ctx.loadFoodReview(ids[1],true);
  assert.equal(element('foodTitle-'+ids[1]).textContent,'신선 단계 추정');
  assert.equal(element('foodCard-'+ids[1]).className,'food-status fresh');
  assert.equal(element('foodTempWarning-'+ids[1]).textContent,'냉장 기준 초과');
  progressCode='MIDDLE';ctx.loadFoodReview(ids[1],true);
  assert.equal(element('foodCard-'+ids[1]).className,'food-status suspect');
  progressCode='LATE';ctx.loadFoodReview(ids[1],true);
  assert.equal(element('foodCard-'+ids[1]).className,'food-status signs');
  riskWarning=true;progressCode=null;ctx.loadFoodReview(ids[1],true);
 holdSecond=true;ctx.loadFoodReview(ids[1],true);
 element('foodSession-'+ids[1]).value='EXP-past';ctx.loadFoodReview(ids[1],true);
 assert.equal(element('foodName-'+ids[1]).textContent,'음식 정보 확인 중');pendingFood();
 assert.equal(element('foodName-'+ids[1]).textContent,'과거 음식 · 실험명에 입력된 음식');
  assert.equal(element('foodAiCandidate-'+ids[1]).textContent,'최근 추론 결과 없음');
  assert.match(element('foodAnomaly-'+ids[1]).textContent,/확인 대기/);
 element('foodSession-'+ids[1]).value='EXP-past';ctx.loadFoodReview(ids[1],true);
 assert.equal(element('foodSession-'+ids[1]).value,'EXP-past');assert.equal(element('foodName-'+ids[1]).textContent,'과거 음식 · 실험명에 입력된 음식');
 failSecond=true;ctx.loadFoodReview(ids[1],true);
 assert.equal(element('foodTitle-'+ids[1]).textContent,'보관 조건 확인 중');assert.equal(element('foodName-'+ids[1]).textContent,'음식 정보 확인 중');
  assert.equal(element('foodAiCandidate-'+ids[1]).textContent,'확인 불가');
  assert.equal(element('foodAnomaly-'+ids[1]).className,'food-signal');
 assert.equal(element('foodTime-'+ids[1]).textContent,'--');assert.equal(element('foodTemp-'+ids[1]).textContent,'--');
 assert.equal(element('foodMeasure-'+ids[1]).textContent,'환경 조회 실패');assert.equal(element('foodGasState-'+ids[1]).textContent,'GAS 조회 실패');assert.match(element('foodReason-'+ids[1]).textContent,/food RPC offline/);
 assert.equal(element('foodName-'+ids[0]).textContent,'현재 음식 · 실험명에 입력된 음식');
 assert(element('all-'+ids[1]).href.includes('device='+ids[1]));
 assert(calls.some(([id,selected])=>id===ids[1]&&selected==='EXP-past'));
  vm.runInContext(`panels.get('${ids[1]}').storage={state:'ACTIVE',container_name:'기존 용기',placed_at:${now-3600000}}`,ctx);
  element('containerName-'+ids[1]).value='같은 용기';element('storageFood-'+ids[1]).value='생숙주';
  element('storagePlaced-'+ids[1]).value='2026-11-16T15:00';ctx.storageControl(ids[1],true);
  assert.equal(storageCalls.length,1);assert.equal(storageCalls[0][0],ids[1]);
  assert.equal(storageCalls[0][1],'같은 용기');assert.equal(storageCalls[0][2],'생숙주');
  assert(Number.isFinite(storageCalls[0][3]));
  assert.deepEqual(backfillCalls,[ids[1]]);
});
test('food review uses live data but abstains on stale, incomplete and unverified status',()=>{
 const e=environment(),id='111111111111',t=e.now(),s=e.api.startRecording('숙주나물 냉장',id);
 const health={ble_connected:true,sensor_ok:true,server_seen_at:t};
 const base={session:s.session_id,live:true,now:t,health,scan:{at:t,quality:'OK'}};
 assert.equal(e.api.foodDecision_(base).code,'UNKNOWN');
 assert.match(e.api.foodDecision_(base).reasons.join(' '),/레이블/);
 assert.match(e.api.foodDecision_({...base,now:t+120000}).reasons.join(' '),/통신/);
 assert.match(e.api.foodDecision_({...base,scan:{at:t,quality:'NEWER_INCOMPLETE_SCAN'}}).reasons.join(' '),/정보가 부족/);
 assert.match(e.api.foodDecision_({...base,scan:null}).reasons.join(' '),/완성 GAS/);
 assert.match(e.api.foodDecision_({...base,scan:null,window_limited:true}).reasons.join(' '),/조회 범위 밖/);
 const review=e.api.getFoodReview(id,'');assert.equal(review.session,s.session_id);
 assert.equal(review.food_name,'숙주나물');assert.equal(review.food_source,'실험명에 입력된 음식');
 assert.equal(review.decision.code,'UNKNOWN');
});
test('two food reviews remain read-only while ingest Script Lock is busy',()=>{
 const e=environment(),a='111111111111',b='222222222222';
 e.api.startRecording('숙주나물 냉장',a);e.api.startRecording('삶은계란 상온',b);
 const before=e.lockCounts();e.setLockBusy(true);
 const first=e.api.getFoodReview(a,''),second=e.api.getFoodReview(b,'');
 assert.equal(first.food_name,'숙주나물');assert.equal(second.food_name,'삶은계란');
 assert.equal(first.decision.code,'UNKNOWN');assert.equal(second.decision.code,'UNKNOWN');
 assert.deepEqual(e.lockCounts(),before);
 assert.equal(e.sensor.getLastRow(),2);e.setLockBusy(false);
});
test('GAS freshness follows ten-minute complete-scan cadence independently of ENV',()=>{
 const e=environment(),now=e.now(),base={session:'EXP-live',live:true,now,
   health:{server_seen_at:now,ble_connected:true},scan:{at:now-120000,quality:'OK'}};
 assert.equal(e.api.foodGasStatus_(base).code,'NORMAL'); // two minutes is normal
 assert.equal(e.api.foodGasStatus_({...base,scan:{at:now-780001,quality:'OK'}}).code,'DELAYED');
 assert.equal(e.api.foodGasStatus_({...base,scan:{at:now-1500001,quality:'OK'}}).code,'STALLED');
 assert.equal(e.api.foodGasStatus_({...base,scan:null}).code,'NO_COMPLETE_SCAN');
 assert.equal(e.api.foodGasStatus_({...base,scan:null,window_limited:true}).code,'WINDOW_LIMIT');
 assert.equal(e.api.foodGasStatus_({...base,health:{server_seen_at:now-91000,ble_connected:true}}).code,'DISCONNECTED');
 assert.equal(e.api.foodGasStatus_({...base,live:false}).code,'HISTORICAL');
 assert.equal(e.api.foodGasStatus_({...base,scan:{at:now-1500000,quality:'OK'},
   health:{server_seen_at:now,ble_connected:true,ai_shadow:{last_complete_scan_at:now-120000}}}).code,'RECORDING_PENDING');
 assert.equal(e.api.foodGasStatus_({...base,scan:{at:now-1000000,quality:'OK'},newer_input_at:now-10000}).code,'DELAYED');
 assert.equal(e.api.foodDecision_({...base,health:{server_seen_at:now,ble_connected:true,sensor_ok:true}}).code,'UNKNOWN');
});
test('food review refreshes GAS age across a 60-second cached result',()=>{
 const e=environment(),id='111111111111',started=e.now();e.api.startRecording('밥 상온',id);
 const frames=Array.from({length:10},(_,i)=>v3(i+1,i,started));
 assert.equal(e.api.ingest({version:3,device_id:id,samples:frames,health:{ble_connected:true,sensor_ok:true}}).stored,10);
 e.setTime(started+750000);
 e.api.ingest({version:3,device_id:id,samples:[],health:{ble_connected:true,sensor_ok:true}});
 const first=e.api.getFoodReview(id,'');assert.equal(first.gas_status.code,'NORMAL');
 e.setTime(started+795000);
 e.api.ingest({version:3,device_id:id,samples:[],health:{ble_connected:true,sensor_ok:true}});
 const cached=e.api.getFoodReview(id,'');assert.equal(cached.gas_status.code,'DELAYED');
 assert(cached.gas_status.age_ms>first.gas_status.age_ms);
});
test('new partial GAS input preserves complete fingerprint quality and 127-minute stall',()=>{
 const e=environment(),id='111111111111',t=e.now();e.api.startRecording('숙주나물 냉장',id);
 const complete=Array.from({length:10},(_,i)=>({...v3(i+1,i,t),g:50+i*10}));
 assert.equal(e.api.ingest({version:3,device_id:id,samples:complete,health:{ble_connected:true,sensor_ok:true}}).stored,10);
 e.setTime(t+127*60000);
 const partial={...v3(11,0,e.now()),scan_seq:2,g:62};
 assert.equal(e.api.ingest({version:3,device_id:id,samples:[partial],health:{ble_connected:true,sensor_ok:true}}).stored,1);
 const review=e.api.getFoodReview(id,'');
 assert.equal(review.scan.quality,'OK');assert.equal(review.gas_status.code,'STALLED');
 assert(review.newer_input_at>review.scan.at);assert(review.rejected_groups.incomplete>=1);
 assert.equal(review.food_prediction,null);
});
test('storage rule scores complete cold history and flags room exposure without GAS',()=>{
 const e=environment(),now=e.now(),start=now-3600000;
 const points=Array.from({length:13},(_,i)=>({at:start+i*300000,t:3,h:90}));
 const cold=e.api.storageRisk_({now,started_at:start,label:'삶은계란',points});
 assert(Number.isFinite(cold.score)&&cold.score<20);assert.equal(cold.temperature_score,0);
 assert.equal(cold.official_duration_h,168);assert.equal(cold.official_c,4);
 assert.equal(cold.humidity_score,null);assert.equal(cold.gas_added_to_score,false);
 const warm=e.api.storageRisk_({now,started_at:start,label:'삶은계란',points:points.map(p=>({...p,t:25}))});
 assert(warm.score>cold.score);assert(warm.temperature_warning);
 assert.equal(warm.temperature_state,'ACTIVE');
});
test('brief refrigerator excursions preserve exposure but do not become active warnings',()=>{
 const e=environment(),now=e.now(),start=now-3*3600000;
 const points=Array.from({length:37},(_,i)=>({at:start+i*300000,t:i<30?5:i===36?4.5:3}));
 const brief=e.api.storageRisk_({now,started_at:start,label:'삶은계란',points});
  assert(brief.over_4c_h>2);assert.equal(brief.temperature_state,'REFERENCE_EXPOSURE');
  assert.equal(brief.temperature_warning,null);assert.equal(brief.temperature_note,null);
 const cooled=e.api.storageRisk_({now,started_at:start,label:'삶은계란',points:points.map((p,i)=>i===36?{...p,t:3}:p)});
 assert.equal(cooled.temperature_state,'HISTORICAL');assert.equal(cooled.temperature_warning,null);
 assert.equal(cooled.temperature_score,brief.temperature_score); // original cumulative exposure preserved
  const recent=Array.from({length:7},(_,i)=>({at:now-1800000+i*300000,t:6.5}));
 const sustained=e.api.storageRisk_({now,started_at:now-1800000,label:'삶은계란',points:recent});
 assert.equal(sustained.temperature_state,'ACTIVE');assert(sustained.temperature_warning);
  assert.equal(sustained.active_alert_c,6);
  const cycling=e.api.storageRisk_({now,started_at:now-1800000,label:'삶은계란',
    points:recent.map(p=>({...p,t:5.8}))});
  assert(cycling.over_4c_h>0);assert.equal(cycling.temperature_warning,null);
  assert.equal(cycling.temperature_state,'REFERENCE_EXPOSURE');
});
test('storage rule preserves nulls for unknown preparation, missing start, duration and gaps',()=>{
 const e=environment(),now=e.now(),start=now-3600000;
 const sparse=[{at:start,t:3},{at:now,t:3}];
 const gap=e.api.storageRisk_({now,started_at:start,label:'삶은계란',points:sparse});
 assert.equal(gap.score,null);assert(gap.has_gap);assert(gap.reasons.some(x=>x.includes('공백')));
 const noStart=e.api.storageRisk_({now,started_at:null,label:'삶은계란',points:[]});
 assert.equal(noStart.score,null);assert.equal(noStart.period_score,null);assert.equal(noStart.temperature_score,null);
 assert.equal(noStart.observed_h,null);assert.equal(noStart.over_4c_h,null);
 const sprouts=e.api.storageRisk_({now,started_at:start,label:'숙주나물',points:[]});
 assert.equal(sprouts.official_duration_h,null);assert.equal(sprouts.score,null);
 assert.equal(e.api.storageRiskProfile_('생닭').duration_h,24);
 assert.equal(e.api.storageRiskProfile_('익힌 닭').duration_h,72);
 assert.equal(e.api.storageRiskProfile_('생달걀').duration_h,null);
 assert.equal(e.api.storageRiskProfile_('생달걀 껍질').duration_h,504);
 assert.equal(e.api.storageRiskProfile_('익힌 숙주').duration_h,72);
 const elapsed=e.api.storageRisk_({now,started_at:now-169*3600000,label:'삶은계란',points:[]});
 assert(elapsed.period_score>=70);assert.equal(elapsed.score,null);
});
test('active STO telemetry supplies a rule score without changing EXP recording',()=>{
 const e=environment(),device='111111111111',sensor='aaaabbbbccccdddd';
 const recording=e.api.startRecording('삶은계란 냉장',device);
 let storageId=null;
 function heartbeat(change){const at=e.now(),health={ble_connected:true,environment_ok:true,sensor_id:sensor,ota_state:'IDLE',
  environment:{t:3,h:80,p:1010,age_ms:0,captured_at_ms:at}};
  if(Number.isFinite(change)&&storageId)health.ai_shadow={storage_session_id:storageId,sensor_id:sensor,change_index:change,
    shape_change:change,level_change:change,scored_at:at};
  return JSON.parse(e.api.doPost({postData:{contents:JSON.stringify({version:3,device_id:device,sensor_id:sensor,samples:[],health})}}).text);}
 heartbeat();storageId=e.api.startStorageSession('egg container',device).storage_session_id;
 e.setTime(e.now()+300000);heartbeat();
 for(let i=0;i<12;i++){e.setTime(e.now()+300000);heartbeat(i===0?.08:i===11?.12:undefined);}
 const review=e.api.getFoodReview(device,'');
 assert.equal(review.session,recording.session_id);assert(Number.isFinite(review.storage_risk.score));
 assert.equal(review.storage_risk.temperature_score,0);
 const history=e.api.getStorageTelemetry(device).points;
 assert.equal(review.gas_change.index,history[history.length-1].change);
 assert(Math.abs(review.gas_change.delta-.04)<1e-10);
 assert.equal(e.api.getDashboard(device).recording.state,'RECORDING');
});
test('editing actual placement time backfills matching device and food ENV without resetting STO',()=>{
 const e=environment(),a='111111111111',b='222222222222',sensor='aaaabbbbccccdddd',other='1111222233334444',placed=e.now();
 e.api.startRecording('숙주나물 냉장',a);e.api.startRecording('삶은계란 냉장',b);
 const env=(id,seq,at,t)=>{const s={...sample(seq,at,{t}),sample_schema:4};delete s.g;
   s.uid=(id===a?'b':'c').repeat(32)+'-'+seq;
   return e.api.ingest({version:3,device_id:id,sensor_id:id===a?sensor:other,samples:[s]});};
 env(a,1,placed,8);env(b,1,placed,28);
 e.setTime(placed+300000);env(a,2,e.now(),7);env(b,2,e.now(),27);
 const heartbeat=()=>JSON.parse(e.api.doPost({postData:{contents:JSON.stringify({version:3,device_id:a,sensor_id:sensor,samples:[],
   health:{ble_connected:true,sensor_id:sensor,ota_state:'IDLE',environment:{t:3,h:80,p:1010,age_ms:0,captured_at_ms:e.now()}}})}}).text);
 e.setTime(placed+600000);heartbeat();const state=e.api.startStorageSession('시연용 용기',a);
 e.setTime(placed+900000);heartbeat();const active=e.api.getStorageSession(a);
 const corrected=e.api.updateStorageDetails(a,'시연용 용기','생숙주',placed);
 assert.equal(corrected.storage_session_id,state.storage_session_id);
 assert.equal(corrected.started_at,active.started_at);assert.equal(corrected.placed_at,placed);
 const fingerprint=[20,200,400,300,80,70,60,30,40,50];
 for(let scan=1;scan<=4;scan++){
   const frames=Array.from({length:10},(_,step)=>({...v3((scan-1)*10+step+1,step,placed+(scan-1)*300000),
     scan_seq:scan,g:fingerprint[step]*(scan===4?2:1)}));
   assert.equal(e.api.ingest({version:3,device_id:a,sensor_id:sensor,samples:frames}).stored,10);
 }
 const otherFrames=Array.from({length:10},(_,step)=>({...v3(step+1,step,placed),
   uid:'e'.repeat(32)+'-'+(step+1),nrf_boot:77,g:fingerprint[step]*3}));
 assert.equal(e.api.ingest({version:3,device_id:b,sensor_id:other,samples:otherFrames}).stored,10);
 const gasHistory=e.api.getStorageGasBackfill(a);
 assert.equal(gasHistory.complete_scans,4);assert.equal(gasHistory.status,'READY');
 assert.equal(gasHistory.source_rows,40);
 assert(gasHistory.change.index>0);
 heartbeat();
 const review=e.api.getFoodReview(a,'');
 assert.equal(review.food_name,'숙주나물');assert.equal(review.food_source,'보관 설정 음식');
 assert.equal(review.elapsed_ms,900000);assert(review.storage_risk.observed_h>0);
 assert(review.storage_risk.over_4c_h>0);assert.equal(review.storage_risk.official_duration_h,null);
  assert.equal(review.storage_risk.history_source.older_env_rows,2);
  assert.equal(review.storage_risk.history_source.matching_experiments,1);
  assert.equal(review.historical_gas.complete_scans,4);assert.equal(review.gas_history_limited,false);
  assert.equal(review.progress.code,'EARLY');assert.equal(review.progress.basis,'model_a');
 assert.equal(e.api.getDashboard(b).recording.state,'RECORDING');
 assert.equal(e.api.getStorageSession(b).state,'NONE');
 assert.equal(e.all.get('EnvironmentHistory').getLastRow(),5); // original ENV rows preserved
});
test('assumption-based progression has time fallback and does not turn stale contact into a verdict',()=>{
 const e=environment(),now=e.now();
 const base={label:'생숙주',live:true,elapsed_ms:90*3600000,now,
   health:{server_seen_at:now,ble_connected:true,environment:{t:3,age_ms:0}},
   storage_risk:{over_4c_h:0,coverage:1,has_gap:false}};
 const withEnv=e.api.foodProgress_(base);
 assert.equal(withEnv.basis,'model_a');
 assert.equal(withEnv.label_basis.includes('가정'),true);
 const timeOnly=e.api.foodProgress_({...base,storage_risk:{}});
 assert.equal(timeOnly.basis,'model_time');
 const early=e.api.foodProgress_({...base,elapsed_ms:3600000});
 assert.equal(early.code,'EARLY');assert(withEnv.index>early.index);
 assert.equal(e.api.foodProgress_({...base,health:{server_seen_at:now-91000}}).basis,'model_time');
});
test('yellow food state starts near the observed end, not at a refrigerator temperature swing',()=>{
 const e=environment(),p={code:'MIDDLE',index:65,model:'test'};
 const at=hours=>e.api.foodDisplayProgress_(p,{label:'생숙주',elapsed_ms:hours*3600000});
 assert.equal(at(25.6).display_code,'EARLY');
 assert.equal(at(70).display_code,'MIDDLE');
 assert.equal(at(85).display_code,'LATE');
 assert.equal(at(25.6).code,'MIDDLE'); // raw model output is preserved for diagnostics
});
test('ESP progression health is accepted only for matching current food, STO and placement',()=>{
 const e=environment(),now=e.now(),session='STO-'+'a'.repeat(32),sensor='aaaabbbbccccdddd',placed=now-120*3600000;
 const payload={model:'assumed-progress-v1-time',storage_session_id:session,sensor_id:sensor,
   food_code:'sprouts',stage:'LATE',index:100,placed_at:placed,inferred_at:now};
 const health=e.api.healthSnapshot({food_progress:payload},now,0);
 assert.equal(health.food_progress.index,100);
 assert.equal(e.api.healthSnapshot({food_progress:{...payload,index:101}},now,0).food_progress,undefined);
 assert.equal(e.api.healthSnapshot({food_progress:{...payload,inferred_at:now+6000}},now,0).food_progress,undefined);
 const ctx={live:true,label:'생숙주',started_at:placed,elapsed_ms:120*3600000,now,
   storage_session:{state:'ACTIVE',storage_session_id:session,sensor_id:sensor},
   health:{...health,server_seen_at:now,sensor_id:sensor}};
 assert.equal(e.api.foodEdgeProgress_(ctx).basis,'esp32_time');
 assert.equal(e.api.foodEdgeProgress_({...ctx,started_at:placed+1}),null);
 assert.equal(e.api.foodEdgeProgress_({...ctx,label:'밥'}),null);
 assert.equal(e.api.foodProgressUnavailable_({...ctx,health:{server_seen_at:now-91000}}),'CLOUD_CONTACT_STALE');
});
test('missing cloud progression runtime and coefficients have explicit deployment reasons',()=>{
 const isolated={Date,Number,Math,JSON};vm.createContext(isolated);
 vm.runInContext(fs.readFileSync(path.join(__dirname,'../cloud/apps_script/FoodReview.gs'),'utf8'),isolated);
 const input={live:true,label:'생숙주',elapsed_ms:3600000,now:Date.now(),health:{server_seen_at:Date.now()}};
 assert.equal(isolated.foodProgressUnavailable_(input),'PROGRESS_RUNTIME_MISSING');
 assert.equal(isolated.foodFallbackProgress_(input).code,'EARLY');
 assert.equal(isolated.foodFallbackProgress_({...input,elapsed_ms:120*3600000}).code,'LATE');
 vm.runInContext(fs.readFileSync(path.join(__dirname,'../cloud/apps_script/FoodProgress.gs'),'utf8'),isolated);
 assert.equal(isolated.foodProgressUnavailable_(input),'PROGRESS_MODEL_MISSING');
});
test('historic GAS trend uses only complete time-ordered fingerprints',()=>{
 const e=environment(),start=e.now(),gas=[20,200,400,300,80,70,60,30,40,50];
 const scans=Array.from({length:4},(_,i)=>({at:start+i*600000,quality:'OK',gas:i===3?gas.map(x=>x*2):gas}));
 const result=e.api.foodHistoricalChange_(scans.slice().reverse());
 assert.equal(result.complete_scans,4);assert.equal(result.baseline_at,start);
 assert(Math.abs(result.index-.3*Math.log10(2))<1e-9);
 assert.equal(e.api.foodHistoricalChange_(scans.slice(1).reverse()),null);
});
test('automatic food view keeps current STO after EXP stops without reusing stopped GAS rows',()=>{
 const e=environment(),device='111111111111',sensor='aaaabbbbccccdddd';
 e.api.startRecording('숙주나물 냉장',device);
 const heartbeat=()=>{const at=e.now();const payload={version:3,device_id:device,sensor_id:sensor,samples:[],
  health:{ble_connected:true,environment_ok:true,sensor_id:sensor,ota_state:'IDLE',
    ai_shadow:{feature:1,state:'SCORED',reason:'VALID_CHANGE_INDEX',active:true,baseline_scans:3,
      complete_scans:4,invalid_scans:0,queue_drops:0,persistent:true,last_complete_scan_at:at,
      storage_session_id:storageId,sensor_id:sensor,storage_started_at:started},
    environment:{t:3,h:90,p:1010,age_ms:0,captured_at_ms:at}}};
  return JSON.parse(e.api.doPost({postData:{contents:JSON.stringify(payload)}}).text);};
 let storageId=null,started=0;heartbeat();storageId=e.api.startStorageSession('숙주나물 용기',device).storage_session_id;
 e.setTime(e.now()+120000);heartbeat();assert.equal(e.api.getStorageSession(device).state,'ACTIVE');
 started=e.api.getStorageSession(device).started_at;
 e.api.stopRecording(device);e.setTime(e.now()+300000);heartbeat();
 const review=e.api.getFoodReview(device,'');assert.equal(review.session,storageId);
 assert.equal(review.storage_only,true);assert.equal(review.live,true);
 assert.equal(review.food_source,'보관 용기 이름');assert.equal(review.scan,null);
 assert.equal(review.gas_status.code,'RECORDING_PENDING');
 assert.equal(review.storage_risk.official_duration_h,null);
 assert.equal(e.api.getDashboard(device).recording.state,'STOPPED');
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
test('battery health remains optional and USB-powered VDDH is not exposed as battery voltage',()=>{
 const e=environment(),a='111111111111';
 e.api.ingest({version:3,device_id:a,samples:[],health:{battery_mv:4090,battery_percent:89,
  battery_calibration_state:'COLLECTING',battery_calibrated:false,battery_min_mv:3600,battery_max_mv:4150,
  battery_measurement_valid:true,usb_power:false}});
 let h=e.api.getDashboard(a).health;assert.equal(h.battery_mv,4090);
 assert.equal(h.battery_measurement_valid,true);assert.equal(h.usb_power,false);
 assert.equal(h.battery_percent,89);assert.equal(h.battery_calibration_state,'COLLECTING');
 e.api.ingest({version:3,device_id:a,samples:[],health:{battery_mv:5000,
  battery_measurement_valid:false,usb_power:true}});
 h=e.api.getDashboard(a).health;assert.equal(h.usb_power,true);
 assert.equal(h.battery_measurement_valid,false);assert.equal(h.battery_mv,undefined);
 assert.throws(()=>e.api.ingest({version:3,device_id:a,samples:[],health:{battery_mv:6000,
  battery_measurement_valid:true,usb_power:false}}),/battery mV/);
 assert.throws(()=>e.api.ingest({version:3,device_id:a,samples:[],health:{battery_min_mv:4100,
  battery_max_mv:3600,battery_measurement_valid:false,usb_power:true}}),/battery extrema order/);
});
test('battery history is device-scoped, five-minute sampled, and preserves measured versus USB states',()=>{
 const e=environment(),a='111111111111',b='222222222222',base=e.now();
 const health={battery_mv:4090,battery_percent:89,battery_calibration_state:'COLLECTING',
  battery_calibrated:false,battery_min_mv:3600,battery_max_mv:4150,battery_measurement_valid:true,
  usb_power:false,battery_nrf_boot:0x12345678,battery_sample_ms:60000,battery_age_ms:1200};
 e.api.ingest({version:3,device_id:a,sensor_id:'a1b2c3d4e5f60708',samples:[],health});
 const sheet=e.all.get('BatteryHistory');assert.equal(sheet.getLastRow(),2);
 assert.equal(sheet.rows[1][1],a);assert.equal(sheet.rows[1][2],'a1b2c3d4e5f60708');
 assert.equal(sheet.rows[1][3],0x12345678);assert.equal(sheet.rows[1][5],4090);assert.equal(sheet.rows[1][6],89);
 e.setTime(base+60000);e.api.ingest({version:3,device_id:a,samples:[],health});assert.equal(sheet.getLastRow(),2);
 e.cache.delete('BATTERY_HISTORY_DUE_V1:'+a);e.setTime(base+301000);
 e.api.ingest({version:3,device_id:a,samples:[],health:{...health,battery_mv:4010,battery_percent:76,battery_sample_ms:360000}});
 assert.equal(sheet.getLastRow(),3);
 e.api.ingest({version:3,device_id:b,samples:[],health:{battery_measurement_valid:false,usb_power:true,
  battery_nrf_boot:99,battery_sample_ms:10,battery_age_ms:50}});
 assert.equal(sheet.getLastRow(),4);assert.equal(sheet.rows[3][5],'');assert.equal(sheet.rows[3][7],true);
 e.api.ingest({version:3,device_id:'333333333333',samples:[],health:{battery_measurement_valid:true,usb_power:false}});
 assert.equal(sheet.getLastRow(),4);
 const ah=e.api.getBatteryHistory(a,'24h');assert.equal(ah.total,2);assert.equal(ah.points[1].battery_mv,4010);
 assert.equal(e.api.getBatteryHistory(b,'all').total,1);assert.throws(()=>e.api.getBatteryHistory(a,'month'),/Invalid/);
});
test('stale gas age is not data corruption and integrity causes remain explicit',()=>{
 const e=environment(),a='111111111111',base={sensor_mode:'ENV',environment_ok:true,sensor_ok:true,
  ble_connected:true,environment:{t:25,h:50,p:1000,age_ms:100},latest:{t:25,h:50,p:1000,g:113.98},last_valid_age_ms:1652000};
 e.api.ingest({version:3,device_id:a,samples:[],health:base});assert.equal(e.api.getDashboard(a).state,'LIVE / ONLINE');
 e.api.ingest({version:3,device_id:a,samples:[],health:{...base,queue_drops:2}});
 let d=e.api.getDashboard(a);assert.equal(d.state,'DATA LOSS RECORDED');assert.match(d.integrity_reason,/2 rejected/);
 e.api.ingest({version:3,device_id:a,samples:[],health:{...base,spool_full:true}});
 d=e.api.getDashboard(a);assert.equal(d.state,'DATA BUFFER FULL');
 e.api.ingest({version:3,device_id:a,samples:[],health:{...base,spool_error:true}});
 d=e.api.getDashboard(a);assert.equal(d.state,'DATA STORAGE ERROR');
});
test('120-second environment cadence does not become stale while cloud transport stays fresh',()=>{
 const e=environment(),a='111111111111',health={sensor_mode:'ENV',environment_ok:true,sensor_ok:true,
  ble_connected:true,environment:{t:25,h:50,p:1000,age_ms:119000}};
 e.api.ingest({version:3,device_id:a,samples:[],health});
 assert.equal(e.api.getDashboard(a).state,'LIVE / ONLINE');
 e.setTime(e.now()+32000);
 e.api.ingest({version:3,device_id:a,samples:[],health:{...health,environment:undefined}});
 assert.equal(e.api.getDashboard(a).state,'STALE');
 e.setTime(e.now()+16000);assert.equal(e.api.getDashboard(a).state,'STALE / CLOUD CONTACT UNKNOWN');
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
test('monitoring observes committed complete scans without changing ordered ACK or duplicate replay',()=>{
 const e=environment();e.props.set('MONITOR_ENABLED','true');e.api.startRecording('test');
 const rows=Array.from({length:10},(_,i)=>v3(i+1,i,e.now()+i));
 const a=e.api.ingest({version:3,samples:rows.slice(0,5)});
 assert.equal(a.stored,5);
 assert.equal(JSON.parse(e.props.get('MONITOR_STORE_V1:legacy')).lastCompleteCapture,undefined);
 const b=e.api.ingest({version:3,samples:rows.slice(5)});assert.equal(b.stored,5);
 assert.equal(JSON.parse(e.props.get('MONITOR_STORE_V1:legacy')).lastCompleteCapture,e.now()+9);
 assert(e.api.ingest({version:3,samples:rows}).acks.every(x=>x.status==='duplicate'));
});
test('STALE explains measurement versus cloud-contact freshness',()=>{
 const e=environment();e.api.ingest({version:2,samples:[],health:{sensor_mode:'ENV',sensor_ok:true,ble_connected:true,environment:{t:25,h:50,p:1000,age_ms:300000}}});
 assert.equal(e.api.getDashboard().state,'STALE');assert(e.api.getDashboard().progress_reason.includes('환경 측정값'));
 e.setTime(e.now()+20000);assert(e.api.getDashboard().progress_reason.includes('heartbeat'));
});
test('explicit storage intent survives reboot and activates only on later valid ENV',()=>{
 const e=environment(),device='111111111111',sensor='aaaabbbbccccdddd',start=e.now();
 const health=(captured,valid=true)=>({ble_connected:true,environment_ok:valid,sensor_id:sensor,ota_state:'IDLE',
  environment:{t:valid?25:999,h:50,p:1000,age_ms:0,captured_at_ms:captured}});
 const sendHealth=(captured,valid=true)=>JSON.parse(e.api.doPost({postData:{contents:JSON.stringify({version:3,device_id:device,sensor_id:sensor,samples:[],health:health(captured,valid)})}}).text);
 assert.equal(sendHealth(start-1000).success,true);
 const state=e.api.startStorageSession('rice container',device);assert.equal(state.state,'PENDING');
 const noUtc=health(e.now());delete noUtc.environment.captured_at_ms;
 const noUtcAck=JSON.parse(e.api.doPost({postData:{contents:JSON.stringify({version:3,device_id:device,sensor_id:sensor,samples:[],health:noUtc})}}).text);
 assert.equal(noUtcAck.storage_session.reason,'GATEWAY_ENV_UTC_MISSING');
 const oldEnv=sendHealth(start-1000);assert.equal(oldEnv.success,true);
 assert.equal(oldEnv.storage_session.reason,'WAIT_ENV_AFTER_REQUEST');
 assert.equal(e.api.getDashboard(device).storage_session.state,'PENDING');
 e.setTime(e.now()+120000);assert.equal(sendHealth(e.now(),false).storage_session.reason,'WAIT_VALID_ENV');
 assert.equal(e.api.getDashboard(device).storage_session.state,'PENDING');
 const staleStat={...health(e.now()),environment_ok:false};
 const activated=JSON.parse(e.api.doPost({postData:{contents:JSON.stringify({version:3,device_id:device,sensor_id:sensor,samples:[],health:staleStat})}}).text);
 assert.equal(activated.success,true); // accepted ENV, stale STAT must not block start
 assert.equal(activated.storage_session.state,'ACTIVE');
 assert.equal(activated.storage_session.storage_session_id,state.storage_session_id);
 const active=e.api.getDashboard(device).storage_session;assert.equal(active.state,'ACTIVE');
 assert.equal(active.started_at,e.now());assert.equal(active.sensor_id,sensor);
 assert.equal(e.api.getStorageTelemetry(device).points.length,1);
 sendHealth(e.now());assert.equal(e.api.getStorageTelemetry(device).points.length,1);
 e.setTime(e.now()+300000);
 const ai={feature:1,state:'SCORED',reason:'VALID_CHANGE_INDEX',active:true,complete_scans:4,
  storage_session_id:state.storage_session_id,sensor_id:sensor,baseline_scans:3,
  storage_started_at:active.started_at,baseline_first_at:active.started_at+600000,
  baseline_ready_at:active.started_at+1800000,invalid_scans:0,queue_drops:0,
  persistent:true,change_index:0.08,shape_change:0.1,level_change:0.03,scored_at:e.now()};
 const h={...health(e.now()),ai_shadow:ai};
 assert.equal(JSON.parse(e.api.doPost({postData:{contents:JSON.stringify({version:3,device_id:device,sensor_id:sensor,samples:[],health:h})}}).text).success,true);
 assert.equal(e.api.getStorageTelemetry(device).points.length,2);
 assert.equal(e.api.getStorageTelemetry(device).points[1].change,0.08);
 assert.equal(e.api.getDashboard(device).health.ai_shadow.change_index,0.08);
 const key='STORAGE_V1:'+device,stale=JSON.parse(e.props.get(key));stale.last_telemetry_at=0;
 e.props.set(key,JSON.stringify(stale));sendHealth(e.now());
 assert.equal(e.api.getStorageTelemetry(device).points.length,2); // property update lost after row append
 e.cache.clear();assert.equal(e.api.getStorageSession(device).started_at,active.started_at); // gateway/Cloud cache restart
 assert.throws(()=>e.api.startStorageSession('duplicate',device),/Finish or repair/);
 const other='222222222222',otherSensor='1111222233334444';
 const otherHealth={...health(e.now()),sensor_id:otherSensor};
 assert.equal(JSON.parse(e.api.doPost({postData:{contents:JSON.stringify({version:3,device_id:other,sensor_id:otherSensor,samples:[],health:otherHealth})}}).text).success,true);
 const otherSession=e.api.startStorageSession('other container',other);
 assert.notEqual(otherSession.storage_session_id,state.storage_session_id);
 assert.equal(e.api.getStorageSession(device).state,'ACTIVE');
 assert.equal(e.api.getStorageTelemetry(other).points.length,0);
 assert.equal(e.api.endStorageSession(device).state,'ENDED');
 assert.equal(e.all.get('StorageSessions').rows[1][7],'ENDED');
});
test('food and placement produce a provisional stage before the first new GAS scan',()=>{
 const e=environment(),device='111111111111',sensor='aaaabbbbccccdddd',now=e.now();
 const payload={version:3,device_id:device,sensor_id:sensor,samples:[],health:{ble_connected:true,
   sensor_id:sensor,environment:{t:3.5,h:80,p:1010,age_ms:0,captured_at_ms:now}}};
 assert.equal(JSON.parse(e.api.doPost({postData:{contents:JSON.stringify(payload)}}).text).success,true);
 const state=e.api.startStorageSession('냉장 용기',device,'생숙주',now);
 assert.equal(state.state,'PENDING');
 const review=e.api.getFoodReview(device,'');
 assert.equal(review.session,state.storage_session_id);assert.equal(review.live,true);
 assert.equal(review.food_name,'숙주나물');assert.equal(review.elapsed_ms,0);
 assert.equal(review.progress.code,'EARLY');assert.equal(review.progress.basis,'model_time');
 assert.equal(review.scan,null);
});
test('six hours of storage still has a stage when separate cloud model files are absent',()=>{
 const e=environment({noProgress:true}),device='111111111111',sensor='aaaabbbbccccdddd',now=e.now();
 const payload={version:3,device_id:device,sensor_id:sensor,samples:[],health:{ble_connected:true,
   sensor_id:sensor,environment:{t:3.5,h:80,p:1010,age_ms:0,captured_at_ms:now}}};
 e.api.doPost({postData:{contents:JSON.stringify(payload)}});
 e.api.startStorageSession('냉장 용기',device,'생숙주',now-6*3600000);
 const review=e.api.getFoodReview(device,'');
 assert.equal(review.progress.code,'EARLY');
 assert.equal(review.progress.basis,'historical_duration_fallback');
 assert.equal(review.progress_fallback_reason,'PROGRESS_RUNTIME_MISSING');
 assert.equal(review.elapsed_ms,6*3600000);
 e.setTime(now+114*3600000);
 const later=e.api.getFoodReview(device,'');
 assert.equal(later.progress.code,'LATE');
 assert.equal(later.progress.basis,'historical_duration_fallback');
});
test('missing storage module is explicit without changing committed ACK',()=>{
 const e=environment();e.api.storageResponse_=undefined;
 const result=JSON.parse(e.api.doPost({postData:{contents:JSON.stringify({version:3,device_id:'111111111111',samples:[]})}}).text);
 assert.equal(result.success,true);assert.equal(result.storage_session.state,'UNAVAILABLE');
 assert.equal(result.storage_session.reason,'STORAGE_MODULE_MISSING');
});
test('inactive AI reason remains visible in dashboard health without STO session',()=>{
 const e=environment(),device='111111111111';
 const ai={feature:1,state:'INACTIVE',reason:'START_REQUIRED',active:false,baseline_scans:0,
  complete_scans:2,invalid_scans:0,queue_drops:0,persistent:true};
 const ack=JSON.parse(e.api.doPost({postData:{contents:JSON.stringify({version:3,device_id:device,samples:[],health:{ai_shadow:ai}})}}).text);
 assert.equal(ack.success,true);assert.equal(ack.storage_session.state,'NONE');
 const shown=e.api.getDashboard(device).health.ai_shadow;
 assert.equal(shown.state,'INACTIVE');assert.equal(shown.reason,'START_REQUIRED');
 assert.equal(shown.complete_scans,2);
});
test('new food storage intent leaves an existing EXP recording unchanged',()=>{
 const e=environment(),device='111111111111',sensor='aaaabbbbccccdddd';
 const recording=e.api.startRecording('ongoing experiment',device);
 const health={ble_connected:true,environment_ok:true,sensor_id:sensor,ota_state:'IDLE',
  environment:{t:25,h:50,p:1000,age_ms:0,captured_at_ms:e.now()-1000}};
 const send=()=>JSON.parse(e.api.doPost({postData:{contents:JSON.stringify({version:3,device_id:device,sensor_id:sensor,samples:[],health})}}).text);
 send();const storage=e.api.startStorageSession('container',device);
 assert(storage.storage_session_id.startsWith('STO-'));
 assert.equal(e.api.getDashboard(device).recording.session_id,recording.session_id);
 e.setTime(e.now()+120000);health.environment.captured_at_ms=e.now();
 const result=send();assert.equal(result.storage_session.state,'ACTIVE');
 assert.equal(result.recording.session_id,recording.session_id);
 assert.equal(e.api.getDashboard(device).recording.state,'RECORDING');
});
test('food candidate and anomaly diagnostics stay separate from recording ACK and safety score',()=>{
 const e=environment(),id='111111111111',sensor='aaaabbbbccccdddd',at=e.now();
 const health={sensor_id:sensor,ble_connected:true,sensor_mode:'GAS',sensor_tx_errors:4,
   ai_shadow:{feature:1,state:'SCORED',reason:'VALID_CHANGE_INDEX',active:true,baseline_scans:3,
     complete_scans:12,invalid_scans:0,queue_drops:0,partial_scans:2,rejected_fields:1,last_partial_mask:7,persistent:true},
   food_ai:{model:'centroid-20261116',sensor_id:sensor,status:'ABSTAIN_ONE_EXPOSURE',
     candidate:'sprouts',inferred_at:at,distance:2.5,margin:1.2},
   gas_anomaly:{model:'diagonal-loggas-six-baseline',storage_session_id:'STO-'+ 'a'.repeat(32),sensor_id:sensor,
     status:'EXPERIMENTAL_INDEX',baseline_scans:6,index:1.4,inferred_at:at,slope_per_hour:.2}};
 const ack=e.api.ingest({version:3,device_id:id,sensor_id:sensor,samples:[],health});
 assert.equal(ack.success,true);assert.equal(ack.acks.length,0);
 const h=e.api.getDashboard(id).health;
 assert.equal(h.food_ai.candidate,'sprouts');assert.equal(h.food_ai.status,'ABSTAIN_ONE_EXPOSURE');
 assert.equal(h.gas_anomaly.index,1.4);assert.equal(h.gas_anomaly.spoilage_probability,false);
 assert.equal(h.ai_shadow.partial_scans,2);assert.equal(h.sensor_tx_errors,4);
 assert.equal(e.sensor.getLastRow(),2);
});
console.log(`${tests} cloud tests passed (mock Sheets, no external services).`);
