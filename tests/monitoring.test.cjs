const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict');
const props=new Map(),cache=new Map();let locked=false,quota=10,fail=false,sent=[];
const ctx={console,Date,JSON,Number,Math,Error,Array,Object,
 validDeviceId:x=>x,cachedObject:(c,k)=>JSON.parse(c.get(k)||'null'),
 CacheService:{getScriptCache:()=>({get:k=>cache.get(k)})},
 PropertiesService:{getScriptProperties:()=>({getProperty:k=>props.get(k),setProperty:(k,v)=>props.set(k,v)})},
 LockService:{getScriptLock:()=>({tryLock:()=>{if(locked)return false;locked=true;return true;},releaseLock:()=>{locked=false;}})},
 Utilities:{formatDate:d=>d.toISOString()},
 MailApp:{getRemainingDailyQuota:()=>quota,sendEmail:e=>{assert(!locked);if(fail)throw Error('injected mail failure');sent.push(e);}}};
vm.createContext(ctx);vm.runInContext(fs.readFileSync('cloud/apps_script/Monitoring.gs','utf8'),ctx);
const t=Date.now(),fresh={server_seen_at:t,environment:{age_ms:1000},last_valid_age_ms:1000,ble_connected:true,sensor_ok:true,ota_state:'IDLE'};
assert.equal(ctx.monitorEvaluate_(fresh,t).length,0);
assert(ctx.monitorEvaluate_({server_seen_at:t,ble_connected:true,sensor_mode:'ENV',gateway_uptime_ms:1800000},t).includes('GAS_NOT_OBSERVED'));
assert(!ctx.monitorEvaluate_({...fresh,sensor_mode:'GAS',environment:{age_ms:400000},mode_age_ms:60000},t).includes('ENV_STALLED'));
assert(ctx.monitorEvaluate_({...fresh,sensor_mode:'GAS',mode_age_ms:240000},t).includes('MEASUREMENT_MODE_STALLED'));
assert.equal(ctx.monitorEvaluate_({...fresh,ota_state:'UPDATING',last_valid_age_ms:9999999},t).length,0);
const stalled={...fresh,environment:{age_ms:5*3600000},last_valid_age_ms:5*3600000};
let p=ctx.monitorPlan_(null,stalled,t);assert.equal(p.events.length,0);
p=ctx.monitorPlan_(p.state,stalled,t+59000);assert.equal(p.events.length,0);
p=ctx.monitorPlan_(p.state,stalled,t+60000);assert.equal(p.events.length,2);
p=ctx.monitorPlan_(p.state,{...stalled,server_seen_at:t+3600000},t+3600000);assert.equal(p.events.length,0); // no ongoing reminders
p=ctx.monitorPlan_(p.state,null,t+3660001);assert(p.events.some(e=>e.code==='HEARTBEAT_STALE'));
assert(p.state.incidents.ENV_STALLED); // lack of contact cannot imply recovery
p=ctx.monitorPlan_(p.state,{...fresh,server_seen_at:t+3661000},t+3661000);
assert.equal(p.events.filter(e=>e.status==='RECOVERED').length,3);
const contact=ctx.monitorPlan_(null,{...fresh,server_seen_at:t},t+60001);
assert(contact.events.some(e=>e.code==='HEARTBEAT_STALE'&&e.status==='ACTIVE'));
const ack=ctx.monitorPlan_(null,{...fresh,gateway_uptime_ms:61000,last_ack_ms:0},t);
assert(ack.events.some(e=>e.code==='CLOUD_ACK_STALLED'&&e.status==='ACTIVE'));
let brief=ctx.monitorPlan_(null,{...fresh,ble_connected:false},t);
brief=ctx.monitorPlan_(brief.state,{...fresh,server_seen_at:t+59000},t+59000);
assert.equal(brief.events.length,0); // cleared before 60 seconds: no alert or recovery email
brief=ctx.monitorPlan_(brief.state,{...fresh,server_seen_at:t+60000,ble_connected:false},t+60000);
assert.equal(brief.events.length,0); // new incident starts its own timer
brief=ctx.monitorPlan_(brief.state,{...fresh,server_seen_at:t+120000,ble_connected:false},t+120000);
assert.equal(brief.events.filter(e=>e.code==='BLE_DISCONNECTED'&&e.status==='ACTIVE').length,1);
brief=ctx.monitorPlan_(brief.state,{...fresh,server_seen_at:t+121000},t+121000);
assert.equal(brief.events.filter(e=>e.code==='BLE_DISCONNECTED'&&e.status==='RECOVERED').length,1);
brief=ctx.monitorPlan_(brief.state,{...fresh,server_seen_at:t+122000,ble_connected:false},t+122000);
assert.equal(brief.events.length,0);
brief=ctx.monitorPlan_(brief.state,{...fresh,server_seen_at:t+181999,ble_connected:false},t+181999);
assert.equal(brief.events.length,0);
brief=ctx.monitorPlan_(brief.state,{...fresh,server_seen_at:t+182000,ble_connected:false},t+182000);
assert.equal(brief.events.filter(e=>e.code==='BLE_DISCONNECTED'&&e.status==='ACTIVE').length,1);
assert.equal(ctx.monitorTick_().enabled,false);assert.equal(sent.length,0);
props.set('MONITOR_ENABLED','true');props.set('MONITOR_RECIPIENTS','test@example.invalid');props.set('MONITOR_DEVICE_IDS','["device"]');
cache.set('LIVE_V2:device',JSON.stringify(stalled));fail=true;ctx.monitorTick_();
assert.equal(sent.length,0);assert.equal(JSON.parse(props.get('MONITOR_V1:device')).outbox.length,0);
let firstTick=JSON.parse(props.get('MONITOR_V1:device'));
Object.values(firstTick.incidents).forEach(i=>i.first-=60000);
props.set('MONITOR_V1:device',JSON.stringify(firstTick));ctx.monitorTick_();
assert.equal(JSON.parse(props.get('MONITOR_V1:device')).outbox.length,2);
fail=false;let s=JSON.parse(props.get('MONITOR_V1:device'));s.outbox.forEach(e=>e.next=0);props.set('MONITOR_V1:device',JSON.stringify(s));
quota=0;ctx.monitorTick_();assert.equal(sent.length,0);
s=JSON.parse(props.get('MONITOR_V1:device'));s.outbox.forEach(e=>e.next=0);props.set('MONITOR_V1:device',JSON.stringify(s));
quota=10;ctx.monitorTick_();assert.equal(sent.length,2);ctx.monitorTick_();assert.equal(sent.length,2);
cache.set('LIVE_V2:device',JSON.stringify({...fresh,server_seen_at:Date.now()}));
ctx.monitorTick_();assert.equal(sent.length,4);assert(sent.slice(2).every(e=>e.subject.includes('RECOVERED')));
ctx.monitorTick_();assert.equal(sent.length,4);
console.log('PASS independent monitoring: five-hour stall, suppression, recovery, missing contact, quota and failed-delivery retry');
const samples=Array.from({length:10},(_,i)=>({sample_schema:3,profile_id:354,nrf_boot:42,scan_seq:1,heater_step:i,seq:i+1,meas_index:i*5,captured_at:t+i}));
const acks=samples.map(()=>({status:'stored'}));
ctx.monitorStoredProgress_('scan-device',samples.slice(0,5),acks.slice(0,5),t+10);
assert.equal(JSON.parse(props.get('MONITOR_STORE_V1:scan-device')).lastCompleteCapture,undefined);
ctx.monitorStoredProgress_('scan-device',samples.slice(5),acks.slice(5),t+20);
assert.equal(JSON.parse(props.get('MONITOR_STORE_V1:scan-device')).lastCompleteCapture,t+9);
ctx.monitorStoredProgress_('stopped',samples,samples.map(()=>({status:'not_recording'})),t+30);
assert.equal(JSON.parse(props.get('MONITOR_STORE_V1:stopped')).lastStoredAt,undefined);
console.log('PASS complete scans span committed batches; STOPPED ACK is not storage');
