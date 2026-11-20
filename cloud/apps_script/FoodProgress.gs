// Weak-label demo progression. Endpoint assumptions are not observed spoilage onset.
function foodProgress_(ctx){
  if(typeof FOOD_PROGRESS_MODEL_==='undefined'||!ctx.live||!Number.isFinite(ctx.elapsed_ms)||
     ctx.elapsed_ms<0)return null;
  const m=FOOD_PROGRESS_MODEL_,food=foodFromLabel_(ctx.label),idx=m.food_order.indexOf(food);
  if(idx<0)return null;
  const h=ctx.health||{},env=h.environment||{},risk=ctx.storage_risk||{};
  const envFresh=h.ble_connected===true&&Number.isFinite(h.server_seen_at)&&
    ctx.now-h.server_seen_at<=90000&&Number.isFinite(env.age_ms)&&env.age_ms>=0&&
    env.age_ms+ctx.now-h.server_seen_at<=300000&&Number.isFinite(env.t);
  const features=m.food_order.map((_,i)=>Number(i===idx));
  features.push(Math.log1p(ctx.elapsed_ms/3600000));
  const warmKnown=envFresh&&Number.isFinite(risk.over_4c_h)&&risk.has_gap!==true&&
    Number.isFinite(risk.coverage)&&risk.coverage>=.8;
  features.push(warmKnown?env.t:null,warmKnown?risk.over_4c_h:null);
  const anomaly=h.gas_anomaly,storage=ctx.storage_session||{};
  const gasKnown=warmKnown&&storage.state==='ACTIVE'&&anomaly&&
    anomaly.storage_session_id===storage.storage_session_id&&anomaly.sensor_id===storage.sensor_id&&
    anomaly.status==='EXPERIMENTAL_INDEX'&&Number.isFinite(anomaly.index)&&
    Number.isFinite(anomaly.inferred_at)&&ctx.now-anomaly.inferred_at<=1200000;
  features.push(gasKnown?anomaly.index:null);
  const selected=m.selected==='model_b'&&gasKnown?'model_b':warmKnown?'model_a':'model_time';
  const model=m.models[selected];let logit=model.intercept;
  for(let i=0;i<model.feature_indices.length;++i){const value=features[model.feature_indices[i]];
    if(!Number.isFinite(value))return null;
    logit+=model.weights[i]*(value-model.mean[i])/model.scale[i];}
  const index=Math.round(100/(1+Math.exp(-Math.max(-30,Math.min(30,logit)))));
  const code=index<33?'EARLY':index<67?'MIDDLE':'LATE';
  return {code,index,model:m.schema,basis:selected,at:ctx.now,
    evaluated:m.evaluation[selected].evaluated,groups:m.evaluation[selected].groups,
    reason:selected==='model_time'?'음식 종류와 실제 투입 이후 경과 시간':
      selected==='model_a'?'음식 종류·경과 시간·환경 온도 이력':'음식 종류·시간·환경 이력·GAS 변화',
    label_basis:'각 노출의 초반 20%와 후반 20%를 사용자가 승인한 가정으로 구분'};
}
