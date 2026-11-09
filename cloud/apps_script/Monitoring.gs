// Opt-in independent monitor. No trigger is installed and no email is sent by
// deployment alone. MONITOR_ENABLED must be true and recipients configured.
function monitorEvaluate_(h, now) {
  if(!h||!Number.isFinite(h.server_seen_at))return ['CONTACT_UNKNOWN'];
  const elapsed=Math.max(0,now-h.server_seen_at), alerts=[];
  if(elapsed>60000)return ['HEARTBEAT_STALE']; // offline cannot prove sensor recovery
  const maintenance=['UPDATING','VERIFYING'].includes(h.ota_state);
  if(h.spool_error)alerts.push('SPOOL_ERROR');
  if(h.spool_full)alerts.push('SPOOL_FULL');
  if(h.queue_drops>0)alerts.push('DATA_LOSS_RECORDED');
  if(h.spool_capacity_bytes>0&&h.spool_used_bytes/h.spool_capacity_bytes>=0.85)alerts.push('SPOOL_CAPACITY');
  if(maintenance)return alerts;
  if(h.ble_connected===false)alerts.push('BLE_DISCONNECTED');
  if(h.environment&&h.environment.age_ms+elapsed>300000&& !['GAS','COOL'].includes(h.sensor_mode))alerts.push('ENV_STALLED');
  if(!h.environment&&h.ble_connected===true&&h.sensor_mode==='ENV'&&h.gateway_uptime_ms>300000)alerts.push('ENV_NOT_OBSERVED');
  if(Number.isFinite(h.last_valid_age_ms)&&h.last_valid_age_ms+elapsed>1200000)alerts.push('GAS_STALLED');
  if(!Number.isFinite(h.last_valid_age_ms)&&h.ble_connected===true&&h.sensor_mode&&h.sensor_mode!=='LEGACY'&&h.gateway_uptime_ms>1200000)alerts.push('GAS_NOT_OBSERVED');
  if(h.recording_active===true&&Number.isFinite(h.recording_started_at)&&
     now-Math.max(h.recording_started_at,h.last_complete_scan_at||0)>1200000)alerts.push('COMPLETE_SCAN_STALLED');
  if(h.sensor_mode==='REC'&&h.mode_age_ms>180000)alerts.push('SENSOR_RECOVERY');
  if(['GAS','COOL'].includes(h.sensor_mode)&&h.mode_age_ms>180000)alerts.push('MEASUREMENT_MODE_STALLED');
  if(h.backlog_stalled===true)alerts.push('BACKLOG_STALLED');
  if(Number.isFinite(h.gateway_uptime_ms)&&Number.isFinite(h.last_ack_ms)&&h.gateway_uptime_ms-h.last_ack_ms>60000)
    alerts.push('CLOUD_ACK_STALLED');
  if(h.usb_power===false&&h.battery_measurement_valid&&Number.isFinite(h.battery_percent)&&h.battery_percent<=5)alerts.push('BATTERY_ESTIMATE_LOW');
  return alerts;
}

// Pure state transition: alert after 60 continuous seconds, then recovery. A missing
// heartbeat does not mark prior sensor/storage incidents recovered.
function monitorPlan_(previous, h, now) {
  const state=previous||{incidents:{},history:[]};
  state.incidents=state.incidents||{};state.history=state.history||[];
  if(h&&(!state.health||h.server_seen_at>=state.health.server_seen_at)) {
    const prev=state.health, reboot=prev&&h.gateway_uptime_ms<prev.gateway_uptime_ms;
    if(!prev||reboot||prev.sensor_mode!==h.sensor_mode)state.modeSince=now;
    const depth=(h.spool_depth||0)+(h.queue_depth||0),previousDepth=prev?(prev.spool_depth||0)+(prev.queue_depth||0):0;
    if(!prev||reboot||!depth||depth<previousDepth||h.last_ack_ms!==prev.last_ack_ms)state.backlogSince=now;
    h.mode_age_ms=now-(state.modeSince||now);
    h.backlog_stalled=depth>0&&now-(state.backlogSince||now)>300000;
    state.health=h;
  }
  const codes=monitorEvaluate_(state.health,now), unknown=codes.includes('HEARTBEAT_STALE')||codes.includes('CONTACT_UNKNOWN');
  const events=[];
  codes.forEach(code=>{
    let incident=state.incidents[code];
    if(!incident){
      const first=code==='HEARTBEAT_STALE'?state.health.server_seen_at:
        code==='CLOUD_ACK_STALLED'?now-(state.health.gateway_uptime_ms-state.health.last_ack_ms):now;
      incident=state.incidents[code]={first,alerted:false};
    }
    // Existing deployed state used `next` after sending an alert.
    if(incident.alerted===undefined)incident.alerted=!!incident.next;
    if(!incident.alerted&&now-incident.first>=60000){events.push({code,status:'ACTIVE',first:incident.first});incident.alerted=true;}
  });
  Object.keys(state.incidents).forEach(code=>{
    const live=state.health||{};
    const known={ENV_STALLED:!!live.environment,GAS_STALLED:Number.isFinite(live.last_valid_age_ms),
      BLE_DISCONNECTED:typeof live.ble_connected==='boolean',SPOOL_ERROR:typeof live.spool_error==='boolean',
      SPOOL_FULL:typeof live.spool_full==='boolean',DATA_LOSS_RECORDED:Number.isFinite(live.queue_drops),
      SPOOL_CAPACITY:live.spool_capacity_bytes>0,BATTERY_ESTIMATE_LOW:live.usb_power===false&&live.battery_measurement_valid&&Number.isFinite(live.battery_percent),
      SENSOR_RECOVERY:typeof live.sensor_ok==='boolean',MEASUREMENT_MODE_STALLED:typeof live.sensor_mode==='string',
      BACKLOG_STALLED:typeof live.backlog_stalled==='boolean',CLOUD_ACK_STALLED:Number.isFinite(live.last_ack_ms)};
    known.ENV_NOT_OBSERVED=!!live.environment;known.GAS_NOT_OBSERVED=Number.isFinite(live.last_valid_age_ms);
    known.COMPLETE_SCAN_STALLED=live.recording_active===true&&Number.isFinite(live.last_complete_scan_at);
    const maintenance=['UPDATING','VERIFYING'].includes(live.ota_state);
    if(!codes.includes(code)&&known[code]!==false&&!maintenance&&(!unknown||['CONTACT_UNKNOWN','HEARTBEAT_STALE'].includes(code))){
      if(state.incidents[code].alerted)events.push({code,status:'RECOVERED',first:state.incidents[code].first});
      delete state.incidents[code];
    }
  });
  events.forEach(e=>state.history.push({at:now,code:e.code,status:e.status}));
  state.history=state.history.slice(-20);
  return {state,events};
}

function monitorTick_() {
  const props=PropertiesService.getScriptProperties();
  if(props.getProperty('MONITOR_ENABLED')!=='true')return {enabled:false};
  const recipients=props.getProperty('MONITOR_RECIPIENTS');
  const ids=JSON.parse(props.getProperty('MONITOR_DEVICE_IDS')||'[]');
  if(!recipients||!Array.isArray(ids)||ids.length>20)throw Error('Configure monitoring recipients and <=20 device IDs');
  const now=Date.now(), deliveries=[];
  const lock=LockService.getScriptLock();
  if(!lock.tryLock(0))return {busy:true};
  try {
    ids.forEach(raw=>{
      const id=validDeviceId(raw),key='MONITOR_V1:'+id;
      const old=JSON.parse(props.getProperty(key)||'null');
      const h=cachedObject(CacheService.getScriptCache(),'LIVE_V2:'+id);
      if(h){
        const stored=JSON.parse(props.getProperty('MONITOR_STORE_V1:'+id)||'{}');
        const recording=cachedObject(CacheService.getScriptCache(),'RECORDING_V1:'+id);
        h.last_complete_scan_at=stored.lastCompleteCapture;
        h.last_data_stored_at=stored.lastStoredAt;
        h.recording_active=recording?recording.state==='RECORDING':null;
        h.recording_started_at=recording&&recording.started_at;
        h.experiment_label=recording&&recording.experiment_label;
      }
      const plan=monitorPlan_(old,h,now),state=plan.state;
      state.outbox=state.outbox||[];
      plan.events.forEach(e=>state.outbox.push({...e,id:now+':'+e.code+':'+e.status,at:now,next:now}));
      // Bound outage reminders without losing the latest recovery transition.
      if(state.outbox.length>10){state.outboxOverflow=(state.outboxOverflow||0)+state.outbox.length-10;state.outbox=state.outbox.slice(-10);}
      state.outbox.forEach(e=>{if(e.next<=now){e.next=now+300000;deliveries.push({device:id,event:e,health:state.health});}});
      props.setProperty(key,JSON.stringify(state));
    });
  } finally {lock.releaseLock();}
  // Never hold the ingestion/recording lock during MailApp network work.
  let sent=0;
  for(const item of deliveries) {
    try {
      if(MailApp.getRemainingDailyQuota()<recipients.split(',').length)break;
      const e=item.event,h=item.health||{};
      const kst=t=>Utilities.formatDate(new Date(t),'Asia/Seoul','yyyy-MM-dd HH:mm:ss');
      MailApp.sendEmail({to:recipients,
        subject:'[FoodSpoilageMonitor]['+e.status+'] '+e.code+' — '+item.device,
        body:'Device: '+item.device+'\nExperiment: '+(h.experiment_label||'unknown')+'\nFirst observed (KST): '+kst(e.first)+
          '\nObserved (KST): '+kst(e.at)+'\nLast cloud contact: '+(h.server_seen_at?kst(h.server_seen_at):'unknown')+
          '\nDuration seconds: '+Math.floor((e.at-e.first)/1000)+
          '\nBLE: '+h.ble_connected+'; mode: '+h.sensor_mode+'; spool: '+h.spool_depth+
          '\nENV age ms: '+(h.environment?h.environment.age_ms:'unknown')+'; GAS age ms: '+h.last_valid_age_ms+
          '\nLast complete scan: '+(h.last_complete_scan_at?kst(h.last_complete_scan_at):'unknown')+
          '\nLast stored data: '+(h.last_data_stored_at?kst(h.last_data_stored_at):'unknown')+
          '\nSpool error: '+h.spool_error+'; queue drops: '+h.queue_drops+
          '\nCheck power, sensor progress and pending data. Do not erase storage.\nRecovered means current observed condition cleared; missing samples remain missing.'});
      if(lock.tryLock(0))try {
        const key='MONITOR_V1:'+item.device,state=JSON.parse(props.getProperty(key));
        state.outbox=state.outbox.filter(x=>x.id!==e.id);props.setProperty(key,JSON.stringify(state));
      } finally {lock.releaseLock();}
      ++sent;
    } catch(error){console.error('Monitor delivery failed; retained for bounded retry: '+String(error));}
  }
  return {sent};
}

// Run manually ONLY after deployment/trigger approval. Re-running is idempotent.
function installMonitorTrigger_() {
  if(PropertiesService.getScriptProperties().getProperty('MONITOR_ENABLED')!=='true')throw Error('Monitoring must be explicitly enabled');
  if(!ScriptApp.getProjectTriggers().some(t=>t.getHandlerFunction()==='monitorTick_'))
    ScriptApp.newTrigger('monitorTick_').timeBased().everyMinutes(1).create();
}

// Called only after the existing journal commit, under the existing ingest
// lock. Optional monitoring failure must never change the already valid ACK.
function monitorStoredProgress_(device,samples,acks,now) {
  const props=PropertiesService.getScriptProperties();
  if(props.getProperty('MONITOR_ENABLED')!=='true'||!samples.length)return;
  const key='MONITOR_STORE_V1:'+device,state=JSON.parse(props.getProperty(key)||'{"scans":{}}');
  samples.forEach((s,i)=>{
    if(!acks[i]||!['stored','unassigned_time'].includes(acks[i].status))return;
    state.lastStoredAt=now;
    if(s.sample_schema!==3||s.profile_id!==354||s.captured_at===null)return;
    const identity=[s.nrf_boot,s.scan_seq,s.profile_id].join(':');
    const scan=state.scans[identity]||(state.scans[identity]={steps:{},bad:false});
    const item=[s.seq,s.meas_index,s.captured_at];
    if(scan.steps[s.heater_step]&&JSON.stringify(scan.steps[s.heater_step])!==JSON.stringify(item))scan.bad=true;
    scan.steps[s.heater_step]=item;
    if(!scan.bad&&Object.keys(scan.steps).length===10){
      for(let n=0;n<10;++n){
        if(!scan.steps[n]){scan.bad=true;break;}
        if(n){const a=scan.steps[n-1],b=scan.steps[n],delta=(b[1]-a[1]+256)%256;
          if(b[0]!==a[0]+1||delta<1||delta>127||b[2]<a[2])scan.bad=true;}
      }
      if(!scan.bad)state.lastCompleteCapture=Math.max(state.lastCompleteCapture||0,scan.steps[9][2]);
    }
    const keys=Object.keys(state.scans);while(keys.length>4)delete state.scans[keys.shift()];
  });
  props.setProperty(key,JSON.stringify(state));
}
