// Storage-condition indicators only. Scores are project display rules, never spoilage probabilities.
const STORAGE_RISK_HEADERS_=['storage_session_id','device_id','sensor_id','captured_at','temperature_c','humidity_rh','pressure_hpa','change_index','shape_change','level_change','scored_at'];
const STORAGE_MAX_GAP_MS_=600000; // 5-minute telemetry cadence plus one missed point
const STORAGE_ALERT_MARGIN_C_=2; // Change this one line for the project air-sensor alert; 4°C exposure records stay intact.
function storageRiskProfile_(label){
  const s=String(label||'').replace(/\s/g,'');
  const cold=(hours,key)=>({max_c:4,duration_h:hours,profile:key});
  if(/생닭|날닭/.test(s))return cold(24,'raw_chicken');
  if(/익힌닭|조리된닭|가열한닭/.test(s))return cold(72,'cooked_chicken');
  if(/닭가슴살|치킨|닭/.test(s))return cold(null,'chicken_state_unknown');
  if(/삶은계란|삶은달걀|완숙/.test(s))return cold(168,'hard_cooked_egg');
  if(/생달걀|날달걀/.test(s))return /껍질/.test(s)?cold(504,'raw_egg_in_shell'):cold(null,'raw_egg_package_unknown');
  if(/달걀|계란/.test(s))return cold(null,'egg_state_unknown');
  if(/익힌숙주|조리된숙주|데친숙주/.test(s))return cold(72,'cooked_sprouts_leftover');
  if(/생숙주|날숙주/.test(s))return cold(null,'raw_sprouts_duration_unknown');
  if(/숙주/.test(s))return cold(null,'sprouts_preparation_unknown');
  if(/밥|조리된쌀/.test(s))return cold(24,'cooked_rice');
  if(/두부/.test(s))return cold(null,'tofu_package_unknown');
  return {max_c:null,duration_h:null,profile:'no_specific_cold_rule'};
}
function storageRisk_(input){
  const now=input.now,start=input.started_at,profile=storageRiskProfile_(input.label),points=(input.points||[])
    .filter(p=>Number.isFinite(p.at)&&Number.isFinite(p.t)&&p.t>=-40&&p.t<=85)
    .sort((a,b)=>a.at-b.at);
  const startValid=Number.isFinite(start)&&start>0&&now>=start;
  const elapsed=startValid?(now-start)/3600000:null;
  const period=elapsed!==null&&profile.duration_h!==null?Math.min(100,elapsed<=profile.duration_h?
    70*elapsed/profile.duration_h:70+30*(elapsed/profile.duration_h-1)):null;
  let observed=0,over=0;let gap=false,previous=null;
  for(const p of points){if(!startValid||p.at<start||p.at>now)continue;
    if(previous&&p.at>previous.at){const delta=p.at-previous.at;
      if(delta>STORAGE_MAX_GAP_MS_)gap=true;
      else{observed+=delta;if(profile.max_c!==null&&previous.t>profile.max_c)over+=delta;}}
    previous=p;
  }
  const observed_h=observed>0?observed/3600000:null;
  const over_h=profile.max_c===null||observed_h===null?null:over/3600000;
  const coverage=elapsed!==null&&elapsed>0&&observed_h!==null?observed_h/elapsed:null;
  const recent=previous&&now-previous.at<=STORAGE_MAX_GAP_MS_;
  const temp=over_h!==null?Math.min(100,over_h/2*100):null;
  const reasons=[],included=[];
  if(period===null)reasons.push(elapsed===null?'STO 보관 시작 시각 없음':'식품 상태에 맞는 기간 기준 없음');else included.push('보관 경과');
  if(temp===null)reasons.push('평가 가능한 온도 구간 없음');else included.push('관측 온도 노출');
  if(gap||!recent||coverage===null||coverage<0.8)reasons.push('환경 측정 공백 또는 부족한 관측 범위');
  if(input.range_limited)reasons.push('과거 환경 기록 조회 범위 부족');
  if(input.source_gap)reasons.push('입력 음식과 일치하는 과거 실험 기록 없음');
  const score=period!==null&&temp!==null&&reasons.length===0?Math.round((period+temp)/2):null;
  const last=previous?previous.t:null;
  const activeAlertC=profile.max_c===null?null:profile.max_c+STORAGE_ALERT_MARGIN_C_;
  let warmStart=null,warmPoints=0,lastWarmAt=null;
  if(profile.max_c!==null)for(const p of points){if(!startValid||p.at<start||p.at>now)continue;
    if(p.t>activeAlertC){
      if(lastWarmAt===null||p.at-lastWarmAt>STORAGE_MAX_GAP_MS_){warmStart=p.at;warmPoints=0;}
      lastWarmAt=p.at;++warmPoints;
    }else{warmStart=null;warmPoints=0;lastWarmAt=null;}
  }
  const sustained=!!recent&&warmStart!==null&&warmPoints>=3&&previous.at-warmStart>=1800000;
  const temperature_state=profile.max_c===null?'UNASSESSED':!recent?'UNKNOWN':sustained?'ACTIVE':
    last!==null&&last>activeAlertC?'SUSPECTED':last!==null&&last>profile.max_c?'REFERENCE_EXPOSURE':
    over_h!==null&&over_h>=2?'HISTORICAL':'NORMAL';
  const temperature_warning=sustained?'용기 주변 센서의 '+activeAlertC+'°C 초과가 30분 이상 지속됐습니다.':null;
  const temperature_note=temperature_state==='SUSPECTED'?'일시적인 '+activeAlertC+'°C 초과 관측 · 지속 여부 확인 중':
    temperature_state==='HISTORICAL'?'과거 4°C 초과 관측 누적 · 현재 온도는 기준 이하':null;
  return {score,profile:profile.profile,official_c:profile.max_c,official_duration_h:profile.duration_h,
    active_alert_c:activeAlertC,
    period_score:period===null?null:Math.round(period),temperature_score:temp===null?null:Math.round(temp),
    elapsed_h:elapsed,observed_h,over_4c_h:over_h,coverage,has_gap:gap,last_temperature_c:last,
    temperature_warning,temperature_note,temperature_state,temperature_first_at:sustained?warmStart:null,
    temperature_last_at:previous?previous.at:null,reasons,included,humidity_score:null,gas_added_to_score:false,
    history_source:input.history_source||null};
}
function storageRiskHistory_(bookRead,device,storage,deviceSessions){
  if(!storage||storage.state!=='ACTIVE'||storage.device_id!==device||!Number.isFinite(storage.started_at))return [];
  const sheet=bookRead.getSheetByName('StorageTelemetry');if(!sheet)return [];
  if(JSON.stringify(sheet.getRange(1,1,1,STORAGE_RISK_HEADERS_.length).getValues()[0])!==JSON.stringify(STORAGE_RISK_HEADERS_))return [];
  const n=Math.min(6000,Math.max(0,sheet.getLastRow()-1));
  const points=n?sheet.getRange(sheet.getLastRow()-n+1,1,n,11).getValues()
    .filter(r=>r[0]===storage.storage_session_id&&r[1]===device&&r[2]===storage.sensor_id)
    .map(r=>({at:millis(r[3]),t:r[4],change:r[7]===''?null:Number(r[7]),scored_at:r[10]?millis(r[10]):null})):[];
  points.history_source={sto_env_rows:points.length,older_env_rows:0,matching_experiments:0};
  const placed=Number.isFinite(storage.placed_at)?storage.placed_at:storage.started_at;
  if(placed>=storage.started_at)return points;
  const food=typeof foodFromLabel_==='function'?foodFromLabel_(storage.food_label||storage.container_name):null;
  const allowed=new Set((deviceSessions||[]).filter(r=>food&&foodFromLabel_(r[1])===food).map(r=>r[0]));
  points.history_source.matching_experiments=allowed.size;
  const history=bookRead.getSheetByName('EnvironmentHistory');
  if(!history||JSON.stringify(history.getRange(1,1,1,HEADERS.length).getValues()[0])!==JSON.stringify(HEADERS)){
    if(storage.started_at-placed>STORAGE_MAX_GAP_MS_)
    points.source_gap='NO_MATCHING_EXPERIMENT';return points;}
  const total=Math.max(0,history.getLastRow()-1),count=Math.min(12000,total),first=history.getLastRow()-count+1;
  if(!count)return points;
  const key='STORAGE_BACKFILL_V1:'+device+':'+storage.storage_session_id+':'+placed;
  const cache=CacheService.getScriptCache();let prior=null;
  try{const meta=JSON.parse(cache.get(key)||'null');
    if(meta&&meta.total===total){prior=[];for(let i=0;i<meta.parts;++i){
      const part=cache.get(key+':'+i);if(!part){prior=null;break;}prior.push(...JSON.parse(part).map(r=>({at:r[0],t:r[1]})));}
      if(prior)points.range_limited=meta.range_limited;
    }
  }catch(_){prior=null;}
  if(!prior){
    prior=[];const dates=history.getRange(first,1,count,2).getValues();
    const sessions=history.getRange(first,7,count,1).getValues();
    const devices=history.getRange(first,23,count,1).getValues();
    for(let i=0;i<count;++i){const at=millis(dates[i][0]);
      if(devices[i][0]===device&&(allowed.has(sessions[i][0])||!sessions[i][0])&&Number.isFinite(at)&&
         at>=placed&&at<storage.started_at&&typeof dates[i][1]==='number')
        prior.push({at,t:dates[i][1]});
    }
    points.range_limited=total>count&&millis(dates[0][0])>placed;
    try{const parts=Math.ceil(prior.length/1000);for(let i=0;i<parts;++i)
      cache.put(key+':'+i,JSON.stringify(prior.slice(i*1000,(i+1)*1000).map(p=>[p.at,p.t])),300);
      cache.put(key,JSON.stringify({total,parts,range_limited:points.range_limited}),300);
    }catch(_){}
  }
  if(!prior.length&&storage.started_at-placed>STORAGE_MAX_GAP_MS_)
    points.source_gap='NO_MATCHING_EXPERIMENT';
  points.history_source.older_env_rows=prior.length;
  points.push(...prior);return points;
}
