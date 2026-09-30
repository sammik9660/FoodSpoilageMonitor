// Reliability v2. Existing SensorData rows and SPREADSHEET_ID are never reset.
const UPLOAD_SCHEMA = 2;
const MAX_BATCH = 32;
const DASHBOARD_REFRESH_MS = 2000;
const STALE_MS = 15000;
const HEADERS = ['timestamp','temperature','humidity','pressure','gas_resistance',
  'sample_uid','session_id','nrf_seq','nrf_ms','capture_quality','received_at','nrf_boot'];
const SESSION_HEADERS = ['session_id','experiment_label','started_at','stopped_at'];
const INGEST_HEADERS = ['gateway_boot_id','ack_high_water','pending_transaction'];

function locked(fn) {
  const lock = LockService.getScriptLock();
  lock.waitLock(20000);
  try { return fn(); } finally { lock.releaseLock(); }
}
function book() {
  const id = PropertiesService.getScriptProperties().getProperty('SPREADSHEET_ID');
  if (!id) throw new Error('SPREADSHEET_ID missing: select the existing spreadsheet; automatic recreation is forbidden');
  return SpreadsheetApp.openById(id);
}
function auxiliary(b, name, headers) {
  let s = b.getSheetByName(name);
  if (!s) { s=b.insertSheet(name); s.getRange(1,1,1,headers.length).setValues([headers]); s.setFrozenRows(1); }
  const actual=s.getRange(1,1,1,headers.length).getValues()[0];
  if (JSON.stringify(actual)!==JSON.stringify(headers)) throw new Error(name+' header mismatch');
  return s;
}
function sheets() {
  const b=book(), sensor=b.getSheetByName('SensorData');
  // Never silently create/reset SensorData on reboot, missing config, or ingest.
  if (!sensor || !sensor.getLastRow()) throw new Error('Existing SensorData/header missing; manual repair required');
  const actual=sensor.getRange(1,1,1,HEADERS.length).getValues()[0];
  for(let i=0;i<5;i++) if(actual[i]!==HEADERS[i]) throw new Error('Legacy SensorData header mismatch');
  for(let i=5;i<HEADERS.length;i++) if(actual[i] && actual[i]!==HEADERS[i]) throw new Error('Schema extension would overwrite header');
  if(actual.slice(5).some((v,i)=>v!==HEADERS[i+5])) {
    if(sensor.getLastColumn()>5 && actual.slice(5).some(v=>!v)) throw new Error('Inspect existing extended columns before migration');
    sensor.getRange(1,6,1,HEADERS.length-5).setValues([HEADERS.slice(5)]);
  }
  return {b, sensor, sessions:auxiliary(b,'Sessions',SESSION_HEADERS),
    ingest:auxiliary(b,'IngestState',INGEST_HEADERS), unassigned:auxiliary(b,'Unassigned',HEADERS)};
}
function dataRows(s) { return s.getLastRow()<2?[]:s.getRange(2,1,s.getLastRow()-1,s.getLastColumn()).getValues(); }
function millis(v) { return v instanceof Date?v.getTime():typeof v==='number'?v:Date.parse(v); }
function sessionsList(s) {
  return dataRows(s).map(r=>({id:r[0],label:r[1],start:millis(r[2]),stop:r[3]?millis(r[3]):null}));
}
function activeSession(list) {
  const active=list.filter(s=>s.stop===null);
  if(active.length>1) throw new Error('Multiple active sessions; manual repair required');
  return active[0]||null;
}
function sessionFor(sample, sessions) {
  // DATA INTEGRITY: capture time, NOT HTTP arrival, assigns session membership.
  // Late retries must not move samples across a subsequent Start/Stop boundary.
  if(sample.captured_at===null) return null;
  const matches=sessions.filter(s=>sample.captured_at>=s.start && (s.stop===null || sample.captured_at<s.stop));
  if(matches.length>1) throw new Error('Overlapping session intervals');
  return matches[0]||null;  // intervals are [start, stop)
}
function ensureRoom(ctx, s, endRow) {
  if(endRow>320000) throw new Error('Sheet row safety limit reached; archive manually, no overwrite');
  if(endRow>s.getMaxRows()) {
    const extra=Math.max(500,endRow-s.getMaxRows());
    const cells=ctx.b.getSheets().reduce((n,x)=>n+x.getMaxRows()*x.getMaxColumns(),0);
    if(cells+extra*s.getMaxColumns()>9500000) throw new Error('Spreadsheet cell budget exhausted');
    s.insertRowsAfter(s.getMaxRows(),extra);
  }
}
function comparable(row) { return JSON.stringify(row.map(v=>v instanceof Date?v.toISOString():v)); }
function finishTransaction(ctx, row, boot, high, pending) {
  // Write-ahead journal survives a response loss or interruption between Sheets
  // write and ACK. Only our reserved empty/identical cells may be written again.
  for(const group of pending.groups) {
    const s=group.sheet==='SensorData'?ctx.sensor:ctx.unassigned;
    ensureRoom(ctx,s,group.start+group.rows.length-1);
    const existing=s.getRange(group.start,1,group.rows.length,HEADERS.length).getValues();
    existing.forEach((r,i)=>{
      if(r.some(v=>v!=='' && v!==null) && comparable(r)!==comparable(group.rows[i]))
        throw new Error('Journal conflict: existing data preserved; manual repair required');
    });
    s.getRange(group.start,1,group.rows.length,HEADERS.length).setValues(group.rows);
  }
  SpreadsheetApp.flush();
  ctx.ingest.getRange(row,1,1,3).setValues([[boot,Math.max(high,pending.high),'']]);
  SpreadsheetApp.flush();
}
function recoverTransactions(ctx) {
  dataRows(ctx.ingest).forEach((r,i)=>{
    if(r[2]) finishTransaction(ctx,i+2,r[0],Number(r[1]),JSON.parse(r[2]));
  });
}
function setupReliability() { return locked(()=>{const ctx=sheets();recoverTransactions(ctx);return {success:true,spreadsheet_id:ctx.b.getId()};}); }
function recordingResult(ctx) {
  const list=sessionsList(ctx.sessions), active=activeSession(list);
  const latest=active?null:(list.slice().reverse().find(s=>s.stop!==null)||null);
  const download=active||latest;
  return {state:active?'RECORDING':'STOPPED',session_id:active?active.id:null,experiment_label:active?active.label:null,
    download_session_id:download?download.id:null,download_experiment_label:download?download.label:null,
    download_kind:active?'CURRENT':latest?'LATEST':null};
}
function startRecording(label) {
  if(typeof label!=='string' || !label.trim() || label.length>120 || /^[=+@\-]/.test(label)) throw new Error('Invalid experiment label');
  return locked(()=>{
    const ctx=sheets();recoverTransactions(ctx);
    if(activeSession(sessionsList(ctx.sessions))) throw new Error('A session is already active');
    const id='EXP-'+Utilities.getUuid(), now=new Date().toISOString();
    const row=ctx.sessions.getLastRow()+1;ensureRoom(ctx,ctx.sessions,row);
    ctx.sessions.getRange(row,1,1,4).setValues([[id,label.trim(),now,'']]);
    SpreadsheetApp.flush();return recordingResult(ctx);
  });
}
function stopRecording() {
  return locked(()=>{
    const ctx=sheets();recoverTransactions(ctx);
    const list=sessionsList(ctx.sessions), active=activeSession(list);
    if(active) ctx.sessions.getRange(list.indexOf(active)+2,4).setValue(new Date().toISOString());
    SpreadsheetApp.flush();return recordingResult(ctx);
  });
}
function number(v,min,max,name) {
  if(typeof v!=='number'||!Number.isFinite(v)||v<min||v>max) throw new Error('Invalid '+name);
  return v;
}
function uint(v,name) {number(v,0,4294967295,name);if(!Number.isInteger(v))throw new Error('Invalid '+name);return v;}
function validateSample(s, now) {
  if(!s || typeof s!=='object' || Array.isArray(s)) throw new Error('Invalid sample');
  const match=typeof s.uid==='string' && /^([a-f0-9]{32})-([1-9][0-9]{0,9})$/.exec(s.uid);
  if(!match) throw new Error('Invalid sample UID');
  const rx=uint(Number(match[2]),'rx');
  if(!rx) throw new Error('Invalid rx');
  uint(s.seq,'seq');uint(s.ms,'ms');uint(s.nrf_boot,'nrf_boot');
  if(!s.seq) throw new Error('Zero sensor sequence');
  if(s.quality!=='ntp'&&s.quality!=='unknown'&&s.quality!=='legacy_server') throw new Error('Invalid time quality');
  if(s.quality==='unknown') {if(s.captured_at!==null)throw new Error('Unknown capture time must be null');}
  else number(s.captured_at,1577836800000,now+5000,'capture time');
  return {uid:s.uid,boot:match[1],rx,seq:s.seq,ms:s.ms,nrf_boot:s.nrf_boot,
    captured_at:s.captured_at,quality:s.quality,t:number(s.t,-40,85,'T'),h:number(s.h,0,100,'H'),
    p:number(s.p,300,1100,'P'),g:number(s.g,0,1e9,'G')};
}
function normalize(payload, now) {
  if(!payload||typeof payload!=='object'||Array.isArray(payload)) throw new Error('Object required');
  if(payload.version===2) {
    if(!Array.isArray(payload.samples)||payload.samples.length>MAX_BATCH)throw new Error('Invalid batch size');
    const samples=payload.samples.map(s=>validateSample(s,now));
    for(let i=1;i<samples.length;i++)if(samples[i].boot!==samples[0].boot||samples[i].rx<=samples[i-1].rx)throw new Error('Batch must be strictly FIFO in one gateway boot');
    return samples;
  }
  // Legacy manual POST has no capture clock or retry identity. Preserve both old
  // key spellings, explicitly tag arrival-time quality; no retry dedup guarantee.
  const short=Object.prototype.hasOwnProperty.call(payload,'T');
  const s={uid:Utilities.getUuid().replace(/-/g,'')+'-1',seq:1,ms:0,nrf_boot:0,
    captured_at:now,quality:'legacy_server',t:short?payload.T:payload.temperature,
    h:short?payload.H:payload.humidity,p:short?payload.P:payload.pressure,g:short?payload.G:payload.gas};
  return [validateSample(s,now)];
}
function healthSnapshot(h, now) {
  const out={server_seen_at:now};
  if(!h||typeof h!=='object')return out;
  const numeric=['last_valid_age_ms','queue_depth','spool_depth','sensor_failures','sensor_recoveries',
    'sensor_attempts','ble_gaps','ble_restarts','parse_errors','queue_drops','upload_failures',
    'last_upload_at','nrf_seq','spool_capacity_bytes','spool_used_bytes','unassigned_count'];
  numeric.forEach(k=>{if(typeof h[k]==='number'&&Number.isFinite(h[k])&&h[k]>=0)out[k]=h[k];});
  ['ble_connected','sensor_ok','spool_error','spool_full'].forEach(k=>{if(typeof h[k]==='boolean')out[k]=h[k];});
  if(h.latest) {const x=h.latest;try {out.latest={t:number(x.t,-40,85,'T'),h:number(x.h,0,100,'H'),p:number(x.p,300,1100,'P'),g:number(x.g,0,1e9,'G')};}catch(_) {}}
  return out;
}
function ingest(payload) {
  const now=Date.now(), samples=normalize(payload,now); // validate ALL before any mutation
  return locked(()=>{
    const ctx=sheets();recoverTransactions(ctx);
    const list=sessionsList(ctx.sessions);activeSession(list);
    const acks=[];let saved=0;
    if(samples.length) {
      const boot=samples[0].boot, states=dataRows(ctx.ingest);
      let idx=states.findIndex(r=>r[0]===boot), row=idx<0?ctx.ingest.getLastRow()+1:idx+2;
      let high=idx<0?0:Number(states[idx][1]);
      const groups={SensorData:[],Unassigned:[]};
      samples.forEach(s=>{
        let status='duplicate';
        if(s.rx>high) {
          const session=sessionFor(s,list);
          status=s.captured_at===null?'unassigned_time':session?'stored':'not_recording';
          if(status==='stored'||status==='unassigned_time') {
            const values=[s.captured_at===null?'':new Date(s.captured_at).toISOString(),s.t,s.h,s.p,s.g,
              s.uid,session?session.id:'',s.seq,s.ms,s.quality,new Date(now).toISOString(),s.nrf_boot];
            groups[status==='stored'?'SensorData':'Unassigned'].push(values);++saved;
          }
        }
        acks.push({uid:s.uid,status});
      });
      if(samples[samples.length-1].rx>high) {
        const pending={high:samples[samples.length-1].rx,groups:[]};
        Object.keys(groups).forEach(name=>{
          if(groups[name].length)pending.groups.push({sheet:name,start:(name==='SensorData'?ctx.sensor:ctx.unassigned).getLastRow()+1,rows:groups[name]});
        });
        const journal=JSON.stringify(pending);
        if(journal.length>45000)throw new Error('Journal too large');
        ensureRoom(ctx,ctx.ingest,row);
        ctx.ingest.getRange(row,1,1,3).setValues([[boot,high,journal]]);
        SpreadsheetApp.flush();
        finishTransaction(ctx,row,boot,high,pending);
      }
    }
    const record=recordingResult(ctx);
    try {
      const snap=healthSnapshot(payload.health,now);
      // Health uses a bounded cache, never raw samples in ScriptProperties.
      if(!snap.latest && payload.version!==2 && samples.length) {
        const s=samples[0];snap.latest={t:s.t,h:s.h,p:s.p,g:s.g};snap.last_valid_age_ms=0;
      }
      CacheService.getScriptCache().put('LIVE_V2',JSON.stringify(snap),600);
    } catch(_) {} // storage ACK must not fail solely because dashboard cache failed
    return {success:true,version:2,acks,stored:saved,recording:record,server_time:now};
  });
}
function doPost(e) {
  try {
    if (!e || !e.postData ||
        typeof e.postData.contents !== 'string' ||
        e.postData.contents.length > 64000) {
      throw new Error('Invalid request size');
    }

    return json(ingest(JSON.parse(e.postData.contents)));

  } catch (error) {
    return json({
      success: false,
      version: 2,
      error: String(error.message || error)
    });
  }
}
function json(v) {return ContentService.createTextOutput(JSON.stringify(v)).setMimeType(ContentService.MimeType.JSON);}
function getDashboard() {
  const recording=locked(()=>{const ctx=sheets();recoverTransactions(ctx);return recordingResult(ctx);});
  const raw=CacheService.getScriptCache().get('LIVE_V2'), h=raw?JSON.parse(raw):null;
  const now=Date.now(), transportStale=!h||now-h.server_seen_at>STALE_MS;
  const age=h&&typeof h.last_valid_age_ms==='number'?h.last_valid_age_ms+now-h.server_seen_at:null;
  let state='WAITING / UNKNOWN';
  if(transportStale)state='STALE / CLOUD CONTACT UNKNOWN';
  else if(h.spool_error||h.spool_full||h.queue_drops>0)state='DATA INTEGRITY ERROR';
  else if(h.ble_connected===false)state='BLE DISCONNECTED';
  else if(h.sensor_ok===false)state='SENSOR ERROR';
  else if(age===null||age>STALE_MS)state='STALE';
  else state='LIVE / ONLINE';
  return {state,age,health:h,recording,server_time:now};
}
function getSensorData() {return getDashboard();}
function csvCell(v) {if(v instanceof Date)v=v.toISOString();return '"'+String(v===null?'':v).replace(/"/g,'""')+'"';}
function csvFilename(label) {
  if(label===null)return 'food-spoilage-all.csv';
  let name=String(label);
  if(name.normalize)name=name.normalize('NFC');
  name=name.replace(/[\u0000-\u001F\u007F<>:"/\\|?*]/g,'_').replace(/\s+/g,' ').trim().replace(/[. ]+$/g,'');
  name=name.replace(/\.csv$/i,'').replace(/[. ]+$/g,'');
  if(!name)name='session';
  if(/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(name))name='_'+name;
  name=Array.from(name).slice(0,100).join('');
  return name+'.csv';
}
function getCsvPage(sessionId, offset, limit) {
  return locked(()=>{
    const ctx=sheets();recoverTransactions(ctx);
    const session=sessionId?sessionsList(ctx.sessions).find(s=>s.id===sessionId):null;
    if(sessionId&&!session)throw new Error('Unknown session');
    const total=ctx.sensor.getLastRow()-1;
    if(offset===undefined && total>10000)throw new Error('CSV exceeds 10000 rows: use explicit offset/limit pages (see dashboard)');
    offset=offset===undefined?0:Number(offset);limit=limit===undefined?10000:Number(limit);
    if(!Number.isInteger(offset)||offset<0||!Number.isInteger(limit)||limit<1||limit>10000)throw new Error('Invalid CSV page');
    const n=Math.max(0,Math.min(limit,total-offset));
    const rows=n?ctx.sensor.getRange(offset+2,1,n,HEADERS.length).getValues():[];
    const selected=[HEADERS].concat(rows.filter(r=>!sessionId||r[6]===sessionId));
    return {csv:selected.map(r=>r.map(csvCell).join(',')).join('\r\n'),
      filename:session?csvFilename(session.label):csvFilename(null)};
  });
}
function getCsvData(sessionId, offset, limit) {return getCsvPage(sessionId,offset,limit).csv;}
function doGet(e) {
  if(e&&e.parameter&&e.parameter.download==='csv') {
    const page=getCsvPage(e.parameter.session||'',e.parameter.offset,e.parameter.limit);
    return ContentService.createTextOutput('\uFEFF'+page.csv).setMimeType(ContentService.MimeType.CSV).downloadAsFile(page.filename);
  }
  const template=HtmlService.createTemplateFromFile('Dashboard');
  template.webAppUrl=ScriptApp.getService().getUrl();
  template.refreshMs=DASHBOARD_REFRESH_MS;
  return template.evaluate().setTitle('Food Spoilage Monitor');
}
