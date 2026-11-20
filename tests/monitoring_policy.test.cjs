const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
let now=Date.parse('2026-11-16T00:00:00Z'),locked=false,sent=[],quota=20,mailFails=false;
class Clock extends Date{constructor(...a){super(...(a.length?a:[now]));}static now(){return now;}}
const props=new Map(),cache=new Map();
const api={Date:Clock,JSON,Number,Math,Array,Object,Error,console,
 validDeviceId:x=>x,cachedObject:(c,k)=>JSON.parse(c.get(k)||'null'),
 CacheService:{getScriptCache:()=>({get:k=>cache.get(k)})},
 PropertiesService:{getScriptProperties:()=>({getProperty:k=>props.get(k),setProperty:(k,v)=>props.set(k,v)})},
 LockService:{getScriptLock:()=>({tryLock(){if(locked)return false;locked=true;return true;},releaseLock(){locked=false;}})},
 Utilities:{formatDate:d=>d.toISOString()},
 MailApp:{getRemainingDailyQuota:()=>quota,sendEmail:e=>{assert(!locked);if(mailFails)throw Error('mail outage');sent.push(e);}}};
vm.createContext(api);
for(const f of ['Monitoring.gs','MonitoringPolicy.gs','StorageRisk.gs'])vm.runInContext(fs.readFileSync('cloud/apps_script/'+f,'utf8'),api);
const fresh=t=>({server_seen_at:t,environment:{t:3,age_ms:0,captured_at_ms:t},last_valid_age_ms:0,
  ble_connected:true,sensor_ok:true,sensor_mode:'ENV',gateway_uptime_ms:1000,last_ack_ms:1000,
  spool_error:false,spool_full:false,queue_drops:0,storage_temp_applicable:true});
let state=null;const step=(h,t)=>{now=t;const r=api.monitorPlanV2_(state,h,t);state=r.state;return r.events;};
const start=now,bad=t=>({...fresh(t),ble_connected:false,environment:{t:3,age_ms:360000,captured_at_ms:t-360000},last_valid_age_ms:3600000});
assert.deepEqual(Array.from(step(bad(start),start)),[]);
let events=step(bad(start+60000),start+60000);
assert.equal(events.length,1);assert.equal(events[0].code,'SENSOR_PATH_STALLED');
let legacy=api.monitorPlan_(null,bad(start),start);
legacy=api.monitorPlan_(legacy.state,bad(start+60000),start+60000);
const legacyInitial=legacy.events.filter(x=>x.status==='ACTIVE').length;
assert(legacyInitial>events.length);
assert.equal(state.incidents.SENSOR_PATH_STALLED.phase,'ACTIVE');
assert.deepEqual(Array.from(step(bad(start+120000),start+120000)),[]);
events=step(bad(start+21660000),start+21660000);assert.equal(events.length,1);assert.equal(events[0].status,'SUMMARY');
events=step(fresh(start+21661000),start+21661000);assert.equal(events.length,0);assert.equal(state.incidents.SENSOR_PATH_STALLED.phase,'RECOVERING');
events=step(bad(start+21720000),start+21720000);assert.equal(events.length,0);assert.equal(state.incidents.SENSOR_PATH_STALLED.phase,'ACTIVE');
step(fresh(start+21780000),start+21780000);
events=step(fresh(start+22080000),start+22080000);assert.equal(events.filter(e=>e.status==='RECOVERED').length,1);
assert.equal(state.incidents.SENSOR_PATH_STALLED.phase,'RECOVERED');
const rebooted=JSON.parse(JSON.stringify(state));state=rebooted;
assert.deepEqual(Array.from(step(fresh(start+22100000),start+22100000)),[]);
 let tempState=null,notifications=[];const t0=start+30000000;
 let mildState=null;for(let i=0;i<=26;i++){const t=t0+i*300000;
   const h={...fresh(t),environment:{t:5.8,age_ms:0,captured_at_ms:t}};
   mildState=api.monitorPlanV2_(mildState,h,t).state;}
 assert.equal(mildState.incidents.TEMP_SUSTAINED,undefined);
 for(let i=0;i<=26;i++){const t=t0+i*300000;now=t;const h={...fresh(t),environment:{t:6.5,age_ms:0,captured_at_ms:t}};
 const r=api.monitorPlanV2_(tempState,h,t);tempState=r.state;notifications.push(...r.events);}
assert.equal(notifications.filter(x=>x.code==='TEMP_SUSTAINED'&&x.status==='ACTIVE').length,1);
assert.equal(notifications.filter(x=>x.code==='TEMP_SUSTAINED'&&x.status==='URGENT').length,1);
const cool={...fresh(t0+27*300000),environment:{t:3,age_ms:0,captured_at_ms:t0+27*300000}};
let r=api.monitorPlanV2_(tempState,cool,t0+27*300000);tempState=r.state;
r=api.monitorPlanV2_(tempState,{...cool,server_seen_at:t0+28*300000},t0+28*300000);
assert.equal(r.events.filter(x=>x.code==='TEMP_SUSTAINED'&&x.status==='RECOVERED').length,1);
const replayPath='data/monitor-replay-20261117.json';
const real=fs.existsSync(replayPath)?JSON.parse(fs.readFileSync(replayPath,'utf8')):[];
const groups=new Map();for(const p of real){if(!groups.has(p.storage))groups.set(p.storage,[]);groups.get(p.storage).push(p);}
let oldWarnings=0,newWarnings=0;
for(const points of groups.values())for(let i=1;i<points.length;i++){
 const batch=points.slice(0,i+1),risk=api.storageRisk_({now:points[i].at,started_at:points[0].at,label:'숙주나물 냉장',points:batch});
 if((risk.over_4c_h||0)>=2||points[i].t>4)oldWarnings++;
 if(risk.temperature_warning)newWarnings++;
}
if(real.length)assert(real.length>100&&newWarnings<=oldWarnings);
props.set('MONITOR_ENABLED','true');props.set('MONITOR_RECIPIENTS','test@example.invalid');props.set('MONITOR_DEVICE_IDS','["device"]');
now=t0;cache.set('LIVE_V2:device',JSON.stringify(bad(now)));
assert.equal(api.monitorTick_().sent,0);let s=JSON.parse(props.get('MONITOR_V1:device'));
for(const x of Object.values(s.incidents))x.first-=60000;
props.set('MONITOR_V1:device',JSON.stringify(s));now+=60000;cache.set('LIVE_V2:device',JSON.stringify(bad(now)));
assert.equal(api.monitorTick_().sent,1);assert.equal(api.monitorTick_().sent,0);
assert.equal(sent.length,1);
let saved=JSON.parse(props.get('MONITOR_V1:device'));
saved.outbox=[{id:'claimed-once',code:'SENSOR_PATH_STALLED',status:'ACTIVE',first:now-60000,at:now,next:0,delivery_state:'CLAIMED'}];
props.set('MONITOR_V1:device',JSON.stringify(saved));api.monitorTick_();assert.equal(sent.length,1);
assert.equal(api.monitorSummaryV2_('device').delivery_uncertain,1);
saved=JSON.parse(props.get('MONITOR_V1:device'));
saved.outbox=[{id:'retry-mail',code:'SENSOR_PATH_STALLED',status:'SUMMARY',first:now-60000,at:now,next:0,delivery_state:'PENDING'}];
props.set('MONITOR_V1:device',JSON.stringify(saved));mailFails=true;api.monitorTick_();assert.equal(sent.length,1);
saved=JSON.parse(props.get('MONITOR_V1:device'));assert.equal(saved.outbox[0].delivery_state,'PENDING');
saved.outbox[0].next=0;props.set('MONITOR_V1:device',JSON.stringify(saved));mailFails=false;
api.monitorTick_();api.monitorTick_();assert.equal(sent.length,2);
console.log('PASS policy: old sensor alerts='+legacyInitial+' new grouped alert=1, 6h summary, 5m recovery, urgent temp, persistent state, one mail');
console.log('REAL ENV replay points='+real.length+' prior-warning-snapshots='+oldWarnings+' sustained-warning-snapshots='+newWarnings+' (no real mail history available)');
