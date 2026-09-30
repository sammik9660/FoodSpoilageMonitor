const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
let tests=0;
function environment(){
 let time=Date.parse('2026-09-28T12:00:00Z'),uuid=0,fail=null,locked=false;
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
  LockService:{getScriptLock:()=>({waitLock(){assert(!locked);locked=true;},releaseLock(){locked=false;}})},
  PropertiesService:{getScriptProperties:()=>({getProperty:k=>props.get(k)})},
  SpreadsheetApp:{openById:id=>{assert.equal(id,'existing-id');return book;},flush(){}},
  Utilities:{getUuid:()=>String(++uuid).padStart(32,'0')},
  CacheService:{getScriptCache:()=>({get:k=>cache.get(k),put:(k,v)=>cache.set(k,v)})},
  ContentService:{MimeType:{JSON:'json',CSV:'csv'},createTextOutput:text=>({text,filename:null,setMimeType(){return this;},downloadAsFile(name){this.filename=name;return this;}})},
  ScriptApp:{getService:()=>({getUrl:()=> 'https://example.invalid/exec'})},
  HtmlService:{createTemplateFromFile:name=>({evaluate(){return {setTitle(){return {name,url:this.webAppUrl};}};}})}
 };
 vm.createContext(api);vm.runInContext(fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Code.gs'),'utf8'),api);
 api.setupReliability();
 return {api,all,sensor,props,setTime:t=>time=t,now:()=>time,setFail:f=>fail=f};
}
const boot='a'.repeat(32);
function sample(seq,time,extra={}){return {uid:boot+'-'+seq,seq,ms:seq*2000,nrf_boot:42,captured_at:time,quality:'ntp',t:25,h:50,p:1000,g:70,...extra};}
function send(e,ss){return e.api.ingest({version:2,samples:ss});}
function test(name,f){f();tests++;console.log('PASS '+name);}
test('migration preserves historical rows and extends headers',()=>{const e=environment();assert.deepEqual(e.sensor.rows[1],['historical',20,40,1000,70]);assert.equal(e.sensor.rows[0].length,12);});
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
 e.props.delete('SPREADSHEET_ID');assert.throws(()=>send(e,[]),/missing/);assert.equal(e.sensor.getLastRow(),2);
});
test('CSV route, session filter and absolute deployed link template',()=>{
 const e=environment();const r=e.api.startRecording('냉장 사과:01/테스트');send(e,[sample(1,e.now())]);const session=e.api.doGet({parameter:{download:'csv',session:r.session_id}});
 assert(session.text.includes('sample_uid'));assert(session.text.includes(boot+'-1'));assert(!session.text.includes('historical'));
 assert.equal(session.filename,'냉장 사과_01_테스트.csv');
 assert.equal(e.api.doGet({parameter:{download:'csv'}}).filename,'food-spoilage-all.csv');
 e.api.stopRecording();const stopped=e.api.getDashboard().recording;
 assert.equal(stopped.state,'STOPPED');assert.equal(stopped.download_session_id,r.session_id);assert.equal(stopped.download_kind,'LATEST');
 const html=fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Dashboard.html'),'utf8');assert(html.includes('<?= webAppUrl ?>?download=csv'));
});
test('dashboard cannot report live solely from successful RPC',()=>{const e=environment();assert(!e.api.getDashboard().state.includes('ONLINE'));e.api.ingest({version:2,samples:[],health:{ble_connected:true,sensor_ok:false,last_valid_age_ms:100}});assert.equal(e.api.getDashboard().state,'SENSOR ERROR');e.setTime(e.now()+31000);assert(e.api.getDashboard().state.startsWith('STALE'));});
test('large CSV requires explicit pages and never silently truncates',()=>{
 const e=environment();for(let i=0;i<10001;i++)e.sensor.rows.push(['row'+i,25,50,1000,70,'uid'+i,'A']);
 assert.throws(()=>e.api.getCsvData(''),/explicit/);
 const page=e.api.getCsvData('',10000,10000);assert(page.includes('row9999'));assert(!page.includes('row9998'));
 assert.throws(()=>e.api.getCsvData('',0,10001),/Invalid CSV/);
});
test('dashboard JavaScript executes and builds absolute paged session links',()=>{
 const e=environment(),elements=new Map();
 function el(id){if(!elements.has(id))elements.set(id,{value:id==='csvOffset'?'0':'test',href:id==='all'?'https://example.invalid/exec?download=csv':'',textContent:'',hidden:false});return elements.get(id);}
 let ok,fail;const runner={withSuccessHandler(f){ok=f;return this;},withFailureHandler(f){fail=f;return this;},getDashboard(){ok(e.api.getDashboard());},startRecording(label){try{ok(e.api.startRecording(label));}catch(x){fail(x);}},stopRecording(){ok(e.api.stopRecording());}};
 const ctx={document:{getElementById:el},google:{script:{run:runner}},setInterval(){},Number,String,JSON,encodeURIComponent};
 vm.createContext(ctx);const html=fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Dashboard.html'),'utf8');
 vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1].replace('<?= refreshMs ?>','10000'),ctx);
 assert(el('all').href.startsWith('https://example.invalid/exec?download=csv&offset=0'));
 ctx.control(true);assert.equal(el('recording').textContent,'RECORDING');assert(el('sessionCsv').href.includes('&session=EXP-'));
 assert(el('sessionCsv').textContent.startsWith('Current Session CSV'));const currentHref=el('sessionCsv').href;
 ctx.control(false);assert.equal(el('recording').textContent,'STOPPED');assert.equal(el('sessionCsv').hidden,false);
 assert(el('sessionCsv').textContent.startsWith('Latest Session CSV'));assert.equal(el('sessionCsv').href,currentHref);
 ctx.control(true);assert.equal(el('recording').textContent,'RECORDING');assert(el('sessionCsv').textContent.startsWith('Current Session CSV'));
 assert.notEqual(el('sessionCsv').href,currentHref);
});
console.log(`${tests} cloud tests passed (mock Sheets, no external services).`);
