// Pure monitoring policy. Real delivery is opt-in through Monitoring.gs.
const MONITOR_ONSET_MS_=60000,MONITOR_RECOVERY_MS_=300000,MONITOR_SUMMARY_MS_=21600000;
const MONITOR_SENSOR_CODES_=['BLE_DISCONNECTED','ENV_STALLED','ENV_NOT_OBSERVED','GAS_STALLED',
  'GAS_NOT_OBSERVED','COMPLETE_SCAN_STALLED','SENSOR_RECOVERY','MEASUREMENT_MODE_STALLED'];
function monitorEvaluateV2_(h,now){
  const raw=monitorEvaluate_(h,now),related=raw.filter(x=>MONITOR_SENSOR_CODES_.includes(x));
  const codes=raw.filter(x=>!MONITOR_SENSOR_CODES_.includes(x));
  if(related.length)codes.push('SENSOR_PATH_STALLED');
  if(h&&h.temperature_sustained&&raw[0]!=='HEARTBEAT_STALE'&&
     !['UPDATING','VERIFYING'].includes(h.ota_state))codes.push('TEMP_SUSTAINED');
  return {codes,related};
}
function monitorTemperatureV2_(state,h,now){
  const activeAlertC=4+(typeof STORAGE_ALERT_MARGIN_C_==='number'?STORAGE_ALERT_MARGIN_C_:2);
  const env=h.environment,at=env&&Number.isFinite(env.captured_at_ms)?env.captured_at_ms:
    env&&Number.isFinite(env.age_ms)?Math.round((h.server_seen_at-env.age_ms)/1000)*1000:null;
  const tracker=state.temperature||{first:null,last:null,count:0};
  if(h.storage_temp_applicable&&Number.isFinite(at)&&Number.isFinite(env.t)&&at<=now+5000&&at>0&&
     (!tracker.last||at>tracker.last)){
    if(env.t>activeAlertC){
      if(!tracker.last||at-tracker.last>600000||tracker.lastWasCool){tracker.first=at;tracker.count=0;}
      tracker.count++;tracker.lastWasCool=false;
    }else{tracker.first=null;tracker.count=0;tracker.lastWasCool=true;}
    tracker.last=at;
  }
  state.temperature=tracker;
  h.temperature_sustained=!!h.storage_temp_applicable&&tracker.count>=3&&
    tracker.first!==null&&tracker.last-tracker.first>=1800000&&now-tracker.last<=600000;
  h.temperature_exposure_urgent=h.temperature_sustained&&tracker.last-tracker.first>=7200000;
  return tracker;
}
function monitorConditionKnownV2_(code,h){
  if(!h)return false;
  if(code==='SENSOR_PATH_STALLED')return h.ble_connected===true&&
    Number.isFinite(h.last_valid_age_ms)&&h.environment&&Number.isFinite(h.environment.age_ms);
  if(code==='TEMP_SUSTAINED')return h.storage_temp_applicable===true&&h.environment&&
    Number.isFinite(h.environment.t)&&Number.isFinite(h.environment.age_ms)&&h.environment.age_ms<=600000;
  if(code==='SPOOL_ERROR')return typeof h.spool_error==='boolean';
  if(code==='SPOOL_FULL')return typeof h.spool_full==='boolean';
  if(code==='DATA_LOSS_RECORDED')return Number.isFinite(h.queue_drops);
  if(code==='CLOUD_ACK_STALLED')return Number.isFinite(h.last_ack_ms);
  return true;
}
function monitorPlanV2_(previous,h,now){
  const state=previous||{incidents:{},history:[]};state.incidents=state.incidents||{};
  state.history=state.history||[];state.recent=state.recent||{};
  const legacySensor=Object.entries(state.incidents).filter(([code])=>MONITOR_SENSOR_CODES_.includes(code));
  if(legacySensor.length){
    const old=state.incidents.SENSOR_PATH_STALLED;
    state.incidents.SENSOR_PATH_STALLED=old||{phase:legacySensor.some(([,x])=>x.alerted)?'ACTIVE':'SUSPECTED',
      first:Math.min(...legacySensor.map(([,x])=>x.first||now)),lastNoticeAt:now,urgentNotified:false,
      related:legacySensor.map(([code])=>code)};
    legacySensor.forEach(([code])=>delete state.incidents[code]);
  }
  for(const item of Object.values(state.incidents))if(!item.phase){
    item.phase=item.alerted?'ACTIVE':'SUSPECTED';item.lastNoticeAt=item.alerted?now:0;
  }
  if(h&&(!state.health||h.server_seen_at>=state.health.server_seen_at)){
    const old=state.health,reboot=old&&h.gateway_uptime_ms<old.gateway_uptime_ms;
    if(!old||reboot||old.sensor_mode!==h.sensor_mode)state.modeSince=now;
    const depth=(h.spool_depth||0)+(h.queue_depth||0),prior=old?(old.spool_depth||0)+(old.queue_depth||0):0;
    if(!old||reboot||!depth||depth<prior||h.last_ack_ms!==old.last_ack_ms)state.backlogSince=now;
    h.mode_age_ms=now-(state.modeSince||now);h.backlog_stalled=depth>0&&now-(state.backlogSince||now)>300000;
    monitorTemperatureV2_(state,h,now);state.health=h;
  }
  const evaluation=monitorEvaluateV2_(state.health,now),codes=evaluation.codes;
  const unknown=codes.includes('HEARTBEAT_STALE')||codes.includes('CONTACT_UNKNOWN');
  const events=[];
  for(const code of codes){
    let incident=state.incidents[code];
    if(!incident||incident.phase==='RECOVERED'){
      const first=code==='HEARTBEAT_STALE'&&state.health?state.health.server_seen_at:
        code==='CLOUD_ACK_STALLED'&&state.health?now-(state.health.gateway_uptime_ms-state.health.last_ack_ms):now;
      incident=state.incidents[code]={phase:'SUSPECTED',first,lastNoticeAt:0,urgentNotified:false};
    }
    if(code==='SENSOR_PATH_STALLED')incident.related=evaluation.related;
    if(incident.phase==='RECOVERING')incident.phase='ACTIVE';
    if(incident.phase==='SUSPECTED'&&(now-incident.first>=MONITOR_ONSET_MS_||
       ['SPOOL_ERROR','DATA_LOSS_RECORDED'].includes(code))){
      incident.phase='ACTIVE';incident.lastNoticeAt=now;events.push({code,status:'ACTIVE',first:incident.first});
    }else if(incident.phase==='ACTIVE'&&now-incident.lastNoticeAt>=MONITOR_SUMMARY_MS_){
      incident.lastNoticeAt=now;events.push({code,status:'SUMMARY',first:incident.first});
    }
    if(code==='TEMP_SUSTAINED'&&incident.phase==='ACTIVE'&&state.health.temperature_exposure_urgent&&!incident.urgentNotified){
      incident.urgentNotified=true;incident.lastNoticeAt=now;events.push({code,status:'URGENT',first:incident.first});
    }
  }
  for(const [code,incident] of Object.entries(state.incidents)){
    if(codes.includes(code)||incident.phase==='RECOVERED')continue;
    // Unknown cloud contact never proves that a sensor or storage incident recovered.
    if(unknown&&!['HEARTBEAT_STALE','CONTACT_UNKNOWN'].includes(code))continue;
    if(!monitorConditionKnownV2_(code,state.health))continue;
    if(incident.phase==='SUSPECTED'){delete state.incidents[code];continue;}
    if(incident.phase==='ACTIVE'){incident.phase='RECOVERING';incident.recoverySince=now;}
    else if(incident.phase==='RECOVERING'&&now-incident.recoverySince>=MONITOR_RECOVERY_MS_){
      incident.phase='RECOVERED';incident.recoveredAt=now;
      state.recent[code]={first:incident.first,recoveredAt:now,lastNoticeAt:incident.lastNoticeAt};
      events.push({code,status:'RECOVERED',first:incident.first});
    }
  }
  events.forEach(e=>state.history.push({at:now,code:e.code,status:e.status}));
  state.history=state.history.slice(-20);
  return {state,events};
}
function monitorSummaryV2_(device){
  const raw=PropertiesService.getScriptProperties().getProperty('MONITOR_V1:'+device);
  if(!raw)return {state:'NORMAL',first_at:null,last_notice_at:null,recovered_at:null};
  try{const s=JSON.parse(raw),uncertain=(s.outbox||[]).filter(x=>x.delivery_state==='CLAIMED').length,
    active=Object.entries(s.incidents||{}).filter(([,v])=>v.phase&&v.phase!=='RECOVERED');
    const item=active.find(([code])=>code==='SENSOR_PATH_STALLED')||active[0];
    if(item)return {state:item[1].phase,code:item[0],first_at:item[1].first,
      last_notice_at:item[1].lastNoticeAt||null,recovered_at:null,related:item[1].related||[],delivery_uncertain:uncertain};
    const recent=Object.entries(s.recent||{}).sort((a,b)=>b[1].recoveredAt-a[1].recoveredAt)[0];
    return recent?{state:'RECOVERED',code:recent[0],first_at:recent[1].first,
      last_notice_at:recent[1].lastNoticeAt,recovered_at:recent[1].recoveredAt,delivery_uncertain:uncertain}:
      {state:'NORMAL',first_at:null,last_notice_at:null,recovered_at:null,delivery_uncertain:uncertain};
  }catch(_){return {state:'UNKNOWN',first_at:null,last_notice_at:null,recovered_at:null};}
}
