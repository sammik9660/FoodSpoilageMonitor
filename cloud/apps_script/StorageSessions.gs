// Separate container intent from experiment recording. No historical rows change.
const STORAGE_HEADERS_=['storage_session_id','device_id','sensor_id','container_name',
  'requested_at','started_at','ended_at','state','start_source'];
const STORAGE_TELEMETRY_HEADERS_=['storage_session_id','device_id','sensor_id','captured_at',
  'temperature_c','humidity_rh','pressure_hpa','change_index','shape_change','level_change','scored_at'];
function storageKey_(device){return 'STORAGE_V1:'+device;}
function storageFoodCode_(label){
  const text=String(label||'');
  for(const [word,code] of [['바나나','banana'],['닭','chicken'],['계란','egg'],['달걀','egg'],
    ['밥','rice'],['숙주','sprouts'],['두부','tofu']])if(text.includes(word))return code;
  return null;
}
function storageCurrent_(device){
  const raw=PropertiesService.getScriptProperties().getProperty(storageKey_(device));
  if(!raw)return {device_id:device,state:'NONE'};
  try{return JSON.parse(raw);}catch(_){return {device_id:device,state:'UNKNOWN'};}
}
function storageSheet_(ctx){return auxiliary(ctx.b,'StorageSessions',STORAGE_HEADERS_);}
function getStorageSession(device){return storageCurrent_(validDeviceId(device));}
function storageDetails_(name,foodLabel,placedAt,now){
  if(typeof name!=='string'||!name.trim()||name.trim().length>80)throw Error('Container name must be 1..80 characters');
  const food=typeof foodLabel==='string'&&foodLabel.trim()?foodLabel.trim():null;
  if(food&&(food.length>80||/^[=+@\-]/.test(food)))throw Error('Invalid food label');
  const placed=placedAt===undefined||placedAt===null||placedAt===''?now:Number(placedAt);
  if(!Number.isSafeInteger(placed)||placed<1577836800000||placed>now)throw Error('Invalid food placement time');
  return {container_name:name.trim(),food_label:food,placed_at:placed};
}
function updateStorageDetails(device,name,foodLabel,placedAt){
  device=validDeviceId(device);
  return locked(()=>{
    const state=storageCurrent_(device),now=Date.now();
    if(state.state!=='ACTIVE'&&state.state!=='PENDING')throw Error('No current storage session to update');
    const details=storageDetails_(name,foodLabel,placedAt,now);
    const ctx=sheets(),sheet=storageSheet_(ctx);
    sheet.getRange(state.row,4).setValue(details.container_name);
    SpreadsheetApp.flush();Object.assign(state,details);
    PropertiesService.getScriptProperties().setProperty(storageKey_(device),JSON.stringify(state));
    CacheService.getScriptCache().remove('FOOD_REVIEW_V1:'+device+':current');
    return state;
  });
}
function storageEnvValuesValid_(env){
  try{number(env.t,-40,85,'storage T');number(env.h,0,100,'storage H');
    number(env.p,300,1100,'storage P');return true;}catch(_){return false;}
}
function storageResponse_(device,payload,now){
  const state=storageCurrent_(validDeviceId(device));
  if(state.state!=='PENDING')return {...state,food_code:storageFoodCode_(state.food_label),reason:state.state==='NONE'?'START_REQUIRED':
    state.state==='ENDED'?'STORAGE_ENDED':state.state==='ACTIVE'?'ACTIVE':'STORAGE_STATE_UNKNOWN'};
  const h=(payload&&payload.health)||{},env=h.environment||{};
  const captured=Number(env.captured_at_ms);
  const reason=h.ble_connected!==true?'WAIT_BLE':
    (payload&&payload.sensor_id)!==state.sensor_id?'SENSOR_ID_MISMATCH':
    !storageEnvValuesValid_(env)?'WAIT_VALID_ENV':
    !Number.isSafeInteger(captured)?'GATEWAY_ENV_UTC_MISSING':
    captured<=state.requested_at?'WAIT_ENV_AFTER_REQUEST':
    captured>now+5000?'INVALID_ENV_TIME':'WAIT_CLOUD_LOCK_OR_WRITE';
  return {...state,food_code:storageFoodCode_(state.food_label),reason};
}
function startStorageSession(name,device,foodLabel,placedAt){
  device=validDeviceId(device);
  if(device==='legacy')throw Error('A device ID is required');
  return locked(()=>{
    const details=storageDetails_(name,foodLabel,placedAt,Date.now());
    const old=storageCurrent_(device);
    if(old.state==='PENDING'||old.state==='ACTIVE'||old.state==='UNKNOWN')throw Error('Finish or repair the existing storage session first');
    if(typeof remoteReservationActive_==='function'&&remoteReservationActive_(device))throw Error('Firmware update is reserved');
    const health=cachedObject(CacheService.getScriptCache(),'LIVE_V2:'+device)||{};
    const sensor=String(health.sensor_id||'');
    if(!/^[a-f0-9]{16}$/.test(sensor)||health.ble_connected!==true||
       !Number.isFinite(health.server_seen_at)||Date.now()-health.server_seen_at>90000)
      throw Error('Wait for a recent connected sensor heartbeat');
    const ctx=sheets(),sheet=storageSheet_(ctx),row=sheet.getLastRow()+1;
    ensureRoom(ctx,sheet,row);
    const state={device_id:device,storage_session_id:'STO-'+Utilities.getUuid(),sensor_id:sensor,
      ...details,state:'PENDING',requested_at:Date.now(),started_at:null,ended_at:null,row};
    sheet.getRange(row,1,1,STORAGE_HEADERS_.length).setValues([[
      state.storage_session_id,device,sensor,state.container_name,new Date(state.requested_at).toISOString(),'','','PENDING','']]);
    SpreadsheetApp.flush();PropertiesService.getScriptProperties().setProperty(storageKey_(device),JSON.stringify(state));
    CacheService.getScriptCache().remove('FOOD_REVIEW_V1:'+device+':current');
    return state;
  });
}
function endStorageSession(device){
  device=validDeviceId(device);
  return locked(()=>{
    const state=storageCurrent_(device);
    if(!['PENDING','ACTIVE'].includes(state.state))throw Error('No active storage session');
    const ctx=sheets(),sheet=storageSheet_(ctx),now=Date.now();
    sheet.getRange(state.row,7,1,2).setValues([[new Date(now).toISOString(),'ENDED']]);
    SpreadsheetApp.flush();state.state='ENDED';state.ended_at=now;
    PropertiesService.getScriptProperties().setProperty(storageKey_(device),JSON.stringify(state));
    return state;
  });
}
function observeStorageSession_(payload,now){
  const device=validDeviceId(payload.device_id),state=storageCurrent_(device);
  if(state.state!=='PENDING')return;
  const h=payload.health||{},env=h.environment||{};
  if(h.ble_connected!==true||!storageEnvValuesValid_(env)||h.ota_state==='UPDATING'||h.ota_state==='VERIFYING'||
     payload.sensor_id!==state.sensor_id)return;
  const captured=Number(env.captured_at_ms);
  if(!Number.isSafeInteger(captured)||captured<=state.requested_at||captured>now+5000)return;
  const attempt=tryLocked(()=>{
    const fresh=storageCurrent_(device);
    if(fresh.state!=='PENDING'||fresh.storage_session_id!==state.storage_session_id)return;
    const ctx=sheets(),sheet=storageSheet_(ctx);
    sheet.getRange(fresh.row,6).setValue(new Date(captured).toISOString());
    sheet.getRange(fresh.row,8,1,2).setValues([['ACTIVE','gateway_receive_utc']]);
    SpreadsheetApp.flush();fresh.state='ACTIVE';fresh.started_at=captured;fresh.start_source='gateway_receive_utc';
    PropertiesService.getScriptProperties().setProperty(storageKey_(device),JSON.stringify(fresh));
  });
  return attempt.acquired;
}
function observeStorageTelemetry_(payload,now){
  const device=validDeviceId(payload.device_id),state=storageCurrent_(device);
  if(state.state!=='ACTIVE'||payload.sensor_id!==state.sensor_id)return;
  const h=payload.health||{},env=h.environment||{},ai=h.ai_shadow||{};
  const at=Number(env.captured_at_ms);
  if(h.ble_connected!==true||!storageEnvValuesValid_(env)||!Number.isSafeInteger(at)||
     at<state.started_at||at>now+5000||at-(state.last_telemetry_at||0)<300000)return;
  const t=number(env.t,-40,85,'storage T'),rh=number(env.h,0,100,'storage H'),
    p=number(env.p,300,1100,'storage P');
  return tryLocked(()=>{
    const fresh=storageCurrent_(device);
    if(fresh.state!=='ACTIVE'||fresh.storage_session_id!==state.storage_session_id||
       at-(fresh.last_telemetry_at||0)<300000)return;
    const ctx=sheets(),sheet=auxiliary(ctx.b,'StorageTelemetry',STORAGE_TELEMETRY_HEADERS_);
    const capturedIso=new Date(at).toISOString();
    const tail=Math.min(64,Math.max(0,sheet.getLastRow()-1));
    if(tail&&sheet.getRange(sheet.getLastRow()-tail+1,1,tail,4).getValues()
      .some(r=>r[0]===fresh.storage_session_id&&r[1]===device&&
       r[2]===fresh.sensor_id&&String(r[3])===capturedIso)){
      fresh.last_telemetry_at=at;
      PropertiesService.getScriptProperties().setProperty(storageKey_(device),JSON.stringify(fresh));
      return;
    }
    const matched=ai.storage_session_id===fresh.storage_session_id&&ai.sensor_id===fresh.sensor_id&&
      Number.isFinite(ai.change_index)&&Number.isFinite(ai.shape_change)&&Number.isFinite(ai.level_change)&&
      Number.isSafeInteger(ai.scored_at)&&ai.scored_at<=now+5000&&ai.scored_at>(fresh.last_scored_at||0);
    const row=[fresh.storage_session_id,device,fresh.sensor_id,capturedIso,t,rh,p,
      matched?ai.change_index:'',matched?ai.shape_change:'',matched?ai.level_change:'',
      matched?new Date(ai.scored_at).toISOString():''];
    const target=sheet.getLastRow()+1;ensureRoom(ctx,sheet,target);
    sheet.getRange(target,1,1,row.length).setValues([row]);SpreadsheetApp.flush();
    fresh.last_telemetry_at=at;
    if(matched)fresh.last_scored_at=ai.scored_at;
    PropertiesService.getScriptProperties().setProperty(storageKey_(device),JSON.stringify(fresh));
  });
}
function getStorageTelemetry(device){
  device=validDeviceId(device);const state=storageCurrent_(device);
  if(!state.storage_session_id)return {points:[],state:state.state};
  const attempt=tryLocked(()=>{
    const ctx=sheets(),sheet=auxiliary(ctx.b,'StorageTelemetry',STORAGE_TELEMETRY_HEADERS_);
    return dataRows(sheet).filter(r=>r[0]===state.storage_session_id&&r[1]===device&&r[2]===state.sensor_id)
      .slice(-288).map(r=>({at:millis(r[3]),t:r[4],h:r[5],p:r[6],
        change:r[7]===''?null:r[7],scored_at:r[10]===''?null:millis(r[10])}));
  });
  if(!attempt.acquired)throw Error('Storage history busy; retry later');
  return {points:attempt.value,state:state.state};
}
