// Read-only inference view of existing experiment data. No ACK or sample mutation.
const FOOD_NAMES_={banana:'바나나',chicken:'닭가슴살',egg:'삶은계란',rice:'밥',sprouts:'숙주나물',tofu:'두부'};
// nRF scan start-to-start 600 s, scan timeout 60 s, 60 s cache and transport margin.
const FOOD_GAS_NORMAL_MS_=780000,FOOD_GAS_STALLED_MS_=1500000;
function foodGasStatus_(ctx){
  if(!ctx.live)return {code:'HISTORICAL',text:'종료된 실험 · 과거 측정',at:ctx.scan&&ctx.scan.at};
  const h=ctx.health||{},seen=h.server_seen_at,now=ctx.now;
  if(!Number.isFinite(seen)||now-seen>90000||h.ble_connected!==true)
    return {code:'DISCONNECTED',text:'통신 단절 · 최근 측정 확인 불가',at:null};
  const sheet=ctx.scan&&Number.isFinite(ctx.scan.at)?ctx.scan.at:null;
  const reported=h.ai_shadow&&h.ai_shadow.last_complete_scan_at;
  const gateway=Number.isFinite(reported)&&(!ctx.storage_only||
    h.ai_shadow.storage_session_id===ctx.session&&h.ai_shadow.sensor_id===h.sensor_id)?reported:null;
  const at=Math.max(sheet||0,gateway||0)||null;
  if(at!==null&&at>now+5000)return {code:'CLOCK_INVALID',text:'측정 시각 확인 필요',at};
  const newer=Number.isFinite(ctx.newer_input_at)&&ctx.newer_input_at>0&&now-ctx.newer_input_at<=FOOD_GAS_NORMAL_MS_;
  if(at===null)return {code:ctx.window_limited?'WINDOW_LIMIT':ctx.invalid_groups>0?'RECONSTRUCTION_FAILED':'NO_COMPLETE_SCAN',
    text:ctx.window_limited?'완성 GAS 기록이 조회 범위 밖에 있습니다':ctx.invalid_groups>0?'GAS 행은 있으나 완성 지문 복원 실패':newer?'새 GAS 입력 수신 · 완성 지문 대기':'완성 GAS 측정 기록 없음',at:null};
  const age=Math.max(0,now-at),pending=gateway!==null&&(sheet===null||gateway>sheet+1000);
  if(age<=FOOD_GAS_NORMAL_MS_)return {code:pending?'RECORDING_PENDING':'NORMAL',
    text:ctx.storage_only?'GAS 현장 측정 정상 · EXP 기록 종료':pending?'GAS 완성 · Sheets 기록 반영 대기':newer?'새 GAS 입력 수신 · 기존 완성 스캔 정상':'GAS 정상 주기 대기',at,age_ms:age,recorded_at:sheet};
  if(age<=FOOD_GAS_STALLED_MS_)return {code:'DELAYED',text:'GAS 주기 초과 · 다음 완성 측정 대기',at,age_ms:age,recorded_at:sheet};
  return {code:'STALLED',text:newer?'마지막 완성 GAS가 오래됨 · 새 입력은 아직 불완전':'GAS 장시간 완성 측정 없음',at,age_ms:age,recorded_at:sheet};
}
function foodFromLabel_(label){
  const s=String(label||'');
  for(const [token,key] of [['바나나','banana'],['닭가슴살','chicken'],['계란','egg'],['달걀','egg'],['밥','rice'],['숙주','sprouts'],['두부','tofu']])if(s.includes(token))return key;
  return null;
}
function foodLatestScan_(rows){
  const groups=new Map(),uids=new Map();let invalid=0;
  const rejected={incomplete:0,duplicate:0,invalid:0};
  for(const r of rows){
    if(Number(r[12])!==3)continue;
    const uid=String(r[5]),key=[r[22],r[6],r[11],r[13],r[21]].join(':');
    if(uids.has(uid)){if(JSON.stringify(uids.get(uid))!==JSON.stringify(r)){
       const old=uids.get(uid);groups.set([old[22],old[6],old[11],old[13],old[21]].join(':'),null);groups.set(key,null);
    }continue;}uids.set(uid,r);
    if(!groups.has(key))groups.set(key,[]);if(groups.get(key))groups.get(key).push(r);
  }
  const candidates=[];
  for(const group of groups.values()){
    if(!group||group.length!==10){invalid++;rejected[!group||group.length>10?'duplicate':'incomplete']++;continue;}
    const s=group.slice().sort((a,b)=>Number(a[14])-Number(b[14]));
    const valid=s.every((r,i)=>Number(r[14])===i&&Number(r[15])===i&&Number(r[21])===354&&
      typeof r[5]==='string'&&/^[a-f0-9]{32}-[1-9][0-9]*$/.test(r[5])&&
      (Number(r[17])&0xb0)===0xb0&&[18,19,20].every(k=>['true','1'].includes(String(r[k]).toLowerCase()))&&
      typeof r[4]==='number'&&Number.isFinite(r[4])&&r[4]>0&&
      [[1,-40,85],[2,0,100],[3,300,1100]].every(([k,lo,hi])=>typeof r[k]==='number'&&Number.isFinite(r[k])&&r[k]>=lo&&r[k]<=hi)&&
      Number.isFinite(millis(r[0]))&&(!i||((Number(r[16])-Number(s[i-1][16])+256)%256>0&&
        (Number(r[16])-Number(s[i-1][16])+256)%256<128&&Number(r[7])===Number(s[i-1][7])+1)));
    const times=s.map(r=>millis(r[0])),gas=s.map(r=>r[4]);
    if(!valid||Math.max(...times)-Math.min(...times)>180000||times.some((t,i)=>i&&t<times[i-1])){invalid++;rejected.invalid++;continue;}
    const logs=gas.map(Math.log10),low=gas.filter(x=>Math.abs(x-5.68)<=.01).length>=8||Math.max(...logs)-Math.min(...logs)<.05;
    candidates.push({at:Math.max(...times),gas,t:s.reduce((a,r)=>a+r[1],0)/10,h:s.reduce((a,r)=>a+r[2],0)/10,p:s.reduce((a,r)=>a+r[3],0)/10,quality:low?'LOW_INFORMATION':'OK'});
  }
  candidates.sort((a,b)=>b.at-a.at);
  const latestInput=rows.reduce((a,r)=>Math.max(a,Number.isFinite(millis(r[0]))?millis(r[0]):0),0);
  const newerInput=candidates[0]&&latestInput>candidates[0].at?latestInput:null;
  return {scan:candidates[0]||null,scans:candidates,invalid_groups:invalid,rejected_groups:rejected,
    latest_input_at:latestInput,newer_input_at:newerInput};
}
function foodHistoricalChange_(scans){
  const valid=(scans||[]).filter(s=>s.quality==='OK').slice().reverse();
  if(valid.length<4)return null;
  const first=valid.slice(0,3),shape=Array(10).fill(0);let baseLevel=0;
  for(const scan of first){const logs=scan.gas.map(Math.log10),mean=logs.reduce((a,b)=>a+b,0)/10;
    baseLevel+=mean/3;for(let i=0;i<10;++i)shape[i]+=(logs[i]-mean)/3;}
  const score=scan=>{const logs=scan.gas.map(Math.log10),mean=logs.reduce((a,b)=>a+b,0)/10;
    return .7*logs.reduce((sum,x,i)=>sum+Math.abs(x-mean-shape[i]),0)/10+.3*Math.abs(mean-baseLevel);};
  const latest=valid[valid.length-1],previous=valid.length>4?valid[valid.length-2]:null;
  return {index:score(latest),delta:previous?score(latest)-score(previous):null,
    baseline_at:first[0].at,at:latest.at,complete_scans:valid.length,source:'SensorData time window'};
}
function storageGasHistoryKey_(device){return 'STORAGE_GAS_HISTORY_V1:'+device;}
// Last-resort time estimate if a separate model file is absent from the deployment.
// Median completed exposure lengths in the unchanged local two-week scan audit.
const FOOD_EXPOSURE_H_={banana:65.751,chicken:74.831,egg:93.894,rice:94.804,sprouts:84.707,tofu:18.521};
function foodFallbackProgress_(ctx){
  if(!ctx.live||!Number.isFinite(ctx.elapsed_ms)||ctx.elapsed_ms<0)return null;
  const food=foodFromLabel_(ctx.label),reference=FOOD_EXPOSURE_H_[food];
  if(!Number.isFinite(reference)||reference<=0)return null;
  const index=Math.min(100,Math.max(0,Math.round(100*ctx.elapsed_ms/3600000/reference)));
  return {code:index<33?'EARLY':index<67?'MIDDLE':'LATE',index,
    model:'assumed-exposure-duration-v1',basis:'historical_duration_fallback',at:ctx.now,
    reason:'음식 종류와 실제 투입 이후 경과 시간',
    label_basis:'동일 음식 실험의 종료 시점을 변질로 간주한 캡스톤 가정'};
}
function foodDisplayProgress_(progress,ctx){
  if(!progress)return null;
  const reference=FOOD_EXPOSURE_H_[foodFromLabel_(ctx.label)];
  if(!Number.isFinite(reference)||!Number.isFinite(ctx.elapsed_ms))return progress;
  const ratio=Math.max(0,ctx.elapsed_ms/3600000/reference);
  return {...progress,display_code:ratio<.7?'EARLY':ratio<1?'MIDDLE':'LATE',
    display_ratio:ratio,display_reference_h:reference,
    display_policy:'음식별 과거 노출기간 70%부터 소비 우선, 100%부터 최종 확인'};
}
function foodEdgeProgress_(ctx){
  const h=ctx.health||{},p=h.food_progress,s=ctx.storage_session||{};
  if(!ctx.live||!p||s.state!=='ACTIVE'||p.storage_session_id!==s.storage_session_id||
     p.sensor_id!==s.sensor_id||p.sensor_id!==h.sensor_id||p.placed_at!==ctx.started_at||
     p.food_code!==foodFromLabel_(ctx.label)||!Number.isFinite(h.server_seen_at)||
     ctx.now-h.server_seen_at>90000||ctx.now-p.inferred_at>90000||
     p.inferred_at>ctx.now+5000)return null;
  return {code:p.stage,index:p.index,model:p.model,basis:'esp32_time',at:p.inferred_at,
    evaluated:typeof FOOD_PROGRESS_MODEL_==='undefined'?null:FOOD_PROGRESS_MODEL_.evaluation.model_time.evaluated,
    groups:typeof FOOD_PROGRESS_MODEL_==='undefined'?null:FOOD_PROGRESS_MODEL_.evaluation.model_time.groups,
    reason:'ESP32-S3 음식 종류와 실제 투입 이후 경과 시간',
    label_basis:'각 노출의 초반 20%와 후반 20%를 사용자가 승인한 가정으로 구분'};
}
function foodProgressUnavailable_(ctx){
  if(typeof foodProgress_!=='function')return 'PROGRESS_RUNTIME_MISSING';
  if(typeof FOOD_PROGRESS_MODEL_==='undefined')return 'PROGRESS_MODEL_MISSING';
  if(!ctx.live)return 'NO_CURRENT_STORAGE_OR_RECORDING';
  if(!foodFromLabel_(ctx.label))return 'FOOD_TYPE_NOT_SET';
  if(!Number.isFinite(ctx.elapsed_ms))return 'PLACEMENT_TIME_UNKNOWN';
  if(!ctx.health||!Number.isFinite(ctx.health.server_seen_at)||ctx.now-ctx.health.server_seen_at>90000)
    return 'CLOUD_CONTACT_STALE';
  return 'PROGRESS_INPUT_UNAVAILABLE';
}
function getStorageGasBackfill(device){
  device=validDeviceId(device);const storage=storageCurrent_(device),placed=storage.placed_at;
  if(!['PENDING','ACTIVE'].includes(storage.state)||!Number.isFinite(placed)||!storage.food_label)
    throw Error('Set the current food and actual placement time first');
  const bookRead=book(),sessions=bookRead.getSheetByName('Sessions'),sensor=bookRead.getSheetByName('SensorData');
  if(!sessions||!sensor||JSON.stringify(sensor.getRange(1,1,1,HEADERS.length).getValues()[0])!==JSON.stringify(HEADERS))
    throw Error('Existing SensorData schema is unavailable');
  const food=foodFromLabel_(storage.food_label),records=dataRows(sessions).filter(r=>(r[4]||'legacy')===device);
  const allowed=new Set(records.filter(r=>food&&foodFromLabel_(r[1])===food).map(r=>r[0]));
  for(const r of records)if(!r[3]&&Number.isFinite(millis(r[2]))&&millis(r[2])>=placed)allowed.add(r[0]);
  const rows=[],last=sensor.getLastRow(),page=1000;
  for(let first=2;first<=last;first+=page){
    const count=Math.min(page,last-first+1),dates=sensor.getRange(first,1,count,1).getValues(),
      sessionIds=sensor.getRange(first,7,count,1).getValues(),devices=sensor.getRange(first,23,count,1).getValues();
    const hasMatch=dates.some((r,i)=>devices[i][0]===device&&allowed.has(sessionIds[i][0])&&
      Number.isFinite(millis(r[0]))&&millis(r[0])>=placed);
    if(!hasMatch)continue;
    rows.push(...sensor.getRange(first,1,count,HEADERS.length).getValues().filter(r=>
      (r[22]||'legacy')===device&&allowed.has(r[6])&&Number.isFinite(millis(r[0]))&&millis(r[0])>=placed));
  }
  if(sensor.getLastRow()!==last)throw Error('SensorData changed during historical review; retry');
  const restored=foodLatestScan_(rows),change=foodHistoricalChange_(restored.scans);
  const summary={device_id:device,storage_session_id:storage.storage_session_id,placed_at:placed,
    source_last_row:last,source_rows:rows.length,complete_scans:restored.scans.length,
    rejected_groups:restored.rejected_groups,change,analyzed_at:Date.now(),
    status:change?'READY':restored.scans.length?'BASELINE_PENDING':'NO_STORED_GAS'};
  const current=storageCurrent_(device);
  if(current.storage_session_id===storage.storage_session_id&&current.placed_at===placed)
    PropertiesService.getScriptProperties().setProperty(storageGasHistoryKey_(device),JSON.stringify(summary));
  CacheService.getScriptCache().remove('FOOD_REVIEW_V1:'+device+':current');
  return summary;
}
function foodPredict_(scan,model){
  if(!scan||scan.quality!=='OK'||!model)return null;
  const logs=scan.gas.map(Math.log10),mean=logs.reduce((a,b)=>a+b,0)/10;
  const x=[...logs.map(x=>x-mean),scan.t,scan.h,scan.p];
  const z=[1,...x.map((v,i)=>(v-model.mean[i])/model.std[i])];
  const scores=model.classes.map((_,j)=>z.reduce((a,v,i)=>a+v*model.coef[i][j],0));
  if(!scores.every(Number.isFinite))return null;
  const order=scores.map((s,i)=>({s,i})).sort((a,b)=>b.s-a.s),candidate=model.classes[order[0].i];
  return {candidate,name:FOOD_NAMES_[candidate],margin:order[0].s-order[1].s,
    approved:model.food_release_approved===true&&model.exposure_support[candidate]>=2,
    confidence:null,reason:'노출 실험 분리 검증 부족 · 분류 후보는 진단용'};
}
function foodDecision_(ctx){
  const base={code:'UNKNOWN',title:'판정 불가',icon:'⚪',tone:'unknown',judged_at:null,
    evaluated_at:ctx.now,reasons:[],food_confidence:null,state_confidence:null};
  if(!ctx.session)return {...base,reasons:['선택한 실험 기록이 없습니다.']};
  if(!ctx.live)return {...base,reasons:['종료된 실험입니다. 과거 측정값을 현재 상태로 판정하지 않습니다.']};
  if(!ctx.health||!Number.isFinite(ctx.health.server_seen_at)||ctx.now-ctx.health.server_seen_at>90000)
    return {...base,reasons:['최근 통신이 없습니다. 장치 연결을 확인해 주세요.']};
  if(ctx.health.ble_connected!==true||
     (ctx.health.sensor_ok===false&&!['GAS','COOL'].includes(ctx.health.sensor_mode))||
     ctx.health.spool_error||ctx.health.queue_drops>0)
    return {...base,reasons:['센서 연결 또는 데이터 품질을 확인해 주세요.']};
  const gas=ctx.gas_status||foodGasStatus_(ctx);
  if(gas.code==='DISCONNECTED'||gas.code==='CLOCK_INVALID'||gas.code==='STALLED'||gas.code==='DELAYED')
    return {...base,reasons:[gas.text]};
  if(gas.code==='RECORDING_PENDING')return {...base,reasons:[ctx.storage_only?
    '현재 보관 용기에서 GAS 측정은 확인됐습니다. EXP 기록은 종료됐습니다.':
    '새 GAS 측정은 확인됐고 클라우드 기록 반영을 기다립니다.']};
  if(!ctx.scan)return {...base,reasons:[gas.text]};
  if(ctx.scan.quality!=='OK')return {...base,reasons:['가스 지문에 정보가 부족해 판단을 보류했습니다.']};
  // An actual validated spoilage model is required; food identity/proxy outputs cannot enter here.
  const m=ctx.spoilage;
  if(!m||m.validated!==true||m.target!=='observed_spoilage'||m.session_id!==ctx.session||
     m.at!==ctx.scan.at||!Number.isFinite(m.confidence)||m.confidence<0.8)
    return {...base,reasons:['측정은 정상입니다. 변질 판정을 검증할 관찰 레이블이 부족합니다.']};
  const labels={FRESH:['신선 추정','🟢','fresh'],SUSPECT:['변질 의심','🟡','suspect'],SIGNS:['변질 징후 감지','🔴','signs']};
  if(!labels[m.code])return {...base,reasons:['모델 결과를 확인할 수 없습니다.']};
  const [title,icon,tone]=labels[m.code];
  return {...base,code:m.code,title,icon,tone,judged_at:m.at,state_confidence:m.confidence,reasons:m.reasons||[]};
}
function getFoodReview(device,requestedSession){
  device=validDeviceId(device);
  const cache=CacheService.getScriptCache(),key='FOOD_REVIEW_V1:'+device+':'+String(requestedSession||'current');
  const cached=cachedObject(cache,key);
  if(cached&&Date.now()-cached.evaluated_at<60000){
    // Re-evaluate freshness against current clock/contact even for cached rows.
    cached.health=cachedObject(cache,'LIVE_V2:'+device);cached.now=Date.now();
    cached.gas_status=foodGasStatus_(cached);
    cached.decision=foodDecision_(cached);
    cached.progress=foodDisplayProgress_(foodEdgeProgress_(cached)||
      (typeof foodProgress_==='function'?foodProgress_(cached):null)||foodFallbackProgress_(cached),cached);
    cached.progress_fallback_reason=cached.progress&&cached.progress.basis==='historical_duration_fallback'?
      foodProgressUnavailable_(cached):null;
    cached.progress_unavailable=cached.progress?null:foodProgressUnavailable_(cached);
    return cached;
  }
  // Read only: do not acquire the ingest/ACK Script Lock or run schema migration.
  const bookRead=book(),sessions=bookRead.getSheetByName('Sessions'),sensor=bookRead.getSheetByName('SensorData');
  if(!sessions||!sensor)throw Error('기존 실험 데이터 시트를 찾을 수 없습니다.');
  const same=(sheet,headers)=>JSON.stringify(sheet.getRange(1,1,1,headers.length).getValues()[0])===JSON.stringify(headers);
  if(!same(sessions,SESSION_HEADERS)||!same(sensor,HEADERS))throw Error('기존 데이터 열 구성을 확인해 주세요.');
  const deviceSessions=dataRows(sessions).filter(r=>(r[4]||'legacy')===device);
    const choices=deviceSessions.slice(-100).reverse().map(r=>({id:r[0],label:r[1],started_at:millis(r[2]),stopped_at:r[3]?millis(r[3]):null}));
    const selected=requestedSession?choices.find(r=>r.id===requestedSession):choices.find(r=>!r.stopped_at)||choices[0];
    const storage=typeof storageCurrent_==='function'?storageCurrent_(device):null;
    const currentExp=!!selected&&!selected.stopped_at;
    const currentStorage=!requestedSession&&storage&&['PENDING','ACTIVE'].includes(storage.state)&&
      Number.isFinite(storage.placed_at);
    const storageOnly=currentStorage&&!currentExp;
    const placedAt=currentStorage&&Number.isFinite(storage.placed_at)?storage.placed_at:
      currentStorage?storage.started_at:null;
    const foodLabel=currentStorage?storage.food_label||(currentExp?selected.label:storage.container_name):null;
    const total=Math.max(0,sensor.getLastRow()-1),limit=requestedSession?6000:1200,now=Date.now();
    const health=cachedObject(cache,'LIVE_V2:'+device);
    let n=Math.min(limit,total);
    const foodKey=currentStorage?foodFromLabel_(foodLabel):null;
    const sessionIds=new Set(currentStorage&&foodKey?deviceSessions.filter(r=>foodFromLabel_(r[1])===foodKey).map(r=>r[0]):[]);
    if(selected)sessionIds.add(selected.id);
    const readRows=count=>count?sensor.getRange(sensor.getLastRow()-count+1,1,count,HEADERS.length).getValues()
      .filter(r=>sessionIds.has(r[6])&&(r[22]||'legacy')===device&&
        (!currentStorage||Number.isFinite(millis(r[0]))&&millis(r[0])>=placedAt)):[];
    let rows=selected&&!storageOnly?readRows(n):[],reconstructed=foodLatestScan_(rows);
    const oldestInWindow=count=>count?millis(sensor.getRange(sensor.getLastRow()-count+1,1).getValues()[0][0]):null;
    const needEarlier=currentStorage&&n<total&&
      Number.isFinite(oldestInWindow(n))&&oldestInWindow(n)>placedAt;
    const reported=health&&health.ai_shadow&&health.ai_shadow.last_complete_scan_at;
    if(selected&&!storageOnly&&n<total&&n<6000&&(needEarlier||!reconstructed.scan||Number.isFinite(reported)&&reported>reconstructed.scan.at+1000)){
      n=Math.min(6000,total);rows=readRows(n);reconstructed=foodLatestScan_(rows);
    }
    const historyWindowLimited=!!currentStorage&&n<total&&
      Number.isFinite(oldestInWindow(n))&&oldestInWindow(n)>placedAt;
    const scan=reconstructed.scan;
    const label=currentStorage?foodLabel:selected?selected.label:'';
    const food=foodFromLabel_(label);
    const model=typeof FOOD_REVIEW_MODEL_==='undefined'?null:FOOD_REVIEW_MODEL_;
    const prediction=foodPredict_(scan,model);
    const out={device_id:device,choices,session:storageOnly?storage.storage_session_id:selected?selected.id:null,label,
      live:currentExp||!!storageOnly,storage_only:!!storageOnly,
      started_at:currentStorage?placedAt:selected?selected.started_at:null,
      elapsed_ms:currentStorage?Math.max(0,now-placedAt):selected?Math.max(0,(selected.stopped_at||now)-selected.started_at):null,
      food_name:food?FOOD_NAMES_[food]:storageOnly?label:'음식 종류 확인 필요',
      food_source:currentStorage&&storage.food_label?'보관 설정 음식':
        currentExp&&food?'실험명에 입력된 음식':storageOnly?'보관 용기 이름':'미확인',
      food_prediction:prediction,scan,invalid_groups:reconstructed.invalid_groups,
      rejected_groups:reconstructed.rejected_groups,latest_input_at:reconstructed.latest_input_at,
      newer_input_at:reconstructed.newer_input_at,
      data_window_rows:n,window_limited:!!selected&&!storageOnly&&!rows.length&&n<total,
      gas_history_limited:historyWindowLimited,
      health,now,evaluated_at:now};
    if(typeof storageRisk_==='function'){
      const current=currentExp||storageOnly;
      out.storage_session=current&&storage&&storage.state==='ACTIVE'?
        {state:'ACTIVE',storage_session_id:storage.storage_session_id,sensor_id:storage.sensor_id}:null;
      out.storage_started_at=current&&storage&&storage.state==='ACTIVE'?storage.started_at:null;
      const riskPoints=current?storageRiskHistory_(bookRead,device,storage,deviceSessions):[];
      out.storage_risk=storageRisk_({now,started_at:out.started_at,label:out.label,points:riskPoints,
        range_limited:riskPoints.range_limited,source_gap:riskPoints.source_gap,
        history_source:riskPoints.history_source});
      const scores=riskPoints.filter(p=>Number.isFinite(p.change)&&Number.isFinite(p.scored_at)).sort((a,b)=>a.scored_at-b.scored_at);
      const latest=scores[scores.length-1],prior=scores.length>1?scores[scores.length-2]:null;
      if(latest)out.gas_change={index:latest.change,delta:prior?latest.change-prior.change:null,at:latest.scored_at,source:'StorageTelemetry'};
      const ai=health&&health.ai_shadow;
      if(current&&storage&&ai&&ai.storage_session_id===storage.storage_session_id&&ai.sensor_id===storage.sensor_id&&
         Number.isFinite(ai.change_index)&&Number.isFinite(ai.scored_at)&&(!latest||ai.scored_at>latest.scored_at))
        out.gas_change={index:ai.change_index,delta:latest?ai.change_index-latest.change:null,at:ai.scored_at,source:'gateway'};
      if(current){
        const raw=PropertiesService.getScriptProperties().getProperty(storageGasHistoryKey_(device));
        let full=null;try{full=raw?JSON.parse(raw):null;}catch(_){}
        if(full&&full.storage_session_id===storage.storage_session_id&&full.placed_at===placedAt){
          out.historical_gas=full.change;out.gas_history_limited=false;
          out.gas_history_status=full.status;out.gas_history_rows=full.source_rows;
          out.gas_history_scans=full.complete_scans;
        }else if(!historyWindowLimited)out.historical_gas=foodHistoricalChange_(reconstructed.scans);
      }
    }
    out.gas_status=foodGasStatus_(out);out.decision=foodDecision_(out);
    out.progress=foodDisplayProgress_(foodEdgeProgress_(out)||
      (typeof foodProgress_==='function'?foodProgress_(out):null)||foodFallbackProgress_(out),out);
    out.progress_fallback_reason=out.progress&&out.progress.basis==='historical_duration_fallback'?
      foodProgressUnavailable_(out):null;
    out.progress_unavailable=out.progress?null:foodProgressUnavailable_(out);
  cache.put(key,JSON.stringify(out),60);return out;
}
